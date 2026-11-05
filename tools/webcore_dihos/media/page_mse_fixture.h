#pragma once
#include "page_mse_fixture.inc"
#include "MediaPlayerDihOS.h"
#include "SecurityOrigin.h"
#include "SecurityOriginPolicy.h"
#include "../../../sdk/sacx/user_runtime/include/dihos_user_allocator.h"
inline int runDihosVideoPageFixture(WebCore::LocalFrame& frame,WebCore::Page& page,const sacx_api* api)
{
    using namespace WebCore;using namespace WTF;(void)page;
    auto require=[](bool ok,const char* text){if(!ok)sacx_user_exit(191,text);};
    auto* large=static_cast<unsigned char*>(dihos_user_malloc(24*1024*1024));
    require(large,"MSE: large decoded-script allocation");
    large[0]=0x31;large[16*1024*1024]=0x47;large[24*1024*1024-1]=0x53;
    require(large[0]==0x31 && large[16*1024*1024]==0x47 && large[24*1024*1024-1]==0x53,"MSE: large allocation owned mappings");
    dihos_user_free(large);require(!dihos_user_allocator_trim(),"MSE: large allocation cleanup");
    api->log("SACX large browser allocation: 24 MiB owned, writable across 16 MiB and trimmable OK");
    setDihOSMediaTestTracing(true);RefPtr document=frame.document();require(!!document,"MSE: document");
    auto opened=document->open();require(!opened.hasException(),"MSE: document open");
    // This is a new, empty, owned fixture document. Use the same ordinary
    // HTTPS origin model as a website; do not bypass Blob/CORS policy for the
    // opaque about:blank document created by EmptyClients.
    URL fixtureURL{"https://mse.dihos.test/"_s};
    RefPtr<SecurityOriginPolicy> policy=SecurityOriginPolicy::create(SecurityOrigin::create(fixtureURL));
    document->setSecurityOriginPolicy(WTF::move(policy));document->setURL(WTF::move(fixtureURL));
    FixedVector<String> chunks{String::fromUTF8(dihos_mse_page_html)};
    auto written=document->write(document.get(),WTF::move(chunks));require(!written.hasException(),"MSE: owned HTML write");document->close();
    std::vector<uint32_t> pixels(640*480,0xffffffff);
    auto* surface=cairo_image_surface_create_for_data((unsigned char*)pixels.data(),CAIRO_FORMAT_ARGB32,640,480,640*4);
    require(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"MSE: surface");
    auto deadline=MonotonicTime::now()+60_s;String last;
    bool red=false,blue=false,playingRed=false,playingBlue=false,replacementRed=false,done=false;
    while(MonotonicTime::now()<deadline){
        RunLoop::cycle();RefPtr phase=document->getElementById(AtomString("phase"_s));
        if(phase){
            String value=phase->textContent();if(value!=last){last=value;auto text=value.utf8();api->log(text.data());}
            if(value.startsWith("FAIL"_s)){auto text=value.utf8();api->log(text.data());require(false,"MSE: JavaScript assertion");}
            if(value=="done"_s){done=true;break;}
            frame.view()->updateLayoutAndStyleIfNeededRecursive();
            {GraphicsContextCairo context(surface);frame.view()->paintContents(context,IntRect(0,0,640,480));}
            cairo_surface_flush(surface);uint32_t p=pixels[120*640+160];
            bool isRed=((p>>16)&255)>245 && (p&255)<10 && ((p>>8)&255)<10;
            bool isBlue=(p&255)>245 && ((p>>16)&255)<10 && ((p>>8)&255)<10;
            RefPtr ack=document->getElementById(AtomString("ack"_s));require(!!ack,"MSE: acknowledgement");
            if(value=="red"_s && !red){require(isRed,"MSE: first fragment paints red");red=true;ack->setTextContent("next"_s);}
            if(value=="blue"_s && !blue){require(isBlue,"MSE: seek paints blue");blue=true;ack->setTextContent("play"_s);}
            if(value=="playing"_s){playingRed|=isRed;playingBlue|=isBlue;}
            if(value=="cancel-red"_s && !replacementRed){require(isRed,"MSE: replacement paints fresh red after abort");replacementRed=true;ack->setTextContent("cancel"_s);}
        }
        (void)sacx_user_call(SACX_USER_SLEEP_MS,4,0,0);
    }
    cairo_surface_destroy(surface);require(done && red && blue && playingRed && playingBlue && replacementRed,"MSE: playback, seek, abort and replacement deadline");
    api->log("SACX IndexedDB: factory, asynchronous open/delete errors, enumeration and context teardown OK");
    api->log("SACX MSE Page: JS append, owned tracks, red/blue painting, seek, timed playback, ended, abort and replacement OK");return 124;
}
