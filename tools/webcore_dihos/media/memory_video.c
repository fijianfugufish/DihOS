#include "memory_video.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

struct dihos_video {
    uint8_t *input,*pixels;
    size_t input_size,position,pixel_bytes;
    AVIOContext* io;
    AVFormatContext* format;
    AVCodecContext* codec;
    AVPacket* packet;
    AVFrame* frame;
    struct SwsContext* scale;
    int stream,eof,flushed,pending;
    unsigned packets;
    int64_t duration_ms,next_ms,seek_target_ms;
};
static int read_memory(void* opaque,uint8_t* output,int size)
{
    dihos_video* v=opaque;
    if(size<=0)return AVERROR(EINVAL);
    size_t available=v->input_size-v->position;
    if(!available)return AVERROR_EOF;
    if((size_t)size>available)size=(int)available;
    memcpy(output,v->input+v->position,(size_t)size);v->position+=(size_t)size;
    return size;
}
static int64_t seek_memory(void* opaque,int64_t offset,int whence)
{
    dihos_video* v=opaque;
    if(whence==AVSEEK_SIZE)return (int64_t)v->input_size;
    whence &= ~AVSEEK_FORCE;
    int64_t base;
    if(whence==SEEK_SET)base=0;
    else if(whence==SEEK_CUR)base=(int64_t)v->position;
    else if(whence==SEEK_END)base=(int64_t)v->input_size;
    else return AVERROR(EINVAL);
    if(offset < -base || offset > (int64_t)v->input_size-base)return AVERROR(EINVAL);
    v->position=(size_t)(base+offset);return (int64_t)v->position;
}
static int deny_nested_io(AVFormatContext* context,AVIOContext** out,
                          const char* url,int flags,AVDictionary** options)
{
    (void)context;(void)url;(void)flags;(void)options;
    *out=NULL;return AVERROR(EACCES);
}
void dihos_video_close(dihos_video* v)
{
    if(!v)return;
    sws_freeContext(v->scale);
    av_frame_free(&v->frame);av_packet_free(&v->packet);
    avcodec_free_context(&v->codec);avformat_close_input(&v->format);
    if(v->io){av_freep(&v->io->buffer);avio_context_free(&v->io);}
    av_free(v->pixels);av_free(v->input);av_free(v);
}
int dihos_video_open(const uint8_t* input,size_t size,dihos_video** out)
{
    if(!out)return AVERROR(EINVAL);
    *out=NULL;
    if(!input || !size)return AVERROR(EINVAL);
    if(size>DIHOS_VIDEO_INPUT_MAX)return AVERROR(E2BIG);
    /* Global decoder policy: serialized backend, bounded individual buffers,
     * no decoder threads, no process-wide file or network initialization. */
    av_max_alloc(16*1024*1024-65536);
    dihos_video* v=av_mallocz(sizeof(*v));
    if(!v)return AVERROR(ENOMEM);
    int result=AVERROR(ENOMEM);
    v->input=av_memdup(input,size);v->input_size=size;
    if(!v->input)goto fail;
    uint8_t* buffer=av_malloc(16384);
    if(!buffer)goto fail;
    v->io=avio_alloc_context(buffer,16384,0,v,read_memory,NULL,seek_memory);
    if(!v->io){av_free(buffer);goto fail;}
    v->format=avformat_alloc_context();
    if(!v->format)goto fail;
    v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    v->format->probesize=65536;v->format->max_analyze_duration=0;
    v->format->max_probe_packets=32;v->format->max_streams=8;
    v->format->io_open=deny_nested_io;
    result=avformat_open_input(&v->format,NULL,NULL,NULL);
    if(result<0)goto fail;
    v->stream=av_find_best_stream(v->format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    if(v->stream<0){result=v->stream;goto fail;}
    AVStream* stream=v->format->streams[v->stream];
    AVCodecParameters* parameters=stream->codecpar;
    if(parameters->width<=0 || parameters->height<=0 ||
       parameters->width>DIHOS_VIDEO_WIDTH_MAX || parameters->height>DIHOS_VIDEO_HEIGHT_MAX){
        result=AVERROR(E2BIG);goto fail;
    }
    const AVCodec* decoder=avcodec_find_decoder(parameters->codec_id);
    if(!decoder){result=AVERROR(ENOTSUP);goto fail;}
    v->codec=avcodec_alloc_context3(decoder);
    if(!v->codec){result=AVERROR(ENOMEM);goto fail;}
    result=avcodec_parameters_to_context(v->codec,parameters);
    if(result<0)goto fail;
    v->codec->thread_count=1;v->codec->thread_type=0;v->codec->pkt_timebase=stream->time_base;
    /* H.264 codes 1080-line pictures as 1088 rows with an SPS crop. */
    v->codec->max_pixels=DIHOS_VIDEO_WIDTH_MAX*((DIHOS_VIDEO_HEIGHT_MAX+15)&~15);
    result=avcodec_open2(v->codec,decoder,NULL);
    if(result<0)goto fail;
    v->frame=av_frame_alloc();v->packet=av_packet_alloc();
    if(!v->frame || !v->packet){result=AVERROR(ENOMEM);goto fail;}
    v->seek_target_ms=-1;
    v->duration_ms=v->format->duration==AV_NOPTS_VALUE ? -1 : v->format->duration/1000;
    if(v->duration_ms<0 && stream->duration!=AV_NOPTS_VALUE)
        v->duration_ms=av_rescale_q(stream->duration,stream->time_base,(AVRational){1,1000});
    *out=v;return 0;
fail:
    dihos_video_close(v);return result;
}
int64_t dihos_video_duration_ms(const dihos_video* v)
{ return v?v->duration_ms:-1; }
int dihos_video_seek_ms(dihos_video* v,int64_t ms)
{
    if(!v || ms<0 || ms>INT64_MAX/1000 || (v->duration_ms>=0 && ms>v->duration_ms))return AVERROR(EINVAL);
    AVStream* stream=v->format->streams[v->stream];
    int64_t timestamp=av_rescale_q(ms,(AVRational){1,1000},stream->time_base);
    int result=av_seek_frame(v->format,v->stream,timestamp,AVSEEK_FLAG_BACKWARD);
    if(result<0)return result;
    avcodec_flush_buffers(v->codec);av_packet_unref(v->packet);av_frame_unref(v->frame);
    v->eof=0;v->flushed=0;v->pending=0;v->packets=0;v->next_ms=ms;v->seek_target_ms=ms;
    return 0;
}
static int publish_frame(dihos_video* v,dihos_video_frame* out)
{
    int width=v->frame->width,height=v->frame->height;
    if(width<=0 || height<=0 || width>DIHOS_VIDEO_WIDTH_MAX || height>DIHOS_VIDEO_HEIGHT_MAX)
        return AVERROR(E2BIG);
    size_t bytes=(size_t)width*(size_t)height*4;
    if(bytes!=v->pixel_bytes){
        uint8_t* pixels=av_malloc(bytes);
        if(!pixels)return AVERROR(ENOMEM);
        av_free(v->pixels);v->pixels=pixels;v->pixel_bytes=bytes;
    }
    v->scale=sws_getCachedContext(v->scale,width,height,v->frame->format,width,height,
                                  AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!v->scale)return AVERROR(ENOMEM);
    uint8_t* planes[4]={v->pixels,NULL,NULL,NULL};int strides[4]={width*4,0,0,0};
    int rows=sws_scale(v->scale,(const uint8_t* const*)v->frame->data,
                       v->frame->linesize,0,height,planes,strides);
    if(rows!=height)return AVERROR(EIO);
    AVRational time_base=v->format->streams[v->stream]->time_base;
    int64_t pts=v->frame->best_effort_timestamp;
    int64_t ms=pts==AV_NOPTS_VALUE?v->next_ms:av_rescale_q(pts,time_base,(AVRational){1,1000});
    int64_t duration=v->frame->duration>0?av_rescale_q(v->frame->duration,time_base,(AVRational){1,1000}):0;
    if(duration<=0)duration=40;
    if(ms<0)ms=0;
    if(ms>INT64_MAX-duration)return AVERROR(EOVERFLOW);
    v->next_ms=ms+duration;
    *out=(dihos_video_frame){v->pixels,(uint32_t)width,(uint32_t)height,(uint32_t)width*4,ms,duration};
    av_frame_unref(v->frame);return DIHOS_VIDEO_FRAME;
}
int dihos_video_next(dihos_video* v,dihos_video_frame* out)
{
    if(!v || !out)return AVERROR(EINVAL);
    memset(out,0,sizeof(*out));
    for(unsigned work=0;work<16;++work){
        int result=avcodec_receive_frame(v->codec,v->frame);
        if(result==0){
            if(v->seek_target_ms>=0 && v->frame->best_effort_timestamp!=AV_NOPTS_VALUE){
                int64_t ms=av_rescale_q(v->frame->best_effort_timestamp,
                    v->format->streams[v->stream]->time_base,(AVRational){1,1000});
                if(ms<v->seek_target_ms){av_frame_unref(v->frame);continue;}
            }
            v->seek_target_ms=-1;return publish_frame(v,out);
        }
        if(result==AVERROR_EOF)return DIHOS_VIDEO_EOF;
        if(result!=AVERROR(EAGAIN))return result;
        if(v->eof){
            if(v->flushed)return AVERROR(EIO);
            result=avcodec_send_packet(v->codec,NULL);
            if(result<0 && result!=AVERROR_EOF)return result;
            v->flushed=1;continue;
        }
        if(!v->pending){
            result=av_read_frame(v->format,v->packet);
            if(result==AVERROR_EOF){v->eof=1;continue;}
            if(result<0)return result;
            if(++v->packets>200000){av_packet_unref(v->packet);return AVERROR(E2BIG);}
            if(v->packet->stream_index!=v->stream){av_packet_unref(v->packet);continue;}
            v->pending=1;
        }
        result=avcodec_send_packet(v->codec,v->packet);
        if(result==AVERROR(EAGAIN))continue;
        av_packet_unref(v->packet);v->pending=0;
        if(result<0)return result;
    }
    return DIHOS_VIDEO_AGAIN;
}
