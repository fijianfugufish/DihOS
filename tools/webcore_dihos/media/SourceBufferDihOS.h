#pragma once
#if ENABLE(MEDIA_SOURCE)
#include "MediaDescription.h"
#include "MediaSample.h"
#include "SourceBufferPrivate.h"
#include "SharedBuffer.h"
#include "encoded_media.h"
#include "fragmented_mp4.h"
#include <wtf/ThreadSafeRefCounted.h>
#include <atomic>

namespace WebCore {
bool supportsDihOSMediaSourceType(const ContentType&);
class DihOSMediaDescription final : public MediaDescription {
public:
    static RefPtr<DihOSMediaDescription> create(const dihos_encoded_track&);
    bool isVideo() const final { return codecID==DIHOS_ENCODED_H264; }
    bool isAudio() const final { return codecID==DIHOS_ENCODED_AAC; }
    bool isText() const final { return false; }
    const TrackID identifier;
    const unsigned codecID,width,height,sampleRate,channels;
    const Ref<SharedBuffer> configuration;
private:
    DihOSMediaDescription(const dihos_encoded_track&,String&&,Ref<SharedBuffer>&&);
};
class DihOSMediaSample final : public MediaSample {
public:
    static RefPtr<DihOSMediaSample> create(const dihos_encoded_sample&,Ref<DihOSMediaDescription>&&);
    MediaTime presentationTime() const final { return m_pts; }
    MediaTime decodeTime() const final { return m_dts; }
    MediaTime duration() const final { return m_duration; }
    TrackID trackID() const final { return description->identifier; }
    size_t sizeInBytes() const final { return bytes->size(); }
    FloatSize presentationSize() const final { return { float(description->width),float(description->height) }; }
    void offsetTimestampsBy(const MediaTime& offset) final { m_pts+=offset;m_dts+=offset; }
    void setTimestamps(const MediaTime& pts,const MediaTime& dts) final { m_pts=pts;m_dts=dts; }
    SampleFlags flags() const final { return m_flags; }
    Type type() const final { return Type::DihOS; }
    PlatformSample platformSample() const final { return PlatformSample { static_cast<const MockSampleBox*>(nullptr) }; }
    Ref<MediaSample> createNonDisplayingCopy() const final;
    Ref<MediaSample> createCopyWithAdjustedStartTime(const MediaTime&) const final;
    const Ref<SharedBuffer> bytes;
    const Ref<DihOSMediaDescription> description;
private:
    DihOSMediaSample(Ref<SharedBuffer>&&,Ref<DihOSMediaDescription>&&,MediaTime,MediaTime,MediaTime,SampleFlags);
    MediaTime m_pts,m_dts,m_duration;
    SampleFlags m_flags;
};
/* Implemented by the persistent player decoder queue. All calls occur on the
 * SourceBuffer dispatcher; implementations must marshal to their own owner.
 * Backpressure is explicit, and readiness callbacks must be delivered once. */
class DihOSSampleConsumer : public ThreadSafeRefCounted<DihOSSampleConsumer> {
public:
    virtual ~DihOSSampleConsumer()=default;
    virtual bool configure(Ref<DihOSMediaDescription>&&) { return true; }
    virtual bool ready(TrackID) const=0;
    virtual void enqueue(Ref<DihOSMediaSample>&&,TrackID)=0;
    virtual void flush(TrackID)=0;
    virtual void whenReady(TrackID,Function<void()>&&)=0;
    virtual void allEnqueued(TrackID) { }
};
class SourceBufferDihOS final : public SourceBufferPrivate {
public:
    static Ref<SourceBufferDihOS> create(MediaSourcePrivate&,Ref<DihOSSampleConsumer>&&,bool videoContainer=true);
    ~SourceBufferDihOS() final;
    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::DihOS; }
    size_t platformMaximumBufferSize() const final { return 32*1024*1024; }
    void removedFromMediaSource() final;
    bool canSwitchToType(const ContentType&) final;
    void startChangingType() final;
    bool endInput() { return !m_promise && dihos_fmp4_finish(m_parser)==DIHOS_FMP4_MORE; }
private:
    SourceBufferDihOS(MediaSourcePrivate&,Ref<DihOSSampleConsumer>&&,bool);
    Ref<MediaPromise> appendInternal(Ref<SharedBuffer>&&) final;
    void resetParserStateInternal() final;
    void flush(TrackID id) final { m_consumer->flush(id); }
    void enqueueSample(Ref<MediaSample>&&,TrackID) final;
    bool isReadyForMoreSamples(TrackID id) final { return !m_removed && m_consumer->ready(id); }
    void notifyClientWhenReadyForMoreSamples(TrackID) final;
    void allSamplesInTrackEnqueued(TrackID id) final { m_consumer->allEnqueued(id); }
    bool initializeTracks();
    void pump(uint64_t);
    void settle(std::optional<PlatformMediaError>);
    Ref<DihOSSampleConsumer> m_consumer;
    const bool m_videoContainer;
    dihos_fmp4* m_parser{};
    dihos_encoded_media* m_demux{};
    RefPtr<SharedBuffer> m_append;
    size_t m_offset{};
    uint64_t m_generation{};
    uint64_t m_initializationVersion{};
    bool m_announced{};
    std::atomic<bool> m_removed{false};
    Vector<Ref<DihOSMediaDescription>> m_descriptions;
    std::optional<MediaPromise::AutoRejectProducer> m_promise;
};
}
#endif
