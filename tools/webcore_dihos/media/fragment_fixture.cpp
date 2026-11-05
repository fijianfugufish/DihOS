#include "fragmented_mp4.h"
#include "memory_video.h"
#include "fragment_fixture.inc"
#include <stdlib.h>
#include <string.h>
#if defined(__DIHOS__)
#include <sacx_user_api.h>
static void check(bool ok,const char* message){if(!ok)sacx_user_exit(189,message);}
#else
#include <stdio.h>
static void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
#endif
static void decode(dihos_fmp4* stream,unsigned segment)
{
    const uint8_t* data=nullptr;size_t size=0;
    check(dihos_fmp4_view(stream,&data,&size)==DIHOS_FMP4_SEGMENT && size>0,"fMP4: owned view");
    dihos_video* video=nullptr;
    check(dihos_video_open(data,size,&video)==0 && video,"fMP4: fragment demux opens");
    unsigned frames=0;bool ended=false;
    for(unsigned turn=0;turn<100;++turn){
        dihos_video_frame frame{};int result=dihos_video_next(video,&frame);
        if(result==DIHOS_VIDEO_AGAIN)continue;
        if(result==DIHOS_VIDEO_EOF){ended=true;break;}
        check(result==DIHOS_VIDEO_FRAME && frames<5,"fMP4: actual fragment decoding");
        check(frame.width==64 && frame.height==48,"fMP4: geometry");
        check(frame.presentation_ms==(int64_t)(segment*1000+frames*200) && frame.duration_ms==200,"fMP4: absolute timestamps survive segment switch");
        const uint8_t* p=frame.bgra;
        check(p[3]==255 && p[1]<10 && (!segment?p[2]>245 && p[0]<10:p[0]>245 && p[2]<10),"fMP4: red/blue decoded pixels");
        ++frames;
    }
    check(ended && frames==5,"fMP4: five frames and drained fragment");dihos_video_close(video);
}
static void tests()
{
    check(dihos_fmp4_create(nullptr)<0,"fMP4: null creation");dihos_fmp4_close(nullptr);
    const unsigned chunks[]={1u,7u,8u,15u,16u,17u,509u,65536u};
    for(unsigned chunk:chunks){
        dihos_fmp4* stream=nullptr;check(dihos_fmp4_create(&stream)==0,"fMP4: allocate");
        size_t pos=0;unsigned segments=0;
        while(pos<sizeof(fragment_fixture)){
            size_t amount=sizeof(fragment_fixture)-pos;if(amount>chunk)amount=chunk;
            auto* copy=(uint8_t*)malloc(amount);check(copy,"fMP4: feed allocation");memcpy(copy,fragment_fixture+pos,amount);
            size_t consumed=0;int result=dihos_fmp4_append(stream,copy,amount,&consumed);
            memset(copy,0,amount);free(copy);pos+=consumed;
            check(result>=0 && consumed>0 && consumed<=amount,"fMP4: split feed progress");
            if(result==DIHOS_FMP4_SEGMENT){
                size_t blocked=42;check(dihos_fmp4_append(stream,fragment_fixture,1,&blocked)==DIHOS_FMP4_SEGMENT && !blocked,"fMP4: backpressure consumes no bytes");
                decode(stream,segments++);check(dihos_fmp4_consume(stream)==0,"fMP4: consume");
            }
        }
        check(segments==2 && dihos_fmp4_finish(stream)==0,"fMP4: complete two segments");
        dihos_fmp4_reset(stream,1);size_t used=0;
        check(dihos_fmp4_append(stream,fragment_fixture+fragment_init_bytes,11,&used)==0 && used==11,"fMP4: incomplete fragment");
        check(dihos_fmp4_finish(stream)<0,"fMP4: truncation rejected");
        dihos_fmp4_reset(stream,1);
        check(dihos_fmp4_append(stream,fragment_fixture+fragment_init_bytes,fragment_first_bytes,&used)==1,"fMP4: abort keeps valid init");
        decode(stream,0);dihos_fmp4_reset(stream,0);
        check(dihos_fmp4_finish(stream)<0,"fMP4: full reset releases init");dihos_fmp4_close(stream);
    }
    dihos_fmp4* stream=nullptr;check(dihos_fmp4_create(&stream)==0,"fMP4: malformed allocate");
    size_t committed=0;
    check(dihos_fmp4_append(stream,fragment_fixture,fragment_init_bytes,&committed)==0,"fMP4: initial version append");
    check(dihos_fmp4_initialization_version(stream)==1,"fMP4: first initialization version");
    const unsigned cuts[]={1,7,8,9,16,31};
    for(unsigned cut:cuts){
        check(dihos_fmp4_append(stream,fragment_fixture,cut,&committed)==0 && committed==cut,"fMP4: partial replacement initialization");
        dihos_fmp4_reset(stream,1);check(dihos_fmp4_initialization_version(stream)==1,"fMP4: abort retains committed version");
        const uint8_t* data=nullptr;size_t bytes=0;
        check(dihos_fmp4_initialization(stream,&data,&bytes)==1 && bytes==fragment_init_bytes && !memcmp(data,fragment_fixture,bytes),"fMP4: abort restores old initialization bytes");
        check(dihos_fmp4_append(stream,fragment_fixture+fragment_init_bytes,fragment_first_bytes,&committed)==1,"fMP4: media after aborted replacement");
        decode(stream,0);check(dihos_fmp4_consume(stream)==0,"fMP4: replacement rollback consume");
    }
    check(dihos_fmp4_append(stream,fragment_fixture,fragment_init_bytes,&committed)==0 && dihos_fmp4_initialization_version(stream)==2,"fMP4: complete replacement commits once");
    check(dihos_fmp4_append(stream,fragment_fixture+fragment_init_bytes+fragment_first_bytes,sizeof(fragment_fixture)-fragment_init_bytes-fragment_first_bytes,&committed)==1,"fMP4: media after reinitialization");
    decode(stream,1);check(dihos_fmp4_consume(stream)==0,"fMP4: reinitialized consume");
    check(dihos_fmp4_append(stream,fragment_fixture,9,&committed)==0 && dihos_fmp4_finish(stream)<0,"fMP4: truncated replacement fails");
    dihos_fmp4_reset(stream,1);check(dihos_fmp4_initialization_version(stream)==2,"fMP4: parse failure rollback retains version");
    dihos_fmp4_reset(stream,0);
    check(dihos_fmp4_append(stream,fragment_fixture,fragment_init_bytes,&committed)==0 && dihos_fmp4_initialization_version(stream)==3,"fMP4: version remains monotonic after full reset");
    dihos_fmp4_reset(stream,0);
    size_t initial_used=0,initial_size=0;const uint8_t* initial_data=nullptr;
    check(dihos_fmp4_append(stream,fragment_fixture,fragment_init_bytes,&initial_used)==0 && initial_used==fragment_init_bytes,"fMP4: standalone init append");
    check(dihos_fmp4_initialization(stream,&initial_data,&initial_size)==1 && initial_size==fragment_init_bytes && !memcmp(initial_data,fragment_fixture,initial_size),"fMP4: metadata available before media");
    dihos_fmp4_reset(stream,0);
    const uint8_t malformed[][16]={
        {0,0,0,0,'f','r','e','e'}, {0,0,0,7,'f','r','e','e'},
        {0,0,0,8,'m','d','a','t'}, {0,0,0,8,'m','o','o','f'},
        {0,0,0,8,'m','o','o','v'}, {0,0,0,8,'u','u','i','d'},
        {0,0,0,1,'f','r','e','e',0,0,0,0,0,0,0,15},
        {0,0,0,1,'f','r','e','e',255,255,255,255,255,255,255,255},
        {0,128,0,1,'f','r','e','e'}, {0,16,0,1,'f','t','y','p'}
    };
    for(auto& input:malformed){
        dihos_fmp4_reset(stream,0);size_t used=0;
        check(dihos_fmp4_append(stream,input,sizeof(input),&used)<0,"fMP4: invalid size/order/type rejected");
        used=99;check(dihos_fmp4_append(stream,fragment_fixture,sizeof(fragment_fixture),&used)<0 && !used,"fMP4: error sticky until reset");
    }
    // Extended-size metadata header may itself arrive one byte at a time.
    const uint8_t extended[]={0,0,0,1,'f','r','e','e',0,0,0,0,0,0,0,16};
    dihos_fmp4_reset(stream,0);
    for(unsigned i=0;i<sizeof(extended);++i){size_t used=0;check(dihos_fmp4_append(stream,extended+i,1,&used)==0 && used==1,"fMP4: extended-size split header");}
    size_t used=0;check(dihos_fmp4_append(stream,fragment_fixture,sizeof(fragment_fixture),&used)==1,"fMP4: valid input after metadata");
    dihos_fmp4_reset(stream,0);
    uint8_t metadata[8*100];for(unsigned i=0;i<100;++i){const uint8_t freebox[]={0,0,0,8,'f','r','e','e'};memcpy(metadata+i*8,freebox,8);}
    check(dihos_fmp4_append(stream,metadata,sizeof(metadata),&used)==0 && used==64*8,"fMP4: bounded metadata service");
    // Header mutation corpus also exercises incomplete extended sizes and
    // recovery. This runs under ASan/UBSan and the actual EL0 allocator.
    uint32_t random=0x745912ab;
    for(unsigned attempt=0;attempt<512;++attempt){
        uint8_t input[64];memcpy(input,fragment_fixture,sizeof(input));
        for(unsigned edit=0;edit<4;++edit){random^=random<<13;random^=random>>17;random^=random<<5;input[random%sizeof(input)]^=(uint8_t)(random>>24);}
        dihos_fmp4_reset(stream,0);size_t pos=0;
        while(pos<sizeof(input)){
            size_t count=1+attempt%17;if(count>sizeof(input)-pos)count=sizeof(input)-pos;
            int result=dihos_fmp4_append(stream,input+pos,count,&used);
            check(used<=count,"fMP4: mutation never overconsumes");pos+=used;
            if(result<0)break;
            if(result==DIHOS_FMP4_SEGMENT){check(dihos_fmp4_consume(stream)==0,"fMP4: mutation consume");}
            check(used>0,"fMP4: mutation cannot spin");
        }
        (void)dihos_fmp4_finish(stream);
    }
    dihos_fmp4_close(stream);
}
#if defined(__DIHOS__)
extern "C" int dihos_user_main(const sacx_api* api)
{tests();api->log("SACX fMP4: split appends, fragment decoding, timestamps, backpressure, abort and malformed input OK");return 119;}
#else
int main(){tests();puts("PASS: streaming fMP4 assembly and real H.264 fragment decoding");}
#endif
