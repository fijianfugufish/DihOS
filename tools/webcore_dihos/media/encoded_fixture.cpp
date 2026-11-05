#include "encoded_media.h"
#include "fragmented_mp4.h"
#include "fragment_fixture.inc"
#include <stdlib.h>
#include <string.h>
#if defined(__DIHOS__)
#include <sacx_user_api.h>
static void check(bool ok,const char* message){if(!ok)sacx_user_exit(190,message);}
#else
#include <stdio.h>
static void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
#endif
static void tests()
{
    dihos_encoded_media* demux=(dihos_encoded_media*)(uintptr_t)1;
    check(dihos_encoded_open(nullptr,1,&demux)<0 && !demux,"Samples: null input");
    check(dihos_encoded_open(fragment_fixture,8*1024*1024+1,&demux)<0 && !demux,"Samples: input bound");
    check(dihos_encoded_open(fragment_fixture,fragment_init_bytes,nullptr)<0,"Samples: null output");
    check(dihos_encoded_open(fragment_fixture,fragment_init_bytes,&demux)==0,"Samples: initialization-only demux");
    check(dihos_encoded_track_count(demux)==1,"Samples: one track in initialization");
    dihos_encoded_track track{};
    check(dihos_encoded_track_at(demux,0,&track)==0 && track.id==1 && track.codec==DIHOS_ENCODED_H264,"Samples: H264 track metadata");
    check(track.width==64 && track.height==48 && track.time_num==1 && track.time_den==1000,"Samples: track geometry and rational clock");
    check(track.configuration_size>=7 && track.configuration[0]==1 && track.configuration[1]==66,"Samples: owned AVC configuration");
    dihos_encoded_sample sample{};
    check(dihos_encoded_next(demux,&sample)==0 && !sample.bytes && !sample.size,"Samples: init exposes no fabricated samples");
    dihos_encoded_close(demux);
    const unsigned chunks[]={1,7,16,4096,65536};
    for(unsigned chunk:chunks){
        dihos_fmp4* parser=nullptr;check(dihos_fmp4_create(&parser)==0,"Samples: assembler");
        size_t pos=0;unsigned index=0;
        while(pos<sizeof(fragment_fixture)){
            size_t amount=sizeof(fragment_fixture)-pos;if(amount>chunk)amount=chunk;
            size_t used=0;int result=dihos_fmp4_append(parser,fragment_fixture+pos,amount,&used);pos+=used;
            check(result>=0 && used>0 && used<=amount,"Samples: partial feed");
            if(result!=DIHOS_FMP4_SEGMENT)continue;
            const uint8_t* data=nullptr;size_t size=0;
            check(dihos_fmp4_view(parser,&data,&size)==1,"Samples: segment view");
            auto* copy=(uint8_t*)malloc(size);check(copy,"Samples: source copy");memcpy(copy,data,size);
            check(dihos_encoded_open(copy,size,&demux)==0,"Samples: complete fragment demux");
            memset(copy,0,size);free(copy); // demux must own both config and packets.
            check(dihos_encoded_track_at(demux,0,&track)==0 && track.configuration[0]==1,"Samples: configuration ownership");
            unsigned count=0;
            for(;;){
                result=dihos_encoded_next(demux,&sample);if(!result)break;
                check(result==1 && count<5 && sample.track_id==1,"Samples: encoded packet track");
                check(sample.time_num==1 && sample.time_den==1000 && sample.presentation==(int64_t)index*200 && sample.decode==sample.presentation && sample.duration==200,"Samples: exact absolute PTS DTS and duration");
                check(sample.sync && sample.size>4 && sample.bytes[4]==0x65,"Samples: key frame AVCC packet");
                uint32_t nal=((uint32_t)sample.bytes[0]<<24)|((uint32_t)sample.bytes[1]<<16)|((uint32_t)sample.bytes[2]<<8)|sample.bytes[3];
                check(nal==sample.size-4,"Samples: actual encoded payload");++count;++index;
            }
            check(count==5,"Samples: five packets per fragment");
            sample.size=42;check(dihos_encoded_next(demux,&sample)==0 && !sample.size,"Samples: repeated EOF zeroes output");
            dihos_encoded_close(demux);demux=nullptr;check(dihos_fmp4_consume(parser)==0,"Samples: consume fragment");
        }
        check(index==10,"Samples: ten packets across fragments");dihos_fmp4_close(parser);
    }
    check(dihos_encoded_open(long_timestamp_fixture,sizeof(long_timestamp_fixture),&demux)==0,"Samples: long tfdt and signed CTS opens");
    for(unsigned index=0;index<5;++index){
        check(dihos_encoded_next(demux,&sample)==1,"Samples: long timestamp packet");
        int64_t dts=INT64_C(4294967587)+index*200;
#if !defined(__DIHOS__)
        if(sample.decode!=dts || sample.presentation!=dts+(index%2?50:-50))fprintf(stderr,"actual DTS=%lld PTS=%lld expected DTS=%lld num=%d den=%d duration=%lld\n",(long long)sample.decode,(long long)sample.presentation,(long long)dts,sample.time_num,sample.time_den,(long long)sample.duration);
#endif
        check(sample.decode==dts && sample.presentation==dts+(index%2?50:-50) && sample.time_den==1000 && sample.duration==200,"Samples: signed CTS and 64-bit tfdt preserved");
    }
    check(dihos_encoded_next(demux,&sample)==0,"Samples: long fragment ends");dihos_encoded_close(demux);
    check(dihos_encoded_open(audio_timing_fixture,sizeof(audio_timing_fixture),&demux)==0,"Samples: AAC timing container opens");
    check(dihos_encoded_track_at(demux,0,&track)==0 && track.codec==DIHOS_ENCODED_AAC && track.channels==2 && track.sample_rate==44100 &&
        track.configuration_size==2 && track.configuration[0]==0x12 && track.configuration[1]==0x10,"Samples: AAC-LC metadata");
    for(unsigned index=0;index<4;++index){
        check(dihos_encoded_next(demux,&sample)==1 && sample.track_id==1 && sample.decode==index*1024 &&
            sample.presentation==sample.decode && sample.duration==1024 && sample.time_num==1 && sample.time_den==44100 && sample.size==6,"Samples: exact audio clock without audio output");
    }
    check(dihos_encoded_next(demux,&sample)==0,"Samples: audio timing fragment ends");dihos_encoded_close(demux);
    for(unsigned length=1;length<32;++length){
        demux=(dihos_encoded_media*)(uintptr_t)1;
        check(dihos_encoded_open(fragment_fixture,length,&demux)<0 && !demux,"Samples: malformed init rejected with cleared output");
    }
    dihos_encoded_close(nullptr);
}
#if !defined(DIHOS_ENCODED_FIXTURE_EMBEDDED)
#if defined(__DIHOS__)
extern "C" int dihos_user_main(const sacx_api* api)
{tests();api->log("SACX encoded samples: init metadata, AVC config, owned packets and rational PTS DTS across fragments OK");return 119;}
#else
int main(){tests();puts("PASS: encoded tracks, initialization, owned AVC packets and exact rational timestamps");}
#endif
#endif
