#pragma once
#if ENABLE(MEDIA_SOURCE)
#include "MediaSourcePrivate.h"
#include "MediaSourcePrivateClient.h"
#include "MediaPlayerPrivate.h"
#include "SourceBufferDihOS.h"
#include "memory_video.h"

namespace WebCore {
// Owns its pixels across asynchronous flush/removal/replacement callbacks.
struct DihOSMSEFrame {
    Vector<uint8_t> pixels;
    uint32_t width{},height{},stride{};
    int64_t presentation{},duration{};
    dihos_video_frame view() const { return {pixels.span().data(),width,height,stride,presentation,duration}; }
};
class DihOSDecoderConsumer;
class MediaSourceDihOS final : public MediaSourcePrivate {
public:
    using PlayerAccess=Function<RefPtr<MediaPlayerPrivateInterface>()>;
    static Ref<MediaSourceDihOS> create(MediaSourcePrivateClient&,PlayerAccess&&,Function<void()>&&);
    ~MediaSourceDihOS() final;
    constexpr MediaPlatformType platformType() const final { return MediaPlatformType::DihOS; }
    RefPtr<MediaPlayerPrivateInterface> player() const final;
    void setPlayer(MediaPlayerPrivateInterface*) final;
    void attach(PlayerAccess&&,Function<void()>&&);
    void detach();
    AddStatus addSourceBuffer(const ContentType&,const MediaSourceConfiguration&,RefPtr<SourceBufferPrivate>&) final;
    void removeSourceBuffer(SourceBufferPrivate&) final;
    void notifyActiveSourceBuffersChanged() final;
    void durationChanged(const MediaTime&) final;
    void bufferedChanged(PlatformTimeRanges&&) final;
    void markEndOfStream(EndOfStreamStatus) final;
    void unmarkEndOfStream() final;
    bool supportsTracksTypeChanged() const final { return true; }
    int nextFrame(DihOSMSEFrame&);
    FloatSize naturalSize() const;
    int error() const { return m_error; }
    void wake();
private:
    MediaSourceDihOS(MediaSourcePrivateClient&,PlayerAccess&&,Function<void()>&&);
    struct Entry {Ref<SourceBufferDihOS> buffer;Ref<DihOSDecoderConsumer> consumer;bool video;};
    Vector<Entry> m_entries;
    PlayerAccess m_playerAccess;
    Function<void()> m_wake;
    int m_error{};
    uint64_t m_attachmentGeneration{1};
    bool m_attached{true};
};
}
SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::MediaSourceDihOS)
static bool isType(const WebCore::MediaSourcePrivate& source){return source.platformType()==WebCore::MediaPlatformType::DihOS;}
SPECIALIZE_TYPE_TRAITS_END()
#endif
