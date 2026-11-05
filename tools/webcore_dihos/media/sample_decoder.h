#pragma once
#include "encoded_media.h"
#include "memory_video.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dihos_sample_decoder dihos_sample_decoder;
/* Copies codec configuration; demux/SourceBuffer storage may be released.
 * One owner serializes all calls. No network, files or decoder threads. */
int dihos_sample_decoder_create(const dihos_encoded_track*,dihos_sample_decoder**);
/* 0 = accepted; DIHOS_VIDEO_AGAIN = receive frames then retry this SAME sample.
 * Null sample drains at the real end of stream, never at fragment boundaries. */
int dihos_sample_decoder_send(dihos_sample_decoder*,const dihos_encoded_sample*);
/* Frame view expires on receive/flush/close. Uses the existing owned BGRA ABI. */
int dihos_sample_decoder_receive(dihos_sample_decoder*,dihos_video_frame*);
void dihos_sample_decoder_flush(dihos_sample_decoder*);
void dihos_sample_decoder_close(dihos_sample_decoder*);
#ifdef __cplusplus
}
#endif
