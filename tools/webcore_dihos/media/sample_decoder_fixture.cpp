#define DIHOS_ENCODED_FIXTURE_EMBEDDED 1
#include "encoded_fixture.cpp"
#include "sample_decoder.h"

static int collect(dihos_sample_decoder* decoder,unsigned& frames)
{
    for(unsigned work=0;work<32;++work){
        dihos_video_frame frame{};int result=dihos_sample_decoder_receive(decoder,&frame);
        if(result==DIHOS_VIDEO_AGAIN || result==DIHOS_VIDEO_EOF)return result;
        check(result==DIHOS_VIDEO_FRAME && frames<10,"Decoder: bounded frame receive");
        check(frame.width==64 && frame.height==48 && frame.presentation_ms==(int64_t)frames*200 && frame.duration_ms==200,"Decoder: persistent geometry and presentation time");
        const uint8_t* p=frame.bgra;
        check(p[3]==255 && p[1]<10 && (frames<5?p[2]>245 && p[0]<10:p[0]>245 && p[2]<10),"Decoder: actual red/blue pixels");
        ++frames;
    }
    check(false,"Decoder: receive work bounded");return -1;
}
static void decoderTests()
{
    check(dihos_sample_decoder_create(nullptr,nullptr)<0,"Decoder: invalid arguments");
    dihos_encoded_media* init=nullptr;
    check(dihos_encoded_open(fragment_fixture,fragment_init_bytes,&init)==0,"Decoder: initialization demux");
    dihos_encoded_track track{};check(dihos_encoded_track_at(init,0,&track)==0,"Decoder: initial track");
    auto* configuration=(uint8_t*)malloc(track.configuration_size);check(configuration,"Decoder: config allocation");
    memcpy(configuration,track.configuration,track.configuration_size);track.configuration=configuration;
    dihos_sample_decoder* decoder=nullptr;
    check(dihos_sample_decoder_create(&track,&decoder)==0,"Decoder: persistent instance");
    memset(configuration,0,track.configuration_size);free(configuration);dihos_encoded_close(init);
    unsigned backpressure=0;
    for(unsigned repeat=0;repeat<4;++repeat){
        unsigned frames=repeat==3?1:0,packets=0;dihos_sample_decoder_flush(decoder);
        dihos_fmp4* assembler=nullptr;check(dihos_fmp4_create(&assembler)==0,"Decoder: segment assembler");
        size_t pos=0;
        while(pos<sizeof(fragment_fixture)){
            size_t used=0;int status=dihos_fmp4_append(assembler,fragment_fixture+pos,sizeof(fragment_fixture)-pos,&used);pos+=used;
            check(status==DIHOS_FMP4_SEGMENT && used>0,"Decoder: assembled fragment");
            const uint8_t* data=nullptr;size_t size=0;check(dihos_fmp4_view(assembler,&data,&size)==1,"Decoder: segment view");
            dihos_encoded_media* demux=nullptr;check(dihos_encoded_open(data,size,&demux)==0,"Decoder: fragment demux");
            dihos_encoded_sample sample{};
            for(;;){
                status=dihos_encoded_next(demux,&sample);if(!status)break;check(status==1,"Decoder: sample read");
                if(repeat==3 && !packets)sample.non_displaying=1; // Decode preroll without painting it.
                auto wrong=sample;wrong.track_id=2;check(dihos_sample_decoder_send(decoder,&wrong)<0,"Decoder: rejects another track");
                unsigned retries=0;
                for(;;){
                    status=dihos_sample_decoder_send(decoder,&sample);
                    if(status!=DIHOS_VIDEO_AGAIN)break;
                    unsigned before=frames;check(collect(decoder,frames)==DIHOS_VIDEO_AGAIN && frames>before,"Decoder: real backpressure drains frames");
                    ++backpressure;check(++retries<8,"Decoder: bounded retry");
                }
                check(status==0,"Decoder: sample accepted");++packets;
                // Deliberately do not receive until codec backpressure. Closing
                // the demux at each boundary must not reset codec references.
            }
            dihos_encoded_close(demux);check(dihos_fmp4_consume(assembler)==0,"Decoder: fragment consumed");
        }
        dihos_fmp4_close(assembler);
        unsigned retries=0;
        while(dihos_sample_decoder_send(decoder,nullptr)==DIHOS_VIDEO_AGAIN){check(collect(decoder,frames)==DIHOS_VIDEO_AGAIN,"Decoder: end drain");check(++retries<8,"Decoder: bounded end retry");}
        check(collect(decoder,frames)==DIHOS_VIDEO_EOF && frames==10 && packets==10,"Decoder: one instance across both fragments, drained at real end only");
        dihos_video_frame empty{};empty.width=42;
        check(dihos_sample_decoder_receive(decoder,&empty)==DIHOS_VIDEO_EOF && !empty.width,"Decoder: EOF clears frame view");
    }
    check(backpressure>0,"Decoder: backpressure path exercised");
    dihos_sample_decoder_close(decoder);dihos_sample_decoder_close(nullptr);
    track.codec=DIHOS_ENCODED_AAC;decoder=(dihos_sample_decoder*)(uintptr_t)1;
    check(dihos_sample_decoder_create(&track,&decoder)<0 && !decoder,"Decoder: audio output is not falsely advertised");
}
#if defined(__DIHOS__)
extern "C" int dihos_user_main(const sacx_api* api)
{tests();decoderTests();api->log("SACX persistent decoder: encoded tracks, fragment continuity, BGRA timing, backpressure and flush OK");return 119;}
#else
int main(){tests();decoderTests();puts("PASS: persistent H264 decoding across fragments, owned config, timestamps, backpressure and flush");}
#endif
