#include "config.h"
#include "SourceBufferDihOS.h"
#if ENABLE(MEDIA_SOURCE)
#include "AudioTrackPrivate.h"
#include "VideoTrackPrivate.h"
#include "MediaSourcePrivate.h"
#include "ContentType.h"
#include <algorithm>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {
RefPtr<DihOSMediaDescription> DihOSMediaDescription::create(const dihos_encoded_track& track)
{
    if(!track.configuration || !track.configuration_size || track.configuration_size>DIHOS_ENCODED_CONFIG_MAX)return nullptr;
    String codec;
    if(track.codec==DIHOS_ENCODED_H264){
        if(track.configuration_size<7 || track.configuration[0]!=1)return nullptr;
        char name[]="avc1.000000";const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<3;++i){name[5+i*2]=hex[track.configuration[i+1]>>4];name[6+i*2]=hex[track.configuration[i+1]&15];}
        codec=String::fromLatin1(name);
    }else if(track.codec==DIHOS_ENCODED_AAC){
        // AAC-LC timing and buffers are supported here; audio output is separate.
        if(track.configuration_size<2 || (track.configuration[0]>>3)!=2)return nullptr;
        codec="mp4a.40.2"_s;
    }else return nullptr;
    return adoptRef(*new DihOSMediaDescription(track,codec.isolatedCopy(),SharedBuffer::create(std::span(track.configuration,track.configuration_size))));
}
DihOSMediaDescription::DihOSMediaDescription(const dihos_encoded_track& t,String&& codec,Ref<SharedBuffer>&& config)
    :MediaDescription(WTF::move(codec)),identifier(t.id),codecID(t.codec),width(t.width),height(t.height),
     sampleRate(t.sample_rate),channels(t.channels),configuration(WTF::move(config)) { }
RefPtr<DihOSMediaSample> DihOSMediaSample::create(const dihos_encoded_sample& sample,Ref<DihOSMediaDescription>&& description)
{
    if(sample.track_id!=description->identifier || !sample.bytes || !sample.size || sample.size>DIHOS_ENCODED_PACKET_MAX ||
       sample.time_num<=0 || sample.time_den<=0 || sample.duration<=0)return nullptr;
    MediaTime pts=MediaTime(sample.presentation,sample.time_den)*sample.time_num;
    MediaTime dts=MediaTime(sample.decode,sample.time_den)*sample.time_num;
    MediaTime duration=MediaTime(sample.duration,sample.time_den)*sample.time_num;
    if(!pts.isFinite() || !dts.isFinite() || !duration.isFinite() || duration<=MediaTime::zeroTime())return nullptr;
    return adoptRef(*new DihOSMediaSample(SharedBuffer::create(std::span(sample.bytes,sample.size)),WTF::move(description),
        pts,dts,duration,SampleFlags(HasSyncInfo|(sample.sync?IsSync:None))));
}
DihOSMediaSample::DihOSMediaSample(Ref<SharedBuffer>&& data,Ref<DihOSMediaDescription>&& desc,
    MediaTime pts,MediaTime dts,MediaTime duration,SampleFlags flags)
    :bytes(WTF::move(data)),description(WTF::move(desc)),m_pts(pts),m_dts(dts),m_duration(duration),m_flags(flags) { }
Ref<MediaSample> DihOSMediaSample::createNonDisplayingCopy() const
{
    return adoptRef(*new DihOSMediaSample(bytes.copyRef(),description.copyRef(),m_pts,m_dts,m_duration,SampleFlags(m_flags|IsNonDisplaying)));
}
Ref<MediaSample> DihOSMediaSample::createCopyWithAdjustedStartTime(const MediaTime& offset) const
{
    auto clamped=std::max(MediaTime::zeroTime(),std::min(offset,m_duration));
    return adoptRef(*new DihOSMediaSample(bytes.copyRef(),description.copyRef(),m_pts+clamped,m_dts+clamped,m_duration-clamped,m_flags));
}
namespace {
class VideoTrack final : public VideoTrackPrivate {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(VideoTrack);
public:
    static Ref<VideoTrack> create(TrackID id){return adoptRef(*new VideoTrack(id));}
    TrackID id() const final { return m_id; }
    Kind kind() const final { return Kind::Main; }
private:
    explicit VideoTrack(TrackID id):m_id(id) { }
    const TrackID m_id;
};
class AudioTrack final : public AudioTrackPrivate {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(AudioTrack);
public:
    static Ref<AudioTrack> create(TrackID id){return adoptRef(*new AudioTrack(id));}
    TrackID id() const final { return m_id; }
private:
    explicit AudioTrack(TrackID id):m_id(id) { }
    const TrackID m_id;
};
}
Ref<SourceBufferDihOS> SourceBufferDihOS::create(MediaSourcePrivate& source,Ref<DihOSSampleConsumer>&& consumer,bool video)
{ return adoptRef(*new SourceBufferDihOS(source,WTF::move(consumer),video)); }
SourceBufferDihOS::SourceBufferDihOS(MediaSourcePrivate& source,Ref<DihOSSampleConsumer>&& consumer,bool video)
    :SourceBufferPrivate(source),m_consumer(WTF::move(consumer)),m_videoContainer(video)
{ dihos_fmp4_create(&m_parser); }
SourceBufferDihOS::~SourceBufferDihOS()
{ dihos_encoded_close(m_demux);dihos_fmp4_close(m_parser); }
bool SourceBufferDihOS::canSwitchToType(const ContentType& type)
{
    if(!supportsDihOSMediaSourceType(type) || type.codecs().isEmpty() ||
       !equalLettersIgnoringASCIICase(type.containerType(),m_videoContainer?"video/mp4"_s:"audio/mp4"_s))return false;
    bool video=false,audio=false,hadVideo=false,hadAudio=false;
    for(auto& codec:type.codecs()){if(codec=="mp4a.40.2"_s)audio=true;else video=true;}
    for(auto& description:m_descriptions){hadVideo|=description->isVideo();hadAudio|=description->isAudio();}
    return m_descriptions.isEmpty() || (video==hadVideo && audio==hadAudio);
}
void SourceBufferDihOS::startChangingType()
{
    SourceBufferPrivate::startChangingType();
    ensureOnDispatcher([protectedThis=Ref{*this}]{
        ++protectedThis->m_generation;protectedThis->settle(PlatformMediaError::Cancelled);
        dihos_fmp4_reset(protectedThis->m_parser,0);protectedThis->m_announced=false;
    });
}
Ref<MediaPromise> SourceBufferDihOS::appendInternal(Ref<SharedBuffer>&& data)
{
    if(!m_parser)return MediaPromise::createAndReject(PlatformMediaError::MemoryError);
    if(m_removed || m_promise || data->size()>DIHOS_FMP4_LIMIT)return MediaPromise::createAndReject(PlatformMediaError::InvalidState);
    m_promise.emplace(PlatformMediaError::Cancelled);Ref promise=m_promise->promise();
    m_append=WTF::move(data);m_offset=0;auto generation=++m_generation;
    m_dispatcher->dispatch([protectedThis=Ref{*this},generation]{protectedThis->pump(generation);});
    return promise;
}
bool SourceBufferDihOS::initializeTracks()
{
    auto version=dihos_fmp4_initialization_version(m_parser);
    if(m_announced && version==m_initializationVersion)return true;
    const uint8_t* data=nullptr;size_t bytes=0;
    if(dihos_fmp4_initialization(m_parser,&data,&bytes)!=DIHOS_FMP4_SEGMENT)return true;
    dihos_encoded_media* init=nullptr;
    if(dihos_encoded_open(data,bytes,&init)<0)return false;
    InitializationSegment segment;segment.duration=MediaTime::positiveInfiniteTime();
    Vector<Ref<DihOSMediaDescription>> descriptions;
    for(unsigned i=0;i<dihos_encoded_track_count(init);++i){
        dihos_encoded_track track{};
        if(dihos_encoded_track_at(init,i,&track)<0){dihos_encoded_close(init);return false;}
        auto description=DihOSMediaDescription::create(track);
        if(!description || !m_consumer->configure(Ref{*description})){dihos_encoded_close(init);return false;}
        if(description->isVideo())segment.videoTracks.append({description,VideoTrack::create(track.id)});
        else segment.audioTracks.append({description,AudioTrack::create(track.id)});
        descriptions.append(description.releaseNonNull());
    }
    dihos_encoded_close(init);
    if(!m_descriptions.isEmpty()){
        if(descriptions.size()!=m_descriptions.size())return false;
        for(unsigned i=0;i<descriptions.size();++i)if(descriptions[i]->identifier!=m_descriptions[i]->identifier ||
            descriptions[i]->isVideo()!=m_descriptions[i]->isVideo())return false;
    }
    m_descriptions=WTF::move(descriptions);m_announced=true;m_initializationVersion=version;
    didReceiveInitializationSegment(WTF::move(segment));return true;
}
void SourceBufferDihOS::settle(std::optional<PlatformMediaError> error)
{
    dihos_encoded_close(std::exchange(m_demux,nullptr));m_append=nullptr;m_offset=0;
    if(auto promise=std::exchange(m_promise,std::nullopt)){
        if(error)promise->reject(*error);else promise->resolve();
    }
}
void SourceBufferDihOS::pump(uint64_t generation)
{
    if(generation!=m_generation || !m_promise)return;
    if(m_removed){settle(PlatformMediaError::BufferRemoved);return;}
    // At most 16 packets/assembler steps per dispatch. The caller's append is
    // retained until consumed, and the native assembler permits one output.
    for(unsigned work=0;work<16;++work){
        if(m_demux){
            dihos_encoded_sample sample{};int result=dihos_encoded_next(m_demux,&sample);
            if(result<0){settle(PlatformMediaError::ParsingError);return;}
            if(!result){dihos_encoded_close(std::exchange(m_demux,nullptr));dihos_fmp4_consume(m_parser);continue;}
            RefPtr<DihOSMediaDescription> description;
            for(auto& candidate:m_descriptions)if(candidate->identifier==sample.track_id){description=candidate.ptr();break;}
            if(!description){settle(PlatformMediaError::ParsingError);return;}
            auto value=DihOSMediaSample::create(sample,description.releaseNonNull());
            if(!value){settle(PlatformMediaError::ParsingError);return;}
            didReceiveSample(value.releaseNonNull());continue;
        }
        if(m_offset==m_append->size()){settle(std::nullopt);return;}
        auto bytes=m_append->span().subspan(m_offset);size_t used=0;
        int result=dihos_fmp4_append(m_parser,bytes.data(),bytes.size(),&used);m_offset+=used;
        if(result<0 || !initializeTracks()){settle(PlatformMediaError::ParsingError);return;}
        if(result==DIHOS_FMP4_SEGMENT){
            const uint8_t* segment=nullptr;size_t length=0;
            if(dihos_fmp4_view(m_parser,&segment,&length)!=1 || dihos_encoded_open(segment,length,&m_demux)<0){settle(PlatformMediaError::ParsingError);return;}
            // Track IDs and codec configuration cannot silently change between
            // fragments; a new initialization segment requires parser reset.
            if(dihos_encoded_track_count(m_demux)!=m_descriptions.size()){settle(PlatformMediaError::ParsingError);return;}
            for(unsigned i=0;i<m_descriptions.size();++i){
                dihos_encoded_track t{};auto& known=m_descriptions[i];
                if(dihos_encoded_track_at(m_demux,i,&t)<0 || t.id!=known->identifier || t.codec!=known->codecID ||
                   t.configuration_size!=known->configuration->size() ||
                   !std::equal(known->configuration->span().begin(),known->configuration->span().end(),t.configuration)){
                    settle(PlatformMediaError::ParsingError);return;
                }
            }
        }else if(!used){settle(PlatformMediaError::ParsingError);return;}
    }
    m_dispatcher->dispatch([protectedThis=Ref{*this},generation]{protectedThis->pump(generation);});
}
void SourceBufferDihOS::resetParserStateInternal()
{
    ++m_generation;settle(PlatformMediaError::Cancelled);dihos_fmp4_reset(m_parser,1);
}
void SourceBufferDihOS::removedFromMediaSource()
{
    m_removed=true;
    ensureOnDispatcher([protectedThis=Ref{*this}]{
        ++protectedThis->m_generation;protectedThis->settle(PlatformMediaError::BufferRemoved);
        dihos_fmp4_reset(protectedThis->m_parser,0);
        for(auto& description:protectedThis->m_descriptions)protectedThis->m_consumer->flush(description->identifier);
        protectedThis->m_descriptions.clear();protectedThis->m_announced=false;
    });
    SourceBufferPrivate::removedFromMediaSource();
}
void SourceBufferDihOS::enqueueSample(Ref<MediaSample>&& sample,TrackID id)
{
    if(sample->type()!=MediaSample::Type::DihOS || m_removed)return;
    Ref<DihOSMediaSample> owned=static_cast<DihOSMediaSample&>(sample.get());m_consumer->enqueue(WTF::move(owned),id);
}
void SourceBufferDihOS::notifyClientWhenReadyForMoreSamples(TrackID id)
{
    m_consumer->whenReady(id,[weakThis=ThreadSafeWeakPtr{*this},id]{
        if(RefPtr buffer=weakThis.get())buffer->m_dispatcher->dispatch([weakThis,id]{
            if(RefPtr buffer=weakThis.get();buffer && !buffer->m_removed)buffer->provideMediaData(id);
        });
    });
}
}
#endif
