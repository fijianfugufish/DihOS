#include "memory_video.h"
#include "video_fixture.inc"
#include <stdlib.h>
#include <string.h>
#if defined(__DIHOS__)
#include <sacx_user_api.h>
static void check(bool ok,const char* message){if(!ok)sacx_user_exit(188,message);}
#else
#include <stdio.h>
static void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
#endif
static void tests()
{
    dihos_video* video=(dihos_video*)(uintptr_t)1;
    check(dihos_video_open(nullptr,1,&video)<0 && !video,"Video: null input rejection");
    check(dihos_video_open(video_fixture,DIHOS_VIDEO_INPUT_MAX+1,&video)<0 && !video,"Video: oversized input rejection");
    check(dihos_video_open(video_fixture,sizeof(video_fixture),nullptr)<0,"Video: null output rejection");
    dihos_video_close(nullptr);
    for(unsigned repeat=0;repeat<8;++repeat){
        /* Mutate/release caller storage before decode to prove ownership. */
        auto* copy=(uint8_t*)malloc(sizeof(video_fixture));check(copy,"Video: fixture allocation");
        memcpy(copy,video_fixture,sizeof(video_fixture));
        int result=dihos_video_open(copy,sizeof(video_fixture),&video);
        memset(copy,0,sizeof(video_fixture));free(copy);
        check(result==0 && video,"Video: owned MP4 opens");
        check(dihos_video_duration_ms(video)==2000,"Video: duration metadata");
        unsigned frames=0;bool ended=false;
        for(unsigned turn=0;turn<100;++turn){
            dihos_video_frame frame{};result=dihos_video_next(video,&frame);
            if(result==DIHOS_VIDEO_EOF){ended=true;break;}
            if(result==DIHOS_VIDEO_AGAIN)continue;
            check(result==DIHOS_VIDEO_FRAME,"Video: frame decode");
            check(frame.width==64 && frame.height==48 && frame.stride==256,"Video: frame geometry");
            check(frame.presentation_ms==(int64_t)frames*200 && frame.duration_ms==200,"Video: presentation timestamps");
            check(frames<10,"Video: bounded frame count");
            for(unsigned pixel=0;pixel<64*48;++pixel){
                const uint8_t* p=frame.bgra+pixel*4;
                check(p[3]==255 && p[1]<10 && (frames<5 ? p[2]>245 && p[0]<10 : p[0]>245 && p[2]<10),"Video: decoded BGRA pixels");
            }
            ++frames;
            if(repeat==1 && frames==3)break; // cancellation before EOF
        }
        check(repeat==1 ? frames==3 : ended && frames==10,"Video: EOF and cancellation");
        check(dihos_video_seek_ms(video,-1)<0 && dihos_video_seek_ms(video,2001)<0,"Video: seek bounds");
        check(dihos_video_seek_ms(video,1000)==0,"Video: seek after EOF");
        dihos_video_frame seeked{};result=dihos_video_next(video,&seeked);
        check(result==DIHOS_VIDEO_FRAME && seeked.presentation_ms==1000 && seeked.bgra[0]>245,"Video: seek displays blue frame");
        check(dihos_video_seek_ms(video,0)==0,"Video: seek back to start");
        result=dihos_video_next(video,&seeked);
        check(result==DIHOS_VIDEO_FRAME && seeked.presentation_ms==0 && seeked.bgra[2]>245,"Video: seek displays red frame");
        dihos_video_close(video);video=nullptr;
    }
    // Truncated MP4 headers must fail without retaining a partially opened decoder.
    for(unsigned bytes=1;bytes<32;++bytes){
        video=(dihos_video*)(uintptr_t)1;
        check(dihos_video_open(video_fixture,bytes,&video)<0 && !video,"Video: truncated header rejection");
    }
    check(dihos_video_open(video_hd_fixture,sizeof(video_hd_fixture),&video)==0,"Video: 1080p MP4 opens");
    for(unsigned frame_index=0;frame_index<2;++frame_index){
        dihos_video_frame frame{};int result=dihos_video_next(video,&frame);
        check(result==DIHOS_VIDEO_FRAME && frame.width==1920 && frame.height==1080 && frame.stride==7680,"Video: 1080p SPS crop and geometry");
        check(frame.presentation_ms==(int64_t)frame_index*200,"Video: 1080p timing");
        for(unsigned pixel=0;pixel<1920*1080;++pixel){const uint8_t* p=frame.bgra+pixel*4;
            check(p[3]==255 && p[1]<10 && (!frame_index?p[2]>245 && p[0]<10:p[0]>245 && p[2]<10),"Video: 1080p BGRA pixels");
        }
    }
    dihos_video_close(video);
}
#if defined(__DIHOS__)
extern "C" int dihos_user_main(const sacx_api* api)
{ tests();api->log("SACX video: owned MP4, H.264 frames, BGRA, timestamps, seek, cancellation and malformed input OK");return 119; }
#else
int main(){tests();puts("PASS: memory-only MP4/H.264 decoding, owned buffers, timestamps, cancellation and malformed input");}
#endif
