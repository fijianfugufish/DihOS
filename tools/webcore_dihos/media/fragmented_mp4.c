#include "fragmented_mp4.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { FTYP=0x66747970, MOOV=0x6d6f6f76, MOOF=0x6d6f6f66, MDAT=0x6d646174,
       STYP=0x73747970, SIDX=0x73696478, FREE=0x66726565, SKIP=0x736b6970,
       EMSG=0x656d7367, PRFT=0x70726674 };
struct dihos_fmp4 {
    uint8_t *data,*previous_init;
    size_t length,capacity,initialization;
    size_t previous_size;
    uint64_t initialization_version;
    uint8_t header[16];
    size_t header_used,header_size;
    uint64_t remaining;
    uint32_t type;
    int copy_box,have_ftyp,have_moov,have_moof,ready,error;
};
static uint32_t be32(const uint8_t* p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int fail(dihos_fmp4* s,int error) { s->error=-error;return s->error; }
static int reserve(dihos_fmp4* s,size_t count)
{
    if(count>DIHOS_FMP4_LIMIT-s->length)return fail(s,E2BIG);
    size_t need=s->length+count;
    if(need<=s->capacity)return 0;
    size_t capacity=s->capacity?s->capacity:4096;
    while(capacity<need)capacity*=2;
    if(capacity>DIHOS_FMP4_LIMIT)capacity=DIHOS_FMP4_LIMIT;
    void* next=realloc(s->data,capacity);
    if(!next)return fail(s,ENOMEM);
    s->data=next;s->capacity=capacity;return 0;
}
int dihos_fmp4_create(dihos_fmp4** out)
{
    if(!out)return -EINVAL;
    *out=calloc(1,sizeof(dihos_fmp4));return *out?0:-ENOMEM;
}
void dihos_fmp4_close(dihos_fmp4* s) { if(s){free(s->data);free(s->previous_init);free(s);} }
void dihos_fmp4_reset(dihos_fmp4* s,int keep)
{
    if(!s)return;
    if(keep && s->previous_init){
        memcpy(s->data,s->previous_init,s->previous_size);s->initialization=s->previous_size;s->have_moov=1;
    }
    free(s->previous_init);
    int retained=keep && s->have_moov;uint64_t version=s->initialization_version;
    uint8_t* data=retained?s->data:NULL;
    size_t size=retained?s->initialization:0,capacity=retained?s->capacity:0;
    if(!retained)free(s->data);
    memset(s,0,sizeof(*s));s->data=data;s->length=size;s->capacity=capacity;
    s->initialization=size;s->have_ftyp=retained;s->have_moov=retained;
    s->initialization_version=version;
}
static int begin_box(dihos_fmp4* s)
{
    uint64_t length=be32(s->header);
    if(length==1)length=((uint64_t)be32(s->header+8)<<32)|be32(s->header+12);
    /* A size-to-EOF box cannot be delimited while more segments may arrive. */
    if(length<s->header_size)return fail(s,EINVAL);
    if(length>DIHOS_FMP4_LIMIT)return fail(s,E2BIG);
    s->type=be32(s->header+4);s->copy_box=0;
    switch(s->type){
    case FTYP:
        if(s->have_moov && !s->have_moof && !s->previous_init){
            // At most one extra 1 MiB initialization is retained for rollback.
            uint8_t* backup=malloc(s->initialization);if(!backup)return fail(s,ENOMEM);
            memcpy(backup,s->data,s->initialization);s->previous_init=backup;s->previous_size=s->initialization;
            s->length=0;s->initialization=0;s->have_ftyp=0;s->have_moov=0;
        }
        if(s->have_ftyp || s->have_moov || s->have_moof || length<s->header_size+8)return fail(s,EINVAL);
        s->copy_box=1;break;
    case MOOV:
        if(!s->have_ftyp || s->have_moov || s->have_moof)return fail(s,EINVAL);
        s->copy_box=1;break;
    case MOOF:
        if(!s->have_moov || s->have_moof)return fail(s,EINVAL);
        s->copy_box=1;break;
    case MDAT:
        if(!s->have_moof)return fail(s,EINVAL);
        s->copy_box=1;break;
    case STYP:case SIDX:case FREE:case SKIP:case EMSG:case PRFT:
        /* Preserve interstitial boxes after moof: trun data offsets can count
         * their bytes. Prefix metadata is not needed by our demuxed segment. */
        s->copy_box=s->have_moof;break;
    default:return fail(s,EINVAL);
    }
    if((s->type==FTYP || s->type==MOOV) && length>DIHOS_FMP4_INIT_LIMIT-s->length)return fail(s,E2BIG);
    if(s->copy_box){
        if(length>DIHOS_FMP4_LIMIT-s->length)return fail(s,E2BIG);
        if(reserve(s,(size_t)length)<0)return s->error;
        memcpy(s->data+s->length,s->header,s->header_size);s->length+=s->header_size;
    }
    s->remaining=length-s->header_size;return 0;
}
static void end_box(dihos_fmp4* s)
{
    switch(s->type){
    case FTYP:s->have_ftyp=1;break;
    case MOOV:
        s->have_moov=1;s->initialization=s->length;++s->initialization_version;
        free(s->previous_init);s->previous_init=NULL;s->previous_size=0;break;
    case MOOF:s->have_moof=1;break;
    case MDAT:s->ready=1;break;
    }
    s->header_used=0;s->header_size=0;s->type=0;s->copy_box=0;
}
int dihos_fmp4_append(dihos_fmp4* s,const uint8_t* bytes,size_t size,size_t* consumed)
{
    if(!consumed)return -EINVAL;
    *consumed=0;if(!s || (!bytes && size))return -EINVAL;
    if(s->error)return s->error;
    if(s->ready)return DIHOS_FMP4_SEGMENT;
    unsigned boxes=0;
    while(*consumed<size){
        if(boxes==64)break; /* Bounded service, including metadata-only input. */
        if(!s->remaining){
            size_t target=s->header_size?s->header_size:8;
            size_t count=target-s->header_used;
            if(count>size-*consumed)count=size-*consumed;
            memcpy(s->header+s->header_used,bytes+*consumed,count);
            s->header_used+=count;*consumed+=count;
            if(s->header_used<target)break;
            if(!s->header_size){s->header_size=be32(s->header)==1?16:8;if(s->header_size==16)continue;}
            if(begin_box(s)<0)return s->error;
            if(!s->remaining){end_box(s);++boxes;if(s->ready)return DIHOS_FMP4_SEGMENT;continue;}
        }
        size_t count=size-*consumed;
        if(count>s->remaining)count=(size_t)s->remaining;
        if(s->copy_box){memcpy(s->data+s->length,bytes+*consumed,count);s->length+=count;}
        s->remaining-=count;*consumed+=count;
        if(!s->remaining){end_box(s);++boxes;if(s->ready)return DIHOS_FMP4_SEGMENT;}
    }
    return DIHOS_FMP4_MORE;
}
int dihos_fmp4_view(const dihos_fmp4* s,const uint8_t** bytes,size_t* size)
{
    if(bytes)*bytes=NULL;if(size)*size=0;
    if(!s || !bytes || !size)return -EINVAL;
    if(s->error)return s->error;
    if(!s->ready)return DIHOS_FMP4_MORE;
    *bytes=s->data;*size=s->length;return DIHOS_FMP4_SEGMENT;
}
int dihos_fmp4_consume(dihos_fmp4* s)
{
    if(!s)return -EINVAL;
    if(s->error)return s->error;
    if(!s->ready)return -EINVAL;
    s->length=s->initialization;s->ready=0;s->have_moof=0;return 0;
}
int dihos_fmp4_initialization(const dihos_fmp4* s,const uint8_t** bytes,size_t* size)
{
    if(bytes)*bytes=NULL;if(size)*size=0;
    if(!s || !bytes || !size)return -EINVAL;
    if(s->error)return s->error;
    if(!s->have_moov)return DIHOS_FMP4_MORE;
    *bytes=s->data;*size=s->initialization;return DIHOS_FMP4_SEGMENT;
}
uint64_t dihos_fmp4_initialization_version(const dihos_fmp4* s) { return s?s->initialization_version:0; }
int dihos_fmp4_finish(dihos_fmp4* s)
{
    if(!s)return -EINVAL;
    if(s->error)return s->error;
    if(s->ready)return DIHOS_FMP4_SEGMENT;
    if(!s->have_moov || s->have_moof || s->header_used || s->remaining)return fail(s,EINVAL);
    return DIHOS_FMP4_MORE;
}
