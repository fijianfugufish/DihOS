// Muted progressive-file player. Networking/origin policy stays in WebCore.
// All FFmpeg calls are serialized on the renderer main thread in preemptible
// EL0. No backend callback waits for network completion or sleeps for a frame.
#include "config.h"
#include "MediaPlayerDihOS.h"
#include "MediaPlayerPrivate.h"
#include "PlatformMediaResourceLoader.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "ResourceError.h"
#include "GraphicsContext.h"
#include "DestinationColorSpace.h"
#include "NativeImage.h"
#include "ContentType.h"
#include "memory_video.h"
#if ENABLE(MEDIA_SOURCE)
#include "MediaSourceDihOS.h"
#endif
#include <cairo.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <limits.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RefCounted.h>
#include <wtf/RunLoop.h>
#include <wtf/ThreadSafeWeakPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/TZoneMallocInlines.h>
#include <sacx_user_api.h>

namespace WebCore {
using namespace WTF;
namespace {
bool testTracing;
unsigned testTraceMessages;
// Complete-file loading and MSE have separate bounded input paths. The MSE
// backend accepts AVC and silent AAC timing; WebM/AV1/encrypted media stay off.
MediaPlayer::SupportsType supports(const MediaEngineSupportParameters& parameters)
{
#if ENABLE(MEDIA_SOURCE)
    if(parameters.platformType==PlatformMediaDecodingType::MediaSource && !parameters.requiresRemotePlayback){
        if(!supportsDihOSMediaSourceType(parameters.type))return MediaPlayer::SupportsType::IsNotSupported;
        return parameters.type.codecs().isEmpty()?MediaPlayer::SupportsType::MayBeSupported:MediaPlayer::SupportsType::IsSupported;
    }
#endif
    if(parameters.platformType!=PlatformMediaDecodingType::FileOrHLS || parameters.requiresRemotePlayback)
        return MediaPlayer::SupportsType::IsNotSupported;
    auto container=parameters.type.containerType();
    if(!equalLettersIgnoringASCIICase(container,"video/mp4"_s))
        return MediaPlayer::SupportsType::IsNotSupported;
    auto codecs=parameters.type.codecs();
    if(codecs.isEmpty())return MediaPlayer::SupportsType::MayBeSupported;
    for(auto& codec:codecs){
        if(!codec.startsWith("avc1."_s) && !codec.startsWith("avc3."_s))
            return MediaPlayer::SupportsType::IsNotSupported;
    }
    return MediaPlayer::SupportsType::IsSupported;
}
class Player final : public MediaPlayerPrivateInterface, public RefCounted<Player>, public CanMakeWeakPtr<Player> {
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }
    explicit Player(MediaPlayer& player)
        :m_player(player),m_timer(RunLoop::mainSingleton(),"DihOS video frame"_s,this,&Player::tick) { }
    ~Player() final { reset(); }
    constexpr MediaPlayerType mediaPlayerType() const final { return MediaPlayerType::DihOS; }
    String engineDescription() const final {
#if ENABLE(MEDIA_SOURCE)
        return "DihOS software video (muted MP4/MSE)"_s;
#else
        return "DihOS software video (muted progressive MP4)"_s;
#endif
    }
    void load(const URL&,const LoadOptions&) final;
#if ENABLE(MEDIA_SOURCE)
    void load(const URL&,const LoadOptions&,MediaSourcePrivateClient&) final;
    void readyStateFromMediaSourceChanged() final;
    void characteristicsFromMediaSourceChanged() final;
    void mediaSourceHasRetrievedAllData() final;
#endif
    void cancelLoad() final { reset();m_network=MediaPlayer::NetworkState::Empty; }
    void play() final;
    void pause() final;
    FloatSize naturalSize() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse && m_size.isEmpty())return m_mse->naturalSize();
#endif
        return m_size;
    }
    bool hasAvailableVideoFrame() const final { return !!m_image; }
    bool hasVideo() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse)return m_mse->hasVideo() || !m_mse->naturalSize().isEmpty();
#endif
        return !!m_decoder;
    }
    bool hasAudio() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse)return m_mse->hasAudio();
#endif
        return false;
    }
    void setPageIsVisible(bool) final { }
    MediaTime duration() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse)return m_mse->duration();
#endif
        return m_duration<0?MediaTime::positiveInfiniteTime():MediaTime(m_duration,1000);
    }
    MediaTime currentTime() const final {
        MediaTime time(position(),1000);
#if ENABLE(MEDIA_SOURCE)
        // Millisecond presentation deadlines round upwards; expose the exact
        // rational endpoint so a fractional AAC tail can still reach ended.
        if(m_mse){auto end=m_mse->duration();if(end.isFinite() && time>end)return end;}
#endif
        return time;
    }
    Ref<MediaTimePromise> seekToTarget(const SeekTarget&) final;
    bool paused() const final { return m_paused; }
    bool ended() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse){auto end=m_mse->duration();return m_mse->isEnded() && end.isFinite() && currentTime()>=end;}
#endif
        return m_ended;
    }
    double rate() const final { return m_rate; }
    void setRateDouble(double) final;
    bool timeIsProgressing() const final { return !m_paused && m_rate>0 && !m_stalled; }
    MediaPlayer::NetworkState networkState() const final { return m_network; }
    MediaPlayer::ReadyState readyState() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse && hasVideo() && !m_image && m_ready>MediaPlayer::ReadyState::HaveMetadata)return MediaPlayer::ReadyState::HaveMetadata;
#endif
        return m_ready;
    }
    const PlatformTimeRanges& buffered() const final { return m_buffered; }
    MediaTime maxTimeSeekable() const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse)return m_mse->seekable().maximumBufferedTime();
#endif
        return m_duration>=0?MediaTime(m_duration,1000):MediaTime::zeroTime();
    }
    unsigned long long totalBytes() const final { return m_totalBytes; }
    bool didLoadingProgress() const final { return std::exchange(m_progress,false); }
    bool didPassCORSAccessCheck() const final {
#if ENABLE(MEDIA_SOURCE)
        // MSE receives bytes already available to its owning JavaScript realm.
        // Cross-origin network reads still go through WebCore Fetch/CORS.
        if(m_mse)return true;
#endif
        return m_passedCORS;
    }
    std::optional<bool> isCrossOrigin(const SecurityOrigin&) const final {
#if ENABLE(MEDIA_SOURCE)
        if(m_mse)return false;
#endif
        return std::nullopt;
    }
    void paint(GraphicsContext& context,const FloatRect& destination) final {
        if(m_image)context.drawNativeImage(*m_image,destination,FloatRect(FloatPoint(),m_size));
    }
    RefPtr<NativeImage> nativeImageForCurrentTime() final { return m_image; }
    DestinationColorSpace colorSpace() final { return DestinationColorSpace::SRGB(); }
private:
    class Client final : public PlatformMediaResourceClient {
    public:
        Client(Player& owner,uint64_t generation):m_owner(owner),m_generation(generation) { }
        void responseReceived(PlatformMediaResource&,const ResourceResponse&,CompletionHandler<void(ShouldContinuePolicyCheck)>&&) final;
        void dataReceived(PlatformMediaResource&,const SharedBuffer&) final;
        void loadFinished(PlatformMediaResource&,const NetworkLoadMetrics&) final;
        void loadFailed(PlatformMediaResource&,const ResourceError&) final;
        void accessControlCheckFailed(PlatformMediaResource&,const ResourceError& error) final { loadFailedPlaceholder(error); }
        bool shouldCacheResponse(PlatformMediaResource&,const ResourceResponse&) final { return false; }
    private:
        void loadFailedPlaceholder(const ResourceError&);
        RefPtr<Player> owner() const { auto* p=m_owner.get();return p && p->m_generation==m_generation?RefPtr<Player>(p):nullptr; }
        WeakPtr<Player> m_owner;
        uint64_t m_generation;
    };
    void reset();
    void fail(MediaPlayer::NetworkState,const char*);
    void finish();
    void tick();
#if ENABLE(MEDIA_SOURCE)
    void wakeMSE();
    bool refreshMSEDuration();
    void tickMSE();
    Ref<MediaTimePromise> seekMSE(const SeekTarget&);
    RefPtr<MediaSourceDihOS> m_mse;
    DihOSMSEFrame m_mseFrame;
    MediaTime m_mseDuration{MediaTime::invalidTime()};
    bool m_mseSeekingReady{};
    int64_t m_stallLimit{INT64_MAX};
#endif
    void trace(const char* action) const {
        if(!testTracing || testTraceMessages++>=64)return;
        char text[256];std::snprintf(text,sizeof(text),"Video test %s: position=%lld rate100=%d paused=%u ready=%u seek=%u duration=%lld stalled=%u image=%u",
            action,(long long)position(),(int)(m_rate*100),m_paused,(unsigned)m_ready,!!m_seekPromise,(long long)m_duration,m_stalled,!!m_image);
        sacx_user_log(text);
    }
    bool present(const dihos_video_frame&);
    int64_t position() const;
    void detachResource(){if(auto resource=std::exchange(m_resource,nullptr)){resource->setClient(nullptr);resource->shutdown();}}
    ThreadSafeWeakPtr<MediaPlayer> m_player;
    RunLoop::Timer m_timer;
    RefPtr<PlatformMediaResource> m_resource;
    Vector<uint8_t> m_data;
    dihos_video* m_decoder{};
    dihos_video_frame m_pending{};
    bool m_hasPending{},m_paused{true},m_ended{},m_decodeEnded{},m_passedCORS{},m_responseAccepted{},m_firstNotified{},m_stalled{};
    mutable bool m_progress{};
    uint64_t m_generation{},m_totalBytes{};
    int64_t m_duration{-1},m_position{},m_frameEnd{};
    double m_rate{1};
    std::optional<MediaTimePromise::AutoRejectProducer> m_seekPromise;
    MonotonicTime m_anchor;
    FloatSize m_size;
    RefPtr<NativeImage> m_image;
    PlatformTimeRanges m_buffered;
    MediaPlayer::NetworkState m_network{MediaPlayer::NetworkState::Empty};
    MediaPlayer::ReadyState m_ready{MediaPlayer::ReadyState::HaveNothing};
};
void Player::reset()
{
    ASSERT(isMainThread());++m_generation;m_timer.stop();detachResource();
#if ENABLE(MEDIA_SOURCE)
    if(auto source=std::exchange(m_mse,nullptr))source->detach();
    m_mseFrame={};m_stallLimit=INT64_MAX;m_mseDuration=MediaTime::invalidTime();m_mseSeekingReady=false;
#endif
    dihos_video_close(std::exchange(m_decoder,nullptr));m_data=Vector<uint8_t>{};m_image=nullptr;
    m_hasPending=false;m_pending={};m_paused=true;m_ended=false;m_decodeEnded=false;m_passedCORS=false;
    m_responseAccepted=false;m_progress=false;m_totalBytes=0;m_position=0;m_duration=-1;
    m_firstNotified=false;m_stalled=false;m_frameEnd=0;m_seekPromise.reset();m_size={};m_buffered={};m_ready=MediaPlayer::ReadyState::HaveNothing;
}
void Player::fail(MediaPlayer::NetworkState state,const char* reason)
{
    Ref protect=*this;reset();m_network=state;sacx_user_log(reason);
    if(RefPtr player=m_player.get()){player->networkStateChanged();player->readyStateChanged();}
}
void Player::load(const URL& url,const LoadOptions&)
{
    Ref protect=*this;reset();m_network=MediaPlayer::NetworkState::Loading;
    if(RefPtr player=m_player.get()){
        auto loader=player->mediaResourceLoader();
        m_resource=loader->requestResource(ResourceRequest(URL(url)),PlatformMediaResourceLoader::DisallowCaching);
        if(!m_resource){fail(MediaPlayer::NetworkState::NetworkError,"Video: resource request rejected");return;}
        m_resource->setClient(adoptRef(*new Client(*this,m_generation)));
        player->networkStateChanged();
    }
}
void Player::Client::responseReceived(PlatformMediaResource&,const ResourceResponse& response,
    CompletionHandler<void(ShouldContinuePolicyCheck)>&& completion)
{
    Ref protect=*this;auto p=owner();
    bool accepted=p && (response.httpStatusCode()==200 || response.httpStatusCode()==0)
        && response.expectedContentLength()<=DIHOS_VIDEO_INPUT_MAX;
    if(p)p->m_responseAccepted=accepted;
    completion(accepted?ShouldContinuePolicyCheck::Yes:ShouldContinuePolicyCheck::No);
    if(!accepted){if(auto current=owner())current->fail(MediaPlayer::NetworkState::NetworkError,"Video: response rejected or complete input exceeds 8 MiB");}
}
void Player::Client::dataReceived(PlatformMediaResource&,const SharedBuffer& bytes)
{
    Ref protect=*this;auto p=owner();if(!p)return;
    size_t old=p->m_data.size();
    if(!p->m_responseAccepted || bytes.size()>DIHOS_VIDEO_INPUT_MAX-old || !p->m_data.tryGrow(old+bytes.size())){
        p->fail(MediaPlayer::NetworkState::NetworkError,"Video: owned input allocation or size limit");return;
    }
    memcpy(p->m_data.mutableSpan().data()+old,bytes.span().data(),bytes.size());
    p->m_totalBytes=p->m_data.size();p->m_progress=true;
}
void Player::Client::loadFinished(PlatformMediaResource& resource,const NetworkLoadMetrics&)
{
    Ref protect=*this;if(auto p=owner()){p->m_passedCORS=resource.didPassAccessControlCheck();p->detachResource();p->finish();}
}
void Player::Client::loadFailed(PlatformMediaResource&,const ResourceError&)
{ Ref protect=*this;if(auto p=owner())p->fail(MediaPlayer::NetworkState::NetworkError,"Video: resource load failed"); }
void Player::Client::loadFailedPlaceholder(const ResourceError&)
{ Ref protect=*this;if(auto p=owner())p->fail(MediaPlayer::NetworkState::NetworkError,"Video: resource origin policy denied access"); }
void Player::finish()
{
    Ref protect=*this;
    if(!m_responseAccepted || dihos_video_open(m_data.span().data(),m_data.size(),&m_decoder)<0){
        fail(MediaPlayer::NetworkState::DecodeError,"Video: MP4/codec initialization failed");return;
    }
    m_data=Vector<uint8_t>{};m_duration=dihos_video_duration_ms(m_decoder);
    if(m_duration>=0)m_buffered.add(MediaTime::zeroTime(),MediaTime(m_duration,1000));
    m_network=MediaPlayer::NetworkState::Loaded;
    m_anchor=MonotonicTime::now();
    auto generation=m_generation;
    if(RefPtr player=m_player.get()){player->durationChanged();player->networkStateChanged();player->readyStateChanged();}
    if(generation==m_generation)m_timer.startOneShot(0_s);
}
int64_t Player::position() const
{
    int64_t ms=m_position;
    bool active=!!m_decoder;
#if ENABLE(MEDIA_SOURCE)
    active|=!!m_mse;
#endif
    if(!m_paused && !m_stalled && active)ms+=(MonotonicTime::now()-m_anchor).milliseconds()*m_rate;
#if ENABLE(MEDIA_SOURCE)
    if(m_mse && ms>m_stallLimit)ms=m_stallLimit;
#endif
    if(m_duration>=0 && ms>m_duration)ms=m_duration;
    return ms;
}
void Player::play()
{
    trace("play");
    Ref protect=*this;if(!m_paused)return;
    m_paused=false;m_ended=false;m_anchor=MonotonicTime::now();
#if ENABLE(MEDIA_SOURCE)
    if(m_mse)m_timer.startOneShot(0_s);
#endif
    if(m_decoder)m_timer.startOneShot(0_s);
    if(RefPtr player=m_player.get())player->playbackStateChanged();
}
void Player::pause()
{
    trace("pause");
    Ref protect=*this;m_position=position();m_paused=true;m_timer.stop();
#if ENABLE(MEDIA_SOURCE)
    if(m_mse && (!m_image || m_seekPromise))m_timer.startOneShot(0_s);
#endif
    if(m_decoder && !m_image)m_timer.startOneShot(0_s);
    auto generation=m_generation;
    if(RefPtr player=m_player.get()){
        // WebCore may observe the clock reaching duration between our timer
        // ticks and pause us from updatePlayState(). Publish the final position
        // before stopping notifications, otherwise the ended task is lost.
        player->timeChanged();
        if(generation==m_generation)player->playbackStateChanged();
    }
}
void Player::setRateDouble(double rate)
{
    Ref protect=*this;
    if(std::isfinite(rate) && rate>=0 && rate<=4){
        m_position=position();m_anchor=MonotonicTime::now();m_rate=rate;
        bool active=!!m_decoder;
#if ENABLE(MEDIA_SOURCE)
        active|=!!m_mse;
#endif
        if(!m_paused && active && rate>0)m_timer.startOneShot(0_s);
        else if(!rate && m_image)m_timer.stop();
    }
    if(RefPtr player=m_player.get())player->rateChanged();
    trace("rate");
}
Ref<MediaTimePromise> Player::seekToTarget(const SeekTarget& target)
{
#if ENABLE(MEDIA_SOURCE)
    if(m_mse)return seekMSE(target);
#endif
    Ref protect=*this;double ms=target.time.toDouble()*1000;
    if(!std::isfinite(ms) || ms<0 || ms>double(INT64_MAX/1000) || !m_decoder || dihos_video_seek_ms(m_decoder,(int64_t)ms)<0)
        return MediaTimePromise::createAndReject(PlatformMediaError::Cancelled);
    m_hasPending=false;m_pending={};m_image=nullptr;m_position=ms;m_anchor=MonotonicTime::now();m_ended=false;m_decodeEnded=false;
    m_seekPromise.reset();m_seekPromise.emplace(PlatformMediaError::Cancelled);
    Ref promise=m_seekPromise->promise();
    m_timer.startOneShot(0_s);
    trace("seek");
    return promise;
}
bool Player::present(const dihos_video_frame& frame)
{
    auto surface=adoptRef(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,frame.width,frame.height));
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)return false;
    auto* pixels=cairo_image_surface_get_data(surface.get());int stride=cairo_image_surface_get_stride(surface.get());
    for(unsigned y=0;y<frame.height;++y)memcpy(pixels+y*stride,frame.bgra+y*frame.stride,frame.width*4);
    cairo_surface_mark_dirty(surface.get());
    m_image=NativeImage::create(WTF::move(surface));
    if(!m_image)return false;
    FloatSize size(frame.width,frame.height);bool resized=m_size!=size;m_size=size;
    m_ready=MediaPlayer::ReadyState::HaveEnoughData;
#if ENABLE(MEDIA_SOURCE)
    if(m_mse)m_ready=m_mse->mediaPlayerReadyState();
#endif
    m_frameEnd=frame.presentation_ms+frame.duration_ms;
    if(m_seekPromise)trace("seek frame");
    if(auto seek=std::exchange(m_seekPromise,std::nullopt))seek->resolve(currentTime());
    if(RefPtr player=m_player.get()){
        if(resized)player->sizeChanged();player->readyStateChanged();player->timeChanged();player->repaint();
        if(!m_firstNotified){m_firstNotified=true;player->firstVideoFrameAvailable();}
    }
    return true;
}
void Player::tick()
{
#if ENABLE(MEDIA_SOURCE)
    if(m_mse){tickMSE();return;}
#endif
    Ref protect=*this;if(!m_decoder)return;auto generation=m_generation;
    auto finishPlayback=[&]{
        int64_t end=m_duration>=0?m_duration:m_frameEnd;
        if(position()<end){if(!m_paused && m_rate>0)m_timer.startOneShot(16_ms);return;}
        m_position=end;m_paused=true;m_ended=true;
        if(RefPtr player=m_player.get()){player->timeChanged();player->playbackStateChanged();}
    };
    if(m_decodeEnded){finishPlayback();return;}
    if(!m_hasPending){
        int result=dihos_video_next(m_decoder,&m_pending);
        if(result<0){fail(MediaPlayer::NetworkState::DecodeError,"Video: frame decoding failed");return;}
        if(result==DIHOS_VIDEO_EOF){
            m_decodeEnded=true;finishPlayback();return;
        }
        if(result==DIHOS_VIDEO_AGAIN){m_timer.startOneShot(1_ms);return;}
        m_hasPending=true;
    }
    // Catch up after a slow frame without displaying every stale frame at a
    // fixed delay. Keep decode work bounded and yield the renderer event loop.
    if(m_image && !m_paused && !m_seekPromise && m_pending.presentation_ms+m_pending.duration_ms<=position()){
        m_hasPending=false;m_timer.startOneShot(1_ms);return;
    }
    if(m_image && m_pending.presentation_ms>position()){
        if(!m_paused && m_rate>0)m_timer.startOneShot(16_ms);return;
    }
    if(!present(m_pending)){if(generation==m_generation)fail(MediaPlayer::NetworkState::DecodeError,"Video: frame surface allocation failed");return;}
    if(generation!=m_generation)return;
    m_hasPending=false;
    if(!m_paused && m_rate>0)m_timer.startOneShot(16_ms);
}
#if ENABLE(MEDIA_SOURCE)
void Player::load(const URL&,const LoadOptions&,MediaSourcePrivateClient& client)
{
    Ref protect=*this;reset();m_stalled=true;m_network=MediaPlayer::NetworkState::Loading;
    auto access=[weak=WeakPtr{*this}]()->RefPtr<MediaPlayerPrivateInterface>{return weak.get();};
    auto wake=[weak=WeakPtr{*this}]{if(auto* player=weak.get())player->wakeMSE();};
    if(auto existing=client.mediaSourcePrivate();existing && existing->platformType()==MediaPlatformType::DihOS){
        m_mse=static_cast<MediaSourceDihOS&>(*existing);m_mse->attach(WTF::move(access),WTF::move(wake));client.reOpen();
    }else m_mse=MediaSourceDihOS::create(client,WTF::move(access),WTF::move(wake));
    if(RefPtr player=m_player.get())player->networkStateChanged();wakeMSE();
}
void Player::wakeMSE()
{
    RELEASE_ASSERT(isMainThread());Ref protect=*this;
    if(refreshMSEDuration() && !m_timer.isActive())m_timer.startOneShot(0_s);
}
bool Player::refreshMSEDuration()
{
    if(!m_mse)return false;Ref source=*m_mse;auto generation=m_generation;
    auto duration=source->duration();
    if(duration!=m_mseDuration){
        auto previous=m_mseDuration;
        m_mseDuration=duration;double ms=duration.toDouble()*1000;
        m_duration=std::isfinite(ms) && ms>=0 && ms<double(INT64_MAX/4)?(int64_t)std::ceil(ms):-1;
        if(RefPtr player=m_player.get()){
            player->durationChanged();
            // Extending an ended stream makes its old endpoint playable again.
            // Re-evaluate that position before playback can reach the new end.
            if(generation==m_generation && m_mse.get()==source.ptr() && previous.isFinite() && duration>previous)
                player->timeChanged();
        }
    }
    return generation==m_generation && m_mse.get()==source.ptr();
}
void Player::readyStateFromMediaSourceChanged()
{
    Ref protect=*this;if(!m_mse)return;m_ready=m_mse->mediaPlayerReadyState();
    if(RefPtr player=m_player.get())player->readyStateChanged();wakeMSE();
}
void Player::characteristicsFromMediaSourceChanged()
{
    Ref protect=*this;if(RefPtr player=m_player.get()){player->characteristicChanged();player->activeSourceBuffersChanged();player->sizeChanged();}wakeMSE();
}
void Player::mediaSourceHasRetrievedAllData()
{
    Ref protect=*this;m_network=MediaPlayer::NetworkState::Loaded;
    if(RefPtr player=m_player.get())player->networkStateChanged();wakeMSE();
}
Ref<MediaTimePromise> Player::seekMSE(const SeekTarget& target)
{
    double ms=target.time.toDouble()*1000;
    if(!std::isfinite(ms) || ms<0 || ms>double(INT64_MAX/4))return MediaTimePromise::createAndReject(PlatformMediaError::Cancelled);
    m_seekPromise.reset();m_seekPromise.emplace(PlatformMediaError::Cancelled);Ref promise=m_seekPromise->promise();
    auto generation=++m_generation;m_mseSeekingReady=false;m_hasPending=false;m_pending={};m_decodeEnded=false;
    m_position=(int64_t)ms;m_anchor=MonotonicTime::now();m_stalled=true;m_ended=false;m_stallLimit=INT64_MAX;
    m_mse->waitForTarget(target)->whenSettled(RunLoop::mainSingleton(),[weak=WeakPtr{*this},generation](auto&& result){
        auto* owner=weak.get();if(!owner || owner->m_generation!=generation || !owner->m_mse)return;
        if(!result){if(auto promise=std::exchange(owner->m_seekPromise,std::nullopt))promise->reject(result.error());return;}
        owner->m_mse->reenqueueMediaForTime(*result)->whenSettled(RunLoop::mainSingleton(),[weak,generation](auto&& requeued){
            auto* owner=weak.get();if(!owner || owner->m_generation!=generation || !owner->m_mse)return;
            if(!requeued){if(auto promise=std::exchange(owner->m_seekPromise,std::nullopt))promise->reject(PlatformMediaError::Cancelled);return;}
            owner->m_mseSeekingReady=true;owner->wakeMSE();
        });
    });
    return promise;
}
void Player::tickMSE()
{
    Ref protect=*this;Ref source=*m_mse;auto generation=m_generation;
    if(source->error()<0){fail(MediaPlayer::NetworkState::DecodeError,"Video: MSE pipeline failed");return;}
    if(!refreshMSEDuration())return;
    auto buffered=source->buffered();if(buffered!=m_buffered){m_buffered=WTF::move(buffered);m_progress=true;}
    m_ready=source->mediaPlayerReadyState();
    int64_t pos=position();
    if(!source->isEnded()){
        m_ended=false;
        if(m_network==MediaPlayer::NetworkState::Loaded){
            m_network=MediaPlayer::NetworkState::Loading;if(RefPtr player=m_player.get())player->networkStateChanged();
            if(generation!=m_generation || m_mse.get()!=source.ptr())return;
        }
    }
    if(!m_seekPromise){
        auto boundary=source->nextStallTime(MediaTime(pos,1000));
        double limit=boundary.toDouble()*1000;
        m_stallLimit=boundary.isPositiveInfinite()?INT64_MAX:(std::isfinite(limit) && limit>=pos && limit<double(INT64_MAX/4)?(int64_t)std::ceil(limit):pos);
        bool available=source->hasFutureTime(MediaTime(pos,1000),MediaTime(1,1000));
        if(m_stalled && available){m_position=pos;m_anchor=MonotonicTime::now();m_stalled=false;}
        else if(!m_stalled && !available){m_position=pos;m_anchor=MonotonicTime::now();m_stalled=true;}
    }
    if(!m_seekPromise && source->isEnded() && m_duration>=0 && pos>=m_duration){
        m_position=m_duration;m_paused=true;m_ended=true;
        if(RefPtr player=m_player.get()){player->timeChanged();player->playbackStateChanged();}return;
    }
    if(m_seekPromise && !m_mseSeekingReady)return;
    if(!m_hasPending && source->hasVideo()){
        int result=source->nextFrame(m_mseFrame);
        if(result<0){fail(MediaPlayer::NetworkState::DecodeError,"Video: MSE frame decoding failed");return;}
        if(result==DIHOS_VIDEO_FRAME){m_pending=m_mseFrame.view();m_hasPending=true;}
        else if(result==DIHOS_VIDEO_EOF)m_decodeEnded=true;
        else if((!m_image || m_seekPromise) && source->hasBufferedData())m_timer.startOneShot(1_ms);
    }
    if(m_seekPromise && !source->hasVideo()){
        if(auto promise=std::exchange(m_seekPromise,std::nullopt))promise->resolve(currentTime());
    }
    if(m_hasPending){
        int64_t end=m_pending.presentation_ms+m_pending.duration_ms;
        bool seekAtEnd=m_seekPromise && m_duration>=0 && pos==m_duration && end==pos;
        if(m_image && end<=pos && !seekAtEnd){m_hasPending=false;m_timer.startOneShot(1_ms);return;}
        if(!m_seekPromise && m_image && m_pending.presentation_ms>pos){
            if(!m_paused && m_rate>0)m_timer.startOneShot(16_ms);return;
        }
        if(m_seekPromise && m_pending.presentation_ms>pos){m_position=m_pending.presentation_ms;m_anchor=MonotonicTime::now();}
        if(!present(m_pending)){if(generation==m_generation)fail(MediaPlayer::NetworkState::DecodeError,"Video: MSE surface allocation failed");return;}
        if(generation!=m_generation || m_mse.get()!=source.ptr())return;
        m_hasPending=false;
    }
    if(!m_paused && m_rate>0){
        if(RefPtr player=m_player.get())player->timeChanged();
        if(generation==m_generation && m_mse.get()==source.ptr())m_timer.startOneShot(16_ms);
    }
}
#endif
class Factory final : public MediaPlayerFactory {
    WTF_MAKE_TZONE_ALLOCATED(Factory);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(Factory);
public:
    MediaPlayerEnums::MediaEngineIdentifier identifier() const final { return MediaPlayerEnums::MediaEngineIdentifier::DihOS; }
    Ref<MediaPlayerPrivateInterface> createMediaEnginePlayer(MediaPlayer& player) const final { return adoptRef(*new Player(player)); }
    void getSupportedTypes(HashSet<String>& types) const final { types.add("video/mp4"_s);
#if ENABLE(MEDIA_SOURCE)
        types.add("audio/mp4"_s);
#endif
    }
    MediaPlayer::SupportsType supportsTypeAndCodecs(const MediaEngineSupportParameters& parameters) const final { return supports(parameters); }
};
WTF_MAKE_TZONE_ALLOCATED_IMPL(Factory);
class Strategy final : public MediaStrategy {
public:
    bool canDecodeExtendedType(PlatformMediaDecodingType platformType,const ContentType& type) final {
        MediaEngineSupportParameters parameters;parameters.platformType=platformType;parameters.type=type;
        return supports(parameters)==MediaPlayer::SupportsType::IsSupported;
    }
};
}
void registerDihOSMediaEngine(MediaEngineRegistrar registrar) { registrar(makeUnique<Factory>()); }
MediaStrategy* createDihOSMediaStrategy() { static NeverDestroyed<Strategy> strategy;return &strategy.get(); }
void setDihOSMediaTestTracing(bool enabled) { testTracing=enabled;testTraceMessages=0; }
}
