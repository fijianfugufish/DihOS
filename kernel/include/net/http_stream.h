#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single-owner incremental HTTP/1 response framing. Body offsets never move. */
typedef struct http_stream {
    uint8_t *body;
    uint32_t capacity,available,expected,http_status;
    uint32_t header_bytes,line_bytes,remaining,state;
    uint8_t headers_ready,complete,failed,truncated,has_length,chunked;
    char content_type[96],location[768],error[96];
    char headers[16384],line[256];
} http_stream;
void http_stream_init(http_stream*,uint8_t*,uint32_t);
int http_stream_feed(http_stream*,const uint8_t*,uint32_t);
int http_stream_finish(http_stream*);
#ifdef __cplusplus
}
#endif
