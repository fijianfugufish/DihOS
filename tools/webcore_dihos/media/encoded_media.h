#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owned, memory-only demuxing for WebCore's encoded sample/track buffers.
 * Each object has one owner; returned views expire at next/close. Rational
 * timestamps are preserved, rather than rounded to presentation milliseconds. */
typedef struct dihos_encoded_media dihos_encoded_media;
enum { DIHOS_ENCODED_H264=1, DIHOS_ENCODED_AAC=2 };
enum { DIHOS_ENCODED_TRACK_MAX=8, DIHOS_ENCODED_CONFIG_MAX=65536,
       DIHOS_ENCODED_PACKET_MAX=1024*1024 };
typedef struct dihos_encoded_track {
    uint64_t id;
    uint32_t codec,width,height,sample_rate,channels;
    int32_t time_num,time_den;
    const uint8_t* configuration;
    size_t configuration_size;
} dihos_encoded_track;
typedef struct dihos_encoded_sample {
    uint64_t track_id;
    const uint8_t* bytes;
    size_t size;
    int64_t presentation,decode,duration;
    int32_t time_num,time_den;
    unsigned sync;
    unsigned non_displaying;
} dihos_encoded_sample;
/* MSE-style initialization (empty sample tables), optionally followed by ONE
 * media fragment. Ordinary complete MP4 files use memory_video.h instead. */
int dihos_encoded_open(const uint8_t*,size_t,dihos_encoded_media**);
unsigned dihos_encoded_track_count(const dihos_encoded_media*);
int dihos_encoded_track_at(const dihos_encoded_media*,unsigned,dihos_encoded_track*);
/* 1 = sample, 0 = drained initialization/fragment, negative = error. */
int dihos_encoded_next(dihos_encoded_media*,dihos_encoded_sample*);
void dihos_encoded_close(dihos_encoded_media*);
#ifdef __cplusplus
}
#endif
