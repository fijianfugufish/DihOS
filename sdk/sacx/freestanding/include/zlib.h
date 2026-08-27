#pragma once
#include <stddef.h>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef void *voidpf;
typedef struct z_stream_s { Bytef *next_in; uInt avail_in; Bytef *next_out; uInt avail_out; voidpf zalloc,zfree,opaque; } z_stream;
typedef void *gzFile;
#define Z_NULL 0
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_DATA_ERROR (-3)
#define Z_NO_FLUSH 0
#define MAX_WBITS 15
#ifdef __cplusplus
extern "C" {
#endif
int inflateInit2_(z_stream *stream,int window_bits,const char *version,int stream_size);
#define inflateInit2(stream,bits) inflateInit2_((stream),(bits),"1.2.11",sizeof(z_stream))
int inflate(z_stream *stream,int flush);
int inflateEnd(z_stream *stream);
gzFile gzopen(const char *path,const char *mode);
char *gzgets(gzFile file,char *buffer,int capacity);
int gzclose(gzFile file);
#ifdef __cplusplus
}
#endif
