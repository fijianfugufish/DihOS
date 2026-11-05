#include "config.h"
#include "MediaSourceDihOS.h"
#if ENABLE(MEDIA_SOURCE)
#include "ContentType.h"
#include "sample_decoder.h"
#include <wtf/Deque.h>
#include <wtf/HashMap.h>
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <cstring>

namespace WebCore {
static bool supportsAVC(const String& codec)
{
    if(codec.length()!=11 || !codec.startsWith("avc1."_s))return false;
    unsigned fields[3]{};
    for(unsigned i=0;i<6;++i){
        auto c=codec[5+i];unsigned value;
        if(c>='0' && c<='9')value=c-'0';else if(c>='a' && c<='f')value=c-'a'+10;else if(c>='A' && c<='F')value=c-'A'+10;else return false;
        fields[i/2]=(fields[i/2]<<4)|value;
    }
    return (fields[0]==66 || fields[0]==77 || fields[0]==88 || fields[0]==100) && fields[2]>=10 && fields[2]<=42;
}
bool supportsDihOSMediaSourceType(const ContentType& type)
{
    auto container=type.containerType();
    bool video=equalLettersIgnoringASCIICase(container,"video/mp4"_s);
    if(!video && !equalLettersIgnoringASCIICase(container,"audio/mp4"_s))return false;
    for(auto& codec:type.codecs()){
        if(codec=="mp4a.40.2"_s)continue;
        if(video && supportsAVC(codec))continue;
        return false;
    }
    return true;
}
// WorkQueue::mainSingleton uses RunLoop::mainSingleton in this port. Keep
// queue access, native decoding, callbacks and graphics on that one owner.
class DihOSDecoderConsumer final : public DihOSSampleConsumer {
public:
    explicit DihOSDecoderConsumer(bool video,Function<void()>&& changed):m_changed(WTF::move(changed)),m_allowVideo(video) { }
    ~DihOSDecoderConsumer() final { RELEASE_ASSERT(isMainThread());dihos_sample_decoder_close(m_decoder); }
    bool configure(Ref<DihOSMediaDescription>&& description) final {
        RELEASE_ASSERT(isMainThread());if(m_closed || (description->isVideo() && !m_allowVideo))return false;
        for(auto& track:m_tracks)if(track->identifier==description->identifier){
            if(track->isVideo()!=description->isVideo())return false;
            track=WTF::move(description);m_changed();return true;
        }
        if(m_tracks.size()>=2)return false;
        for(auto& track:m_tracks)if(track->isVideo()==description->isVideo())return false;
        m_tracks.append(WTF::move(description));return true;
    }
    bool ready(TrackID id) const final {
        RELEASE_ASSERT(isMainThread());
        if(m_closed || m_error)return false;
        for(auto& track:m_tracks)if(track->identifier==id)return track->isAudio() || (m_queue.size()<32 && m_bytes<=4*1024*1024-DIHOS_ENCODED_PACKET_MAX);
        return false;
    }
    void enqueue(Ref<DihOSMediaSample>&& sample,TrackID id) final {
        RELEASE_ASSERT(isMainThread());if(m_closed)return;
        if(sample->description->isAudio()){m_changed();return;} // Silent audio timeline lives in WebCore's track buffers.
        if(!ready(id) || sample->sizeInBytes()>DIHOS_ENCODED_PACKET_MAX){m_error=-1;m_changed();return;}
        m_allEnqueued=false;m_bytes+=sample->sizeInBytes();m_queue.append(WTF::move(sample));m_changed();
    }
    void flush(TrackID id) final {
        RELEASE_ASSERT(isMainThread());
        for(auto& track:m_tracks)if(track->identifier==id && track->isVideo()){
            m_queue.clear();m_bytes=0;m_allEnqueued=false;m_drainSent=false;m_configDraining=false;
            dihos_sample_decoder_flush(m_decoder);notifyReady();break;
        }
        m_changed();
    }
    void whenReady(TrackID id,Function<void()>&& function) final {
        RELEASE_ASSERT(isMainThread());m_waiters.set(id,WTF::move(function));notifyReady();
    }
    void allEnqueued(TrackID id) final {
        RELEASE_ASSERT(isMainThread());for(auto& track:m_tracks)if(track->identifier==id && track->isVideo())m_allEnqueued=true;
        m_changed();
    }
    void close() {
        RELEASE_ASSERT(isMainThread());m_closed=true;m_queue.clear();m_bytes=0;m_waiters.clear();
        dihos_sample_decoder_close(std::exchange(m_decoder,nullptr));m_decoderDescription=nullptr;m_tracks.clear();
    }
    bool reopen() {
        RELEASE_ASSERT(isMainThread());bool replay=m_drainSent;m_allEnqueued=false;m_drainSent=false;m_configDraining=false;
        if(replay)dihos_sample_decoder_flush(m_decoder);
        return replay;
    }
    FloatSize naturalSize() const {
        RELEASE_ASSERT(isMainThread());for(auto& track:m_tracks)if(track->isVideo())return {float(track->width),float(track->height)};
        return {};
    }
    int pull(bool ended,DihOSMSEFrame& out) {
        RELEASE_ASSERT(isMainThread());if(m_closed)return DIHOS_VIDEO_AGAIN;if(m_error)return m_error;
        for(unsigned work=0;work<16;++work){
            if(m_decoder){
                dihos_video_frame frame{};int result=dihos_sample_decoder_receive(m_decoder,&frame);
                if(result==DIHOS_VIDEO_FRAME){
                    size_t bytes=(size_t)frame.stride*frame.height;
                    if(bytes<out.pixels.size())out.pixels.shrink(bytes);
                    else if(!out.pixels.tryGrow(bytes))return m_error=-1;
                    memcpy(out.pixels.mutableSpan().data(),frame.bgra,bytes);
                    out.width=frame.width;out.height=frame.height;out.stride=frame.stride;out.presentation=frame.presentation_ms;out.duration=frame.duration_ms;
                    return result;
                }
                if(result==DIHOS_VIDEO_EOF){
                    if(m_configDraining){dihos_sample_decoder_close(std::exchange(m_decoder,nullptr));m_decoderDescription=nullptr;m_configDraining=false;m_drainSent=false;continue;}
                    return result;
                }
                if(result!=DIHOS_VIDEO_AGAIN)return m_error=result;
            }
            if(m_queue.isEmpty()){
                if(m_decoder && ended && m_allEnqueued && !m_drainSent){
                    int result=dihos_sample_decoder_send(m_decoder,nullptr);
                    if(result<0)return m_error=result;
                    if(!result)m_drainSent=true;
                    continue;
                }
                return DIHOS_VIDEO_AGAIN;
            }
            auto& sample=m_queue.first();auto& description=sample->description;
            if(m_decoder && (description->width!=m_decoderDescription->width || description->height!=m_decoderDescription->height ||
                description->configuration->span().size()!=m_decoderDescription->configuration->span().size() ||
                !std::equal(description->configuration->span().begin(),description->configuration->span().end(),m_decoderDescription->configuration->span().begin()))){
                if(!m_configDraining){int result=dihos_sample_decoder_send(m_decoder,nullptr);if(result<0)return m_error=result;if(!result)m_configDraining=true;}
                continue;
            }
            if(!m_decoder){
                auto config=description->configuration->span();
                dihos_encoded_track track{description->identifier,description->codecID,description->width,description->height,
                    description->sampleRate,description->channels,1,1000,config.data(),config.size()};
                int result=dihos_sample_decoder_create(&track,&m_decoder);if(result<0)return m_error=result;
                m_decoderDescription=description.copyRef();
            }
            // The source buffers retain exact rational time. Preserve it when
            // passing timestamp-offset/seek-adjusted samples into the decoder.
            auto pts=sample->presentationTime(),dts=sample->decodeTime(),duration=sample->duration();
            // Rescale only at the decoder boundary; WebCore's track buffers
            // above retain the original exact rational timestamps.
            const uint32_t scale=1000000;
            auto p=pts.toTimeScale(scale),d=dts.toTimeScale(scale),t=duration.toTimeScale(scale);
            if(!p.isFinite() || !d.isFinite() || !t.isFinite())return m_error=-1;
            auto bytes=sample->bytes->span();
            dihos_encoded_sample packet{sample->trackID(),bytes.data(),bytes.size(),p.timeValue(),d.timeValue(),t.timeValue(),1,(int32_t)scale,sample->isSync(),sample->isNonDisplaying()};
            int result=dihos_sample_decoder_send(m_decoder,&packet);
            if(result<0)return m_error=result;
            if(result==DIHOS_VIDEO_AGAIN)continue;
            m_bytes-=sample->sizeInBytes();m_queue.removeFirst();notifyReady();
        }
        return DIHOS_VIDEO_AGAIN;
    }
private:
    void notifyReady() {
        for(auto& track:m_tracks)if(ready(track->identifier)){
            auto callback=m_waiters.take(track->identifier);
            if(callback)WorkQueue::mainSingleton().dispatch(WTF::move(callback));
        }
    }
    Function<void()> m_changed;
    Vector<Ref<DihOSMediaDescription>> m_tracks;
    Deque<Ref<DihOSMediaSample>> m_queue;
    HashMap<TrackID,Function<void()>> m_waiters;
    size_t m_bytes{};
    dihos_sample_decoder* m_decoder{};
    RefPtr<DihOSMediaDescription> m_decoderDescription;
    int m_error{};
    bool m_allEnqueued{},m_drainSent{},m_closed{},m_allowVideo{},m_configDraining{};
};
Ref<MediaSourceDihOS> MediaSourceDihOS::create(MediaSourcePrivateClient& client,PlayerAccess&& access,Function<void()>&& wake)
{
    Ref source=adoptRef(*new MediaSourceDihOS(client,WTF::move(access),WTF::move(wake)));
    client.setPrivateAndOpen(source.copyRef());return source;
}
MediaSourceDihOS::MediaSourceDihOS(MediaSourcePrivateClient& client,PlayerAccess&& access,Function<void()>&& wake)
    :MediaSourcePrivate(client),m_playerAccess(WTF::move(access)),m_wake(WTF::move(wake)) { RELEASE_ASSERT(isMainThread()); }
MediaSourceDihOS::~MediaSourceDihOS(){RELEASE_ASSERT(isMainThread());for(auto& entry:m_entries)entry.consumer->close();}
RefPtr<MediaPlayerPrivateInterface> MediaSourceDihOS::player() const {RELEASE_ASSERT(isMainThread());return m_playerAccess?m_playerAccess():nullptr;}
void MediaSourceDihOS::setPlayer(MediaPlayerPrivateInterface* value){RELEASE_ASSERT(isMainThread());if(!value)detach();else RELEASE_ASSERT(player().get()==value);}
void MediaSourceDihOS::attach(PlayerAccess&& access,Function<void()>&& wake){RELEASE_ASSERT(isMainThread());++m_attachmentGeneration;m_playerAccess=WTF::move(access);m_wake=WTF::move(wake);m_attached=true;m_error=0;}
void MediaSourceDihOS::detach()
{
    RELEASE_ASSERT(isMainThread());++m_attachmentGeneration;m_attached=false;m_wake=nullptr;m_playerAccess=nullptr;cancelPendingWaitForTarget();
    Vector<Ref<SourceBufferDihOS>> buffers;
    for(auto& entry:m_entries){entry.consumer->close();buffers.append(entry.buffer.copyRef());}
    for(auto& buffer:buffers)buffer->removedFromMediaSource();
}
MediaSourcePrivate::AddStatus MediaSourceDihOS::addSourceBuffer(const ContentType& type,const MediaSourceConfiguration&,RefPtr<SourceBufferPrivate>& output)
{
    RELEASE_ASSERT(isMainThread());output=nullptr;
    if(!m_attached)return AddStatus::InvalidState;
    if(!supportsDihOSMediaSourceType(type))return AddStatus::NotSupported;
    bool video=equalLettersIgnoringASCIICase(type.containerType(),"video/mp4"_s);
    if(m_entries.size()>=2)return AddStatus::ReachedIdLimit;
    for(auto& entry:m_entries)if(entry.video==video)return AddStatus::ReachedIdLimit;
    Ref consumer=adoptRef(*new DihOSDecoderConsumer(video,[weak=ThreadSafeWeakPtr<MediaSourcePrivate>{*this}]{
        if(RefPtr source=weak.get())static_cast<MediaSourceDihOS&>(*source).wake();
    }));
    Ref buffer=SourceBufferDihOS::create(*this,consumer.copyRef(),video);
    {Locker locker{m_lock};m_sourceBuffers.append(buffer.copyRef());}
    output=buffer.ptr();buffer->setMediaSourceDuration(duration());m_entries.append({WTF::move(buffer),WTF::move(consumer),video});
    return AddStatus::Ok;
}
void MediaSourceDihOS::removeSourceBuffer(SourceBufferPrivate& buffer)
{
    RELEASE_ASSERT(isMainThread());
    for(size_t i=0;i<m_entries.size();++i)if(m_entries[i].buffer.ptr()==&buffer){m_entries[i].consumer->close();m_entries.removeAt(i);break;}
    MediaSourcePrivate::removeSourceBuffer(buffer);wake();
}
void MediaSourceDihOS::notifyActiveSourceBuffersChanged(){RELEASE_ASSERT(isMainThread());if(RefPtr owner=player()){
    // MediaPlayer receives this notification through its private implementation.
    owner->characteristicsFromMediaSourceChanged();
}wake();}
void MediaSourceDihOS::durationChanged(const MediaTime& time){MediaSourcePrivate::durationChanged(time);wake();}
void MediaSourceDihOS::bufferedChanged(PlatformTimeRanges&& ranges){MediaSourcePrivate::bufferedChanged(WTF::move(ranges));wake();}
void MediaSourceDihOS::markEndOfStream(EndOfStreamStatus status){
    RELEASE_ASSERT(isMainThread());
    for(auto& entry:m_entries)if(!entry.buffer->endInput())status=EndOfStreamStatus::DecodeError;
    MediaSourcePrivate::markEndOfStream(status);if(status!=EndOfStreamStatus::NoError)m_error=-1;wake();
}
void MediaSourceDihOS::unmarkEndOfStream()
{
    RELEASE_ASSERT(isMainThread());MediaSourcePrivate::unmarkEndOfStream();bool replay=false;
    for(auto& entry:m_entries)replay|=entry.consumer->reopen();
    if(replay)reenqueueMediaForTime(currentTime())->whenSettled(RunLoop::mainSingleton(),[weak=ThreadSafeWeakPtr<MediaSourcePrivate>{*this},generation=m_attachmentGeneration](auto&& result){
        if(RefPtr source=weak.get()){
            auto& own=static_cast<MediaSourceDihOS&>(*source);if(generation!=own.m_attachmentGeneration || !own.m_attached)return;
            if(!result)own.m_error=-1;own.wake();
        }
    });
    wake();
}
void MediaSourceDihOS::wake(){RELEASE_ASSERT(isMainThread());if(m_attached && m_wake)m_wake();}
int MediaSourceDihOS::nextFrame(DihOSMSEFrame& frame)
{
    RELEASE_ASSERT(isMainThread());if(m_error)return m_error;
    for(auto& entry:m_entries)if(entry.video && m_activeSourceBuffers.contains(entry.buffer.ptr())){
        int result=entry.consumer->pull(isEnded(),frame);if(result<0)m_error=result;return result;
    }
    return DIHOS_VIDEO_AGAIN;
}
FloatSize MediaSourceDihOS::naturalSize() const
{
    RELEASE_ASSERT(isMainThread());for(auto& entry:m_entries)if(entry.video)return entry.consumer->naturalSize();return {};
}
}
#endif
