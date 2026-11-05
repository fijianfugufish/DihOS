#include "encoded_media.h"
#include "memory_video.h"
#include <errno.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavutil/mem.h>
#include <libavutil/channel_layout.h>
struct dihos_encoded_media {
    uint8_t* data;
    size_t size,position;
    AVIOContext* io;
    AVFormatContext* format;
    AVPacket* packet;
    unsigned packets;
    int failed;
    uint32_t composition_shift[DIHOS_ENCODED_TRACK_MAX];
};
static uint32_t be32(const uint8_t* p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
typedef struct iso_box { const uint8_t* bytes;size_t size;uint32_t type; } iso_box;
static int next_box(const uint8_t* data,size_t length,size_t* position,iso_box* out)
{
    if(*position==length)return 0;
    if(length-*position<8)return AVERROR(EINVAL);
    const uint8_t* p=data+*position;uint64_t size=be32(p);size_t header=8;
    if(size==1){if(length-*position<16)return AVERROR(EINVAL);size=((uint64_t)be32(p+8)<<32)|be32(p+12);header=16;}
    if(size<header || size>length-*position)return AVERROR(EINVAL);
    *out=(iso_box){p+header,(size_t)size-header,be32(p+4)};*position+=(size_t)size;return 1;
}
static int validate_initialization(const uint8_t* data,size_t size,unsigned depth)
{
    if(depth>8)return AVERROR(EINVAL);
    size_t pos=0;iso_box b;int status;
    while((status=next_box(data,size,&pos,&b))>0){
        switch(b.type){
        case 0x6d6f6f76:case 0x7472616b:case 0x6d646961:case 0x6d696e66:case 0x7374626c:
            if(b.size>1024*1024 || validate_initialization(b.bytes,b.size,depth+1)<0)return AVERROR(ENOTSUP);
            break;
        case 0x73747473:case 0x73747363:case 0x7374636f:case 0x636f3634:case 0x63747473:case 0x73747373:
            // MSE initialization has no samples. In particular an existing
            // ctts table must not introduce another MOV normalization shift.
            if(b.size<8 || be32(b.bytes+4))return AVERROR(ENOTSUP);
            break;
        case 0x7374737a:case 0x73747a32:
            if(b.size<12 || be32(b.bytes+8))return AVERROR(ENOTSUP);
            break;
        case 0x636d6f76:return AVERROR(ENOTSUP); // compressed movie headers
        }
    }
    return status;
}
/* FFmpeg's MOV demuxer adds a stream-wide dts_shift to PTS when a trun
 * contains negative composition offsets. MSE needs the original signed CTS.
 * Derive that adjustment from bounded trun headers; never use FFmpeg private
 * structs, infer it from the first PTS, or round it to milliseconds. */
static int scan_traf(dihos_encoded_media* m,const iso_box* traf)
{
    size_t pos=0;iso_box child;uint32_t track=0;int status;
    while((status=next_box(traf->bytes,traf->size,&pos,&child))>0){
        if(child.type==0x74666864){if(track || child.size<8)return AVERROR(EINVAL);track=be32(child.bytes+4);}
    }
    if(status<0 || !track)return AVERROR(EINVAL);
    unsigned index=0;while(index<m->format->nb_streams && m->format->streams[index]->id!=(int)track)++index;
    if(index==m->format->nb_streams)return AVERROR(EINVAL);
    pos=0;
    while((status=next_box(traf->bytes,traf->size,&pos,&child))>0){
        if(child.type!=0x7472756e)continue; /* trun */
        if(child.size<8)return AVERROR(EINVAL);
        uint32_t flags=be32(child.bytes)&0xffffff,count=be32(child.bytes+4);unsigned version=child.bytes[0];
        if(version>1 || count>200000 || (flags&~0xf05u))return AVERROR(EINVAL);
        size_t offset=8+((flags&1)?4:0)+((flags&4)?4:0);
        unsigned fields=!!(flags&0x100)+!!(flags&0x200)+!!(flags&0x400)+!!(flags&0x800);
        if(offset>child.size || (fields && count>(child.size-offset)/(fields*4)))return AVERROR(EINVAL);
        if(!(flags&0x800))continue;
        unsigned prefix=!!(flags&0x100)*4+!!(flags&0x200)*4+!!(flags&0x400)*4;
        for(unsigned n=0;n<count;++n){
            uint32_t raw=be32(child.bytes+offset+n*fields*4+prefix);
            if(!version && raw>INT32_MAX)return AVERROR(ENOTSUP);
            int64_t cts=version?(int64_t)(int32_t)raw:(int64_t)raw;
            if(cts==INT32_MIN)return AVERROR(ENOTSUP);
            if(cts<0 && (uint32_t)-cts>m->composition_shift[index])m->composition_shift[index]=(uint32_t)-cts;
        }
    }
    return status;
}
static int scan_composition_shift(dihos_encoded_media* m)
{
    size_t pos=0;iso_box box;int result;unsigned fragments=0;
    while((result=next_box(m->data,m->size,&pos,&box))>0){
        if(box.type!=0x6d6f6f66)continue; /* moof */
        if(++fragments>1)return AVERROR(ENOTSUP); // One assembler output at a time.
        size_t inner=0;iso_box child;int status;
        while((status=next_box(box.bytes,box.size,&inner,&child))>0){
            if(child.type==0x74726166 && scan_traf(m,&child)<0)return AVERROR(EINVAL);
        }
        if(status<0)return status;
    }
    return result;
}
static int read_memory(void* context,uint8_t* bytes,int size)
{
    dihos_encoded_media* m=context;
    if(size<=0)return AVERROR(EINVAL);
    size_t available=m->size-m->position;
    if(!available)return AVERROR_EOF;
    if((size_t)size>available)size=(int)available;
    memcpy(bytes,m->data+m->position,(size_t)size);m->position+=(size_t)size;return size;
}
static int64_t seek_memory(void* context,int64_t offset,int whence)
{
    dihos_encoded_media* m=context;
    if(whence==AVSEEK_SIZE)return m->size;
    whence&=~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)m->position:whence==SEEK_END?(int64_t)m->size:-1;
    if(base<0 || offset < -base || offset>(int64_t)m->size-base)return AVERROR(EINVAL);
    m->position=(size_t)(base+offset);return m->position;
}
static int deny_nested(AVFormatContext* f,AVIOContext** out,const char* url,int flags,AVDictionary** options)
{
    (void)f;(void)url;(void)flags;(void)options;*out=NULL;return AVERROR(EACCES);
}
void dihos_encoded_close(dihos_encoded_media* m)
{
    if(!m)return;
    av_packet_free(&m->packet);avformat_close_input(&m->format);
    if(m->io){av_freep(&m->io->buffer);avio_context_free(&m->io);}
    av_free(m->data);av_free(m);
}
unsigned dihos_encoded_track_count(const dihos_encoded_media* m)
{ return m?m->format->nb_streams:0; }
int dihos_encoded_track_at(const dihos_encoded_media* m,unsigned index,dihos_encoded_track* out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!m || !out || index>=m->format->nb_streams)return AVERROR(EINVAL);
    const AVStream* s=m->format->streams[index];const AVCodecParameters* p=s->codecpar;
    uint32_t codec=p->codec_id==AV_CODEC_ID_H264?DIHOS_ENCODED_H264:p->codec_id==AV_CODEC_ID_AAC?DIHOS_ENCODED_AAC:0;
    if(!codec || s->id<=0 || s->time_base.num<=0 || s->time_base.den<=0 ||
       p->extradata_size<=0 || p->extradata_size>DIHOS_ENCODED_CONFIG_MAX)return AVERROR(ENOTSUP);
    if(p->codec_tag==MKTAG('e','n','c','v') || p->codec_tag==MKTAG('e','n','c','a'))return AVERROR(EACCES);
    if(codec==DIHOS_ENCODED_H264 && (p->width<=0 || p->height<=0 || p->width>DIHOS_VIDEO_WIDTH_MAX || p->height>DIHOS_VIDEO_HEIGHT_MAX))return AVERROR(E2BIG);
    if(codec==DIHOS_ENCODED_AAC && (p->sample_rate<=0 || p->ch_layout.nb_channels<=0 || p->ch_layout.nb_channels>8))return AVERROR(EINVAL);
    *out=(dihos_encoded_track){(uint64_t)s->id,codec,(uint32_t)p->width,(uint32_t)p->height,
        (uint32_t)p->sample_rate,(uint32_t)p->ch_layout.nb_channels,s->time_base.num,s->time_base.den,
        p->extradata,(size_t)p->extradata_size};return 0;
}
int dihos_encoded_open(const uint8_t* data,size_t size,dihos_encoded_media** out)
{
    if(!out)return AVERROR(EINVAL);
    *out=NULL;if(!data || !size)return AVERROR(EINVAL);
    if(size>DIHOS_VIDEO_INPUT_MAX)return AVERROR(E2BIG);
    if(validate_initialization(data,size,0)<0)return AVERROR(ENOTSUP);
    dihos_encoded_media* m=av_mallocz(sizeof(*m));if(!m)return AVERROR(ENOMEM);
    int result=AVERROR(ENOMEM);
    m->data=av_memdup(data,size);m->size=size;if(!m->data)goto fail;
    uint8_t* buffer=av_malloc(16384);if(!buffer)goto fail;
    m->io=avio_alloc_context(buffer,16384,0,m,read_memory,NULL,seek_memory);
    if(!m->io){av_free(buffer);goto fail;}
    m->format=avformat_alloc_context();if(!m->format)goto fail;
    m->format->pb=m->io;m->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    m->format->io_open=deny_nested;m->format->probesize=65536;m->format->max_analyze_duration=0;
    m->format->max_streams=DIHOS_ENCODED_TRACK_MAX;m->format->max_probe_packets=32;
    m->format->max_index_size=1024*1024;m->format->max_picture_buffer=DIHOS_VIDEO_INPUT_MAX;
    const AVInputFormat* mov=av_find_input_format("mov");if(!mov){result=AVERROR(ENOTSUP);goto fail;}
    result=avformat_open_input(&m->format,NULL,mov,NULL);if(result<0)goto fail;
    if(!m->format->nb_streams || m->format->nb_streams>DIHOS_ENCODED_TRACK_MAX){result=AVERROR(E2BIG);goto fail;}
    for(unsigned i=0;i<m->format->nb_streams;++i){
        dihos_encoded_track track;result=dihos_encoded_track_at(m,i,&track);if(result<0)goto fail;
        for(unsigned j=0;j<i;++j)if(m->format->streams[j]->id==(int)track.id){result=AVERROR(EINVAL);goto fail;}
    }
    result=scan_composition_shift(m);if(result<0)goto fail;
    m->packet=av_packet_alloc();if(!m->packet){result=AVERROR(ENOMEM);goto fail;}
    *out=m;return 0;
fail:dihos_encoded_close(m);return result;
}
int dihos_encoded_next(dihos_encoded_media* m,dihos_encoded_sample* out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!m || !out)return AVERROR(EINVAL);
    if(m->failed)return m->failed;
    av_packet_unref(m->packet);
    int result=av_read_frame(m->format,m->packet);
    if(result==AVERROR_EOF)return 0;
    if(result<0){m->failed=result;return result;}
    AVPacket* p=m->packet;
    if(++m->packets>200000 || p->size<=0 || p->size>DIHOS_ENCODED_PACKET_MAX ||
       p->stream_index<0 || (unsigned)p->stream_index>=m->format->nb_streams){m->failed=AVERROR(E2BIG);return m->failed;}
    if(av_packet_get_side_data(p,AV_PKT_DATA_ENCRYPTION_INFO,NULL)) {m->failed=AVERROR(EACCES);return m->failed;}
    if(p->pts==AV_NOPTS_VALUE || p->dts==AV_NOPTS_VALUE || p->duration<=0){m->failed=AVERROR(EINVAL);return m->failed;}
    const AVStream* s=m->format->streams[p->stream_index];
    uint32_t shift=m->composition_shift[p->stream_index];
    if(p->pts<INT64_MIN+shift){m->failed=AVERROR(EOVERFLOW);return m->failed;}
    *out=(dihos_encoded_sample){(uint64_t)s->id,p->data,(size_t)p->size,p->pts-shift,p->dts,p->duration,
        s->time_base.num,s->time_base.den,!!(p->flags&AV_PKT_FLAG_KEY)};return 1;
}
