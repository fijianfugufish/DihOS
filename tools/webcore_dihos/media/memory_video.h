#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A single owned decoder thread must serialize ALL calls to this backend.
 * The FFmpeg profile has no threading; callbacks must copy a published frame
 * before advancing the decoder. There are no kernel addresses or file APIs. */
typedef struct dihos_video dihos_video;
typedef struct dihos_video_frame {
    const uint8_t* bgra;
    uint32_t width, height, stride;
    int64_t presentation_ms;
    int64_t duration_ms;
} dihos_video_frame;
enum { DIHOS_VIDEO_EOF=0, DIHOS_VIDEO_FRAME=1, DIHOS_VIDEO_AGAIN=2 };
enum { DIHOS_VIDEO_INPUT_MAX=8*1024*1024, DIHOS_VIDEO_WIDTH_MAX=1920,
       DIHOS_VIDEO_HEIGHT_MAX=1080 };
/* Copies input; callers can release it as soon as this returns. Returns a
 * negative errno on error and always leaves *out null on failure. */
int dihos_video_open(const uint8_t* data,size_t size,dihos_video** out);
/* Advances at most 16 packets. All decoding runs in preemptible EL0. */
int dihos_video_next(dihos_video*,dihos_video_frame* out);
int64_t dihos_video_duration_ms(const dihos_video*);
int dihos_video_seek_ms(dihos_video*,int64_t position_ms);
/* Must be serialized with next/open. Closing releases every owned buffer. */
void dihos_video_close(dihos_video*);
#ifdef __cplusplus
}
#endif
