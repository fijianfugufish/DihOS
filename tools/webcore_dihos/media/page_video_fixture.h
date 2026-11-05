#pragma once
// Owned offline Page fixture. The shipping browser does not include this code.
#include "Document.h"
#include "Element.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "GraphicsContextCairo.h"
#include "Settings.h"
#include "MediaPlayerDihOS.h"
#include <wtf/RunLoop.h>
#include <cairo.h>
#include <vector>
#include "page_video_fixture.inc"
inline int runDihosVideoPageFixture(WebCore::LocalFrame& frame,WebCore::Page& page,const sacx_api* api)
{
    using namespace WebCore;using namespace WTF;
    auto require=[](bool ok,const char* message){if(!ok)sacx_user_exit(189,message);};
    setDihOSMediaTestTracing(true);
    RefPtr document=frame.document();require(!!document,"Video Page: initial document");
    auto open=document->open();require(!open.hasException(),"Video Page: document open");
    FixedVector<String> chunks { String::fromUTF8(dihos_video_page_html) };
    auto write=document->write(document.get(),WTF::move(chunks));require(!write.hasException(),"Video Page: owned HTML");
    document->close();
    std::vector<uint32_t> pixels(640*480,0xffffffff);
    auto* surface=cairo_image_surface_create_for_data((unsigned char*)pixels.data(),CAIRO_FORMAT_ARGB32,640,480,640*4);
    require(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"Video Page: paint surface");
    auto deadline=MonotonicTime::now()+30_s;bool blueSeek=false,redPlayback=false,bluePlayback=false,done=false;String lastPhase;
    while(MonotonicTime::now()<deadline){
        RunLoop::cycle();
        RefPtr phase=document->getElementById(AtomString("phase"_s));
        if(phase){
            String value=phase->textContent();
            if(value!=lastPhase){lastPhase=value;auto text=value.utf8();api->log(text.data());}
            if(value.startsWith("FAIL"_s)){auto text=value.utf8();sacx_user_log(text.data());require(false,"Video Page: JavaScript assertion");}
            frame.view()->updateLayoutAndStyleIfNeededRecursive();
            {GraphicsContextCairo context(surface);frame.view()->paintContents(context,IntRect(0,0,640,480));}
            cairo_surface_flush(surface);uint32_t pixel=pixels[120*640+160];
            bool red=((pixel>>16)&255)>245 && (pixel&255)<10 && ((pixel>>8)&255)<10;
            bool blue=(pixel&255)>245 && ((pixel>>16)&255)<10 && ((pixel>>8)&255)<10;
            if(value=="blue"_s){
                require(blue,"Video Page: seek paints decoded blue pixels");blueSeek=true;
                RefPtr ack=document->getElementById(AtomString("ack"_s));require(!!ack,"Video Page: acknowledgement marker");
                ack->setTextContent("play"_s);
            }
            if(value=="playing"_s){redPlayback|=red;bluePlayback|=blue;}
            if(value=="done"_s){done=true;break;}
        }
        (void)sacx_user_call(SACX_USER_SLEEP_MS,4,0,0);
    }
    cairo_surface_destroy(surface);
    require(done && blueSeek && redPlayback && bluePlayback,"Video Page: MP4 playback/seek/ended deadline");
    api->log("SACX video Page: HTML video, codec probes, dimensions, seek, red/blue painting, timed muted playback and ended OK");
    return 124;
}
