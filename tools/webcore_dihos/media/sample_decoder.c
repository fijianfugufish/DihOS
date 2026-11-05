#include "sample_decoder.h"
#include <errno.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
struct dihos_sample_decoder {
    AVCodecContext* codec;
    AVPacket* packet;
    AVFrame* frame;
    struct SwsContext* scale;
    uint8_t* pixels;
    size_t pixel_bytes;
    uint64_t track;
    int draining;
};
void dihos_sample_decoder_close(dihos_sample_decoder* d)
{
    if(!d)return;
    sws_freeContext(d->scale);av_free(d->pixels);av_packet_free(&d->packet);av_frame_free(&d->frame);
    avcodec_free_context(&d->codec);av_free(d);
}
int dihos_sample_decoder_create(const dihos_encoded_track* t,dihos_sample_decoder** out)
{
    if(!out)return AVERROR(EINVAL);
    *out=NULL;if(!t)return AVERROR(EINVAL);
    if(t->codec!=DIHOS_ENCODED_H264)return AVERROR(ENOTSUP);
    if(!t->id || !t->configuration || !t->configuration_size || t->configuration_size>DIHOS_ENCODED_CONFIG_MAX ||
       !t->width || !t->height || t->width>DIHOS_VIDEO_WIDTH_MAX || t->height>DIHOS_VIDEO_HEIGHT_MAX)return AVERROR(EINVAL);
    const AVCodec* codec=avcodec_find_decoder(AV_CODEC_ID_H264);if(!codec)return AVERROR(ENOTSUP);
    dihos_sample_decoder* d=av_mallocz(sizeof(*d));if(!d)return AVERROR(ENOMEM);
    int result=AVERROR(ENOMEM);
    d->codec=avcodec_alloc_context3(codec);if(!d->codec)goto fail;
    d->codec->width=t->width;d->codec->height=t->height;
    d->codec->max_pixels=DIHOS_VIDEO_WIDTH_MAX*DIHOS_VIDEO_HEIGHT_MAX;
    d->codec->thread_count=1;d->codec->thread_type=0;
    d->codec->pkt_timebase=(AVRational){1,1000000};
    d->codec->extradata=av_mallocz(t->configuration_size+AV_INPUT_BUFFER_PADDING_SIZE);
    if(!d->codec->extradata)goto fail;
    memcpy(d->codec->extradata,t->configuration,t->configuration_size);d->codec->extradata_size=(int)t->configuration_size;
    result=avcodec_open2(d->codec,codec,NULL);if(result<0)goto fail;
    d->packet=av_packet_alloc();d->frame=av_frame_alloc();
    if(!d->packet || !d->frame){result=AVERROR(ENOMEM);goto fail;}
    d->track=t->id;*out=d;return 0;
fail:dihos_sample_decoder_close(d);return result;
}
void dihos_sample_decoder_flush(dihos_sample_decoder* d)
{
    if(!d)return;
    avcodec_flush_buffers(d->codec);av_frame_unref(d->frame);av_packet_unref(d->packet);d->draining=0;
}
int dihos_sample_decoder_send(dihos_sample_decoder* d,const dihos_encoded_sample* sample)
{
    if(!d)return AVERROR(EINVAL);
    if(!sample){
        if(d->draining)return 0;
        int result=avcodec_send_packet(d->codec,NULL);
        if(result==AVERROR(EAGAIN))return DIHOS_VIDEO_AGAIN;
        if(result<0 && result!=AVERROR_EOF)return result;
        d->draining=1;return 0;
    }
    if(d->draining || sample->track_id!=d->track || !sample->bytes || !sample->size ||
       sample->size>DIHOS_ENCODED_PACKET_MAX || sample->time_num<=0 || sample->time_den<=0 || sample->duration<=0)return AVERROR(EINVAL);
    AVRational input={sample->time_num,sample->time_den};
    int64_t pts=av_rescale_q(sample->presentation,input,d->codec->pkt_timebase);
    int64_t dts=av_rescale_q(sample->decode,input,d->codec->pkt_timebase);
    int64_t duration=av_rescale_q(sample->duration,input,d->codec->pkt_timebase);
    if(pts==AV_NOPTS_VALUE || dts==AV_NOPTS_VALUE || duration<=0)return AVERROR(EOVERFLOW);
    av_packet_unref(d->packet);
    int result=av_new_packet(d->packet,(int)sample->size);if(result<0)return result;
    memcpy(d->packet->data,sample->bytes,sample->size);d->packet->pts=pts;d->packet->dts=dts;d->packet->duration=duration;
    if(sample->sync)d->packet->flags|=AV_PKT_FLAG_KEY;
    if(sample->non_displaying)d->packet->flags|=AV_PKT_FLAG_DISCARD;
    result=avcodec_send_packet(d->codec,d->packet);av_packet_unref(d->packet);
    if(result==AVERROR(EAGAIN))return DIHOS_VIDEO_AGAIN;
    return result;
}
int dihos_sample_decoder_receive(dihos_sample_decoder* d,dihos_video_frame* out)
{
    if(out)memset(out,0,sizeof(*out));if(!d || !out)return AVERROR(EINVAL);
    av_frame_unref(d->frame);
    int result=avcodec_receive_frame(d->codec,d->frame);
    if(result==AVERROR(EAGAIN))return DIHOS_VIDEO_AGAIN;
    if(result==AVERROR_EOF)return DIHOS_VIDEO_EOF;
    if(result<0)return result;
    if(d->frame->flags&AV_FRAME_FLAG_DISCARD){av_frame_unref(d->frame);return DIHOS_VIDEO_AGAIN;}
    int width=d->frame->width,height=d->frame->height;
    if(width<=0 || height<=0 || width>DIHOS_VIDEO_WIDTH_MAX || height>DIHOS_VIDEO_HEIGHT_MAX)return AVERROR(E2BIG);
    size_t size=(size_t)width*(size_t)height*4;
    if(size!=d->pixel_bytes){
        uint8_t* pixels=av_malloc(size);if(!pixels)return AVERROR(ENOMEM);
        av_free(d->pixels);d->pixels=pixels;d->pixel_bytes=size;
    }
    d->scale=sws_getCachedContext(d->scale,width,height,d->frame->format,width,height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!d->scale)return AVERROR(ENOMEM);
    uint8_t* planes[4]={d->pixels,NULL,NULL,NULL};int strides[4]={width*4,0,0,0};
    if(sws_scale(d->scale,(const uint8_t* const*)d->frame->data,d->frame->linesize,0,height,planes,strides)!=height)return AVERROR(EIO);
    if(d->frame->best_effort_timestamp==AV_NOPTS_VALUE || d->frame->duration<=0)return AVERROR(EINVAL);
    int64_t pts=av_rescale_q(d->frame->best_effort_timestamp,d->codec->pkt_timebase,(AVRational){1,1000});
    int64_t duration=av_rescale_q(d->frame->duration,d->codec->pkt_timebase,(AVRational){1,1000});
    if(duration<=0)return AVERROR(EINVAL);
    *out=(dihos_video_frame){d->pixels,(uint32_t)width,(uint32_t)height,(uint32_t)width*4,pts,duration};return DIHOS_VIDEO_FRAME;
}
