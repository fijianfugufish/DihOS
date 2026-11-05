// Native, offscreen WebCore Page/DOM/JavaScript fixture. No host renderer.
#include "config.h"
#include <sacx_user_api.h>
#include "Document.h"
#include "DNS.h"
#include "DOMRect.h"
#include "Element.h"
#include "EmptyClients.h"
#include "FrameLoader.h"
#include "GraphicsContextCairo.h"
#include <JavaScriptCore/InitializeThreading.h>
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "LocalFrameInlines.h"
#include "DocumentView.h"
#include <JavaScriptCore/Options.h>
#include <JavaScriptCore/ConsoleTypes.h>
#include "Page.h"
#include "PageConfiguration.h"
#include "Settings.h"
#include "SharedMemory.h"
#include "SharedBuffer.h"
#ifdef DIHOS_INDEXEDDB_PROVIDER
#include "DatabaseProviderDihOS.h"
#endif
#include "fontconfig/memory.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fcfreetype.h>
#include <cairo.h>
#include <pal/SessionID.h>
#include <wtf/FixedVector.h>
#include <wtf/CompletionHandler.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>
#include <wtf/text/AtomString.h>
#include <variant>
#include <vector>
#ifdef DIHOS_PAGE_NAVIGATION
#include "navigation_strategies.h"
#include "navigation_client.h"
#include "navigation_page.h"
#include "navigation_history.h"
#include "browser_diagnostics.h"
#include "SessionCookiesDihOS.h"
#include "EmptyClients.h"
#else
#include "headless_page_strategies.h"
#endif
#ifdef DIHOS_PAGE_WINDOW
#include "../../sdk/sacx/include/sacx_user_window.h"
#include "EventHandler.h"
#include "HandleUserInputEventResult.h"
#include "ScrollingCoordinatorTypes.h"
#include "FocusController.h"
#include "PlatformMouseEvent.h"
#include "PlatformWheelEvent.h"
using namespace WebCore;
using namespace WTF;

#ifdef DIHOS_VIEW_TRANSITION_FIXTURE
#include "view_transition_fixture.h"
#elif defined(DIHOS_MSE_TEST_FIXTURE)
#include "media/page_mse_fixture.h"
#elif defined(DIHOS_VIDEO_TEST_FIXTURE)
#include "media/page_video_fixture.h"
#endif
#ifdef DIHOS_PAGE_NAVIGATION
class NavigationChrome final:public EmptyChromeClient {
public:
    explicit NavigationChrome(DihosNavigation::NavigationStatus& status):m_status(status){}
    void invalidateContentsAndRootView(const IntRect& rect) final {m_status.repaint.add(rect.x(),rect.y(),rect.width(),rect.height());}
    void addMessageToConsole(JSC::MessageSource,JSC::MessageLevel level,const String& message,unsigned lineNumber,unsigned,const String& sourceURL) final {
        if(level!=JSC::MessageLevel::Error && level!=JSC::MessageLevel::Warning)return;
        auto messageBytes=message.substring(0,350).utf8(),source=sourceURL.substring(0,100).utf8();char detail[512];
        std::snprintf(detail,sizeof(detail),"%s [%s:%u]",messageBytes.data(),source.data(),lineNumber);
        DihosNavigation::browserDiagnostics.console(level==JSC::MessageLevel::Error,detail);
        if(m_consoleMessages++>=32)return;
        auto bytes=message.substring(0,512).utf8();char line[320]="WebCore console: ";unsigned at=17;
        for(unsigned i=0;i<bytes.length() && at+1<sizeof(line);++i){unsigned char c=bytes.data()[i];line[at++]=c<32||c==127?' ':c;}
        line[at]=0;sacx_user_log(line);
    }
private:
    DihosNavigation::NavigationStatus& m_status;
    unsigned m_consoleMessages{};
};
#endif

class WindowMouseEvent final : public PlatformMouseEvent {
public:
    WindowMouseEvent(const sacx_user_window_event& e,PlatformEvent::Type type)
        : PlatformMouseEvent(DoublePoint(e.x,e.y),DoublePoint(e.x,e.y),
            type==PlatformEvent::Type::MouseMoved?MouseButton::None:MouseButton::Left,
            type,1,{},MonotonicTime::fromRawSeconds(e.time_ms/1000.0),0,
            SyntheticClickType::NoTap,MouseEventInputSource::UserDriven)
    {m_buttons=e.buttons&1;}
};
#endif

extern "C" const unsigned char font_sans[],font_sans_end[],font_serif[],font_serif_end[],font_mono[],font_mono_end[];
using namespace WebCore;
using namespace WTF;
static void check(bool ok,const char* why){if(!ok)sacx_user_exit(180,why);}

class MemoryFonts {
public:
    MemoryFonts(){
        check(!FT_Init_FreeType(&library),"Page: native font library");
        config=dihos_fc_config_create_memory();check(config && FcConfigSetCurrent(config),"Page: native memory font config");
        check(FcInit() && !dihos_fc_io_denials(),"Page: Fontconfig initialization reused memory config");
        const unsigned char* starts[]={font_sans,font_serif,font_mono};
        const unsigned char* ends[]={font_sans_end,font_serif_end,font_mono_end};
        const char* ids[]={"/DihOS/MemoryFonts/Sans.ttf","/DihOS/MemoryFonts/Serif.ttf","/DihOS/MemoryFonts/Mono.ttf"};
        for(unsigned i=0;i<3;++i){
            check(!FT_New_Memory_Face(library,starts[i],ends[i]-starts[i],0,&faces[i]),"Page: packaged font face");
            auto* pattern=FcFreeTypeQueryFace(faces[i],reinterpret_cast<const FcChar8*>(ids[i]),0,nullptr);
            check(pattern && FcPatternAddFTFace(pattern,FC_FT_FACE,faces[i]) && dihos_fc_config_add_pattern(config,pattern),"Page: font metadata registration");
            FcPatternDestroy(pattern);
        }
        const char rules[]="<fontconfig><alias binding='strong'><family>sans-serif</family><prefer><family>Noto Sans</family></prefer></alias><alias binding='strong'><family>serif</family><prefer><family>Noto Serif</family></prefer></alias><alias binding='strong'><family>monospace</family><prefer><family>Noto Sans Mono</family></prefer></alias></fontconfig>";
        check(FcConfigParseAndLoadFromMemory(config,reinterpret_cast<const FcChar8*>(rules),FcTrue),"Page: font alias rules");
        check(!dihos_fc_io_denials(),"Page: memory font setup attempted I/O");
    }
    // Process-lifetime backing: WebCore/JSC may retain font-cache references
    // after the local Page is released. OS process teardown owns final cleanup.
    FT_Library library{};
    FT_Face faces[3]{};
    FcConfig* config{};
};

#ifdef DIHOS_IMAGE_TEST_FIXTURE
#include "image_decoder_fixture.h"
#endif
extern "C" int dihos_user_main(const sacx_api* api){
    JSC::initialize([]{JSC::Options::AllowUnfinalizedAccessScope scope;JSC::Options::useConcurrentGC()=false;});
    initializeMainThread();
#ifdef DIHOS_IMAGE_TEST_FIXTURE
    checkImageDecoder(api);
#endif
    // Strategies outlive Page teardown. No external request is admitted.
#ifdef DIHOS_PAGE_NAVIGATION
    static NeverDestroyed<DihosNavigation::NavigationStrategies> strategies;
#else
    static NeverDestroyed<DihosHeadless::LocalPageStrategies> strategies;
#endif
    setPlatformStrategies(&strategies.get());
    MemoryFonts fonts;
    api->log("Native Page: fonts and main thread ready");
    unsigned dnsCompletions=0;
    resolveDNS("fixture.invalid"_s,1,[&](DNSAddressesOrError&& result){
        check(!result && result.error()==DNSError::CannotResolve,"Page: unsupported direct DNS rejects lookup");
        ++dnsCompletions;
    });
    stopResolveDNS(1);
    prefetchDNS("fixture.invalid"_s);
    check(dnsCompletions==1,"Page: direct DNS completes exactly once");
    {
        auto memory=SharedMemory::allocate(8193);check(!!memory,"Page: owned backing allocation");
        auto bytes=memory->mutableSpan();check(bytes.size()==8193,"Page: owned backing bounds");
        for(auto byte:bytes)check(!byte,"Page: owned backing initially zero");
        bytes[8192]=0x5a;
        auto buffer=memory->createSharedBuffer(bytes.size());
        auto copy=SharedMemory::copyBuffer(buffer.get());
        check(copy && copy->span()[8192]==0x5a,"Page: owned backing copy");
        copy->mutableSpan()[8192]=0xa5;
        check(memory->span()[8192]==0x5a,"Page: backing copy independence");
        check(!memory->createHandle(SharedMemory::Protection::ReadOnly)
            && !SharedMemoryHandle::createVMShare(memory->span(),SharedMemory::Protection::ReadWrite),"Page: unavailable IPC handles rejected");
    }
    api->log("Native Page: owned storage, zeroing, copy and IPC rejection OK");
    {
        // A real Page identifier is required for the native media-session
        // manager and other Page-scoped services, including play() admission.
        auto configuration=pageConfigurationWithEmptyClients(PageIdentifier::generate(),PAL::SessionID::defaultSessionID());
#ifdef DIHOS_INDEXEDDB_PROVIDER
        configuration.databaseProvider=DihOSWeb::createDatabaseProvider();
#endif
        auto* local=std::get_if<PageConfiguration::LocalMainFrameCreationParameters>(&configuration.mainFrameCreationParameters);
        check(local,"Page: expected local main frame");
        // Allow inline script only in this owned in-memory test. The process
        // has no network/file grant; no policy for remote content is implied.
        local->effectiveSandboxFlags={};
#ifdef DIHOS_PAGE_NAVIGATION
        DihosNavigation::NavigationStatus navigation;
        configuration.chromeClient=makeUniqueRef<NavigationChrome>(navigation);
        configuration.backForwardClient=DihosNavigation::NavigationHistory::create();
        configuration.cookieJar=DihOSWeb::createSessionCookieJar();
        local->clientCreator=[&navigation](LocalFrame& frame,FrameLoader& loader)->UniqueRef<LocalFrameLoaderClient>{
            return makeUniqueRefWithoutRefCountedCheck<DihosNavigation::NavigationClient>(frame,loader,navigation);
        };
#endif
        api->log("Native Page: creating local Page");
        auto page=Page::create(WTF::move(configuration));
        // This port paints through Cairo into an owned software surface.
        // Never advertise a GPU compositor whose GraphicsLayer factory is
        // unavailable; ordinary transforms/fixed layers must paint in software.
        page->settings().setAcceleratedCompositingEnabled(false);
        page->settings().setAcceleratedCompositingForFixedPositionEnabled(false);
        page->settings().setScriptEnabled(true);
        page->settings().setCookieStoreAPIEnabled(false);
        page->settings().setImagesEnabled(true);
        page->settings().setLoadsImagesAutomatically(true);
#if ENABLE(MEDIA_SOURCE)
        page->settings().setMediaSourceEnabled(true);
        page->settings().setManagedMediaSourceEnabled(false);
#endif
        page->settings().setStandardFontFamily("Noto Serif"_s);
        page->settings().setSerifFontFamily("Noto Serif"_s);
        page->settings().setSansSerifFontFamily("Noto Sans"_s);
        page->settings().setFixedFontFamily("Noto Sans Mono"_s);
        auto& frame=downcast<LocalFrame>(page->mainFrame());
        api->log("Native Page: initializing frame");
        constexpr unsigned width=640,height=480,guard=16;
        // DocumentWriter::begin requires an existing view. Empty clients do
        // not create a platform view during transitionToCommittedForNewPage.
#ifdef DIHOS_PAGE_EMBEDDED
        frame.createView(IntSize(width,height),Color::white,IntSize(),false,ScrollbarMode::Auto,false,ScrollbarMode::Auto,false);
#else
        frame.createView(IntSize(width,height),Color::white,IntSize(),false,ScrollbarMode::AlwaysOff,true,ScrollbarMode::AlwaysOff,true);
#endif
        frame.init();
#ifdef DIHOS_VIEW_TRANSITION_FIXTURE
        return runDihosViewTransitionFixture(frame,page.get(),api);
#elif defined(DIHOS_VIDEO_TEST_FIXTURE)
        return runDihosVideoPageFixture(frame,page.get(),api);
#endif
#ifdef DIHOS_PAGE_NAVIGATION
        page->settings().setCookieEnabled(true);
#ifdef DIHOS_PAGE_EMBEDDED
        int result=DihosNavigation::runEmbeddedBrowser(frame,page.get(),api,navigation);
#else
        int result=DihosNavigation::runNavigationPage(frame,page.get(),api,navigation);
#endif
        check(!dihos_fc_io_denials(),"Navigation: fonts attempted filesystem I/O");
        return result;
#else
        RefPtr document=frame.document();check(!!document,"Page: initial document");
        auto opened=document->open();check(!opened.hasException(),"Page: document.open");
        FixedVector<String> chunks { "<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:white;font-family:sans-serif}h1{margin:0;font-size:24px;line-height:32px}#box{width:160px;height:64px;background:#2468ac}</style><h1 id=title>initial</h1><div id=box onclick=\"document.getElementById('title').textContent='clicked in DihOS';this.style.backgroundColor='#24ac68'\"></div><script>document.getElementById('title').textContent='native DOM + JS';document.getElementById('box').style.width='192px';</script>"_s };
        api->log("Native Page: parsing owned HTML");
        auto written=document->write(document.get(),WTF::move(chunks));check(!written.hasException(),"Page: document.write");document->close();
        for(unsigned i=0;i<32;++i)RunLoop::cycle();
        RefPtr title=document->getElementById(AtomString("title"_s));
        check(title && title->textContent()=="native DOM + JS"_s,"Page: inline JavaScript DOM mutation");
        RefPtr box=document->getElementById(AtomString("box"_s));check(!!box,"Page: parsed element");
        auto* view=frame.view();check(view,"Page: local frame view");view->updateLayoutAndStyleIfNeededRecursive();
        auto bounds=box->getBoundingClientRect();
        check(bounds->width()==192 && bounds->height()==64,"Page: CSS layout after JavaScript style mutation");
        api->log("Native Page: HTML, JavaScript DOM mutation and CSS layout OK");
        std::vector<uint32_t> pixels(width*height+2*guard,0xdeadbeef);
        // Text ink must differ from a known white image, not an unpainted
        // guard value. Guards remain outside the image's writable span.
        std::fill(pixels.begin()+guard,pixels.end()-guard,0xffffffff);
        auto* surface=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels.data()+guard),CAIRO_FORMAT_ARGB32,width,height,width*4);
        check(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"Page: owned image surface");
        api->log("Native Page: painting local view");
        {GraphicsContextCairo context(surface);view->paintContents(context,IntRect(0,0,width,height));}
        cairo_surface_flush(surface);
        check(bounds->x()>=0 && bounds->x()<width-16 && bounds->y()>=0 && bounds->y()<height-16,"Page: finite visible box origin");
        unsigned x=static_cast<unsigned>(bounds->x())+16,y=static_cast<unsigned>(bounds->y())+16;
        check(x<width && y<height && pixels[guard+y*width+x]==0xff2468ac,"Page: CSS background pixel");
        unsigned textInk=0;for(unsigned row=0;row<32;++row)for(unsigned col=0;col<width;++col)textInk+=pixels[guard+row*width+col]!=0xffffffff;
        check(textInk>100 && textInk<width*32/2 && pixels[guard+width-1]==0xffffffff,"Page: painted native text on white background");
        for(unsigned i=0;i<guard;++i)check(pixels[i]==0xdeadbeef && pixels[pixels.size()-1-i]==0xdeadbeef,"Page: surface bounds");
#ifdef DIHOS_PAGE_WINDOW
        sacx_user_window_desc description={width,height,"WebCore native: click blue box"};
        int64_t window=sacx_user_window_create(&description);check(window>0,"Page: create owned window");
        page->focusController().setActive(true);page->focusController().setFocused(true);
        auto present=[&]{
            for(unsigned offset=0;offset<width*height;){unsigned n=width*height-offset;if(n>SACX_USER_WINDOW_WRITE_MAX)n=SACX_USER_WINDOW_WRITE_MAX;
                check(!sacx_user_window_write(window,pixels.data()+guard+offset,offset,n),"Page: checked window upload");offset+=n;}
            check(!sacx_user_window_present(window),"Page: publish completed window frame");
        };
        present();api->log("Native Page: window frame presented; awaiting input");
        bool closed=false;unsigned held=0;
        while(!closed){
            bool dirty=false;
            for(unsigned count=0;count<16;++count){
                sacx_user_window_event event={};int rc=sacx_user_window_event_next(window,&event);
                if(rc==-11)break;check(!rc,"Page: checked window event");
                if(event.type==SACX_WINDOW_CLOSE){closed=true;break;}
                if(event.type==SACX_WINDOW_RESET){if(held){event.buttons=0;event.x=event.y=-1;WindowMouseEvent mouse(event,PlatformEvent::Type::MouseReleased);frame.eventHandler().handleMouseReleaseEvent(mouse);}held=0;continue;}
                if(event.type==SACX_WINDOW_WHEEL){
                    PlatformWheelEvent wheel(IntPoint(event.x,event.y),IntPoint(event.x,event.y),0,event.delta*40.0f,0,event.delta,PlatformWheelEventGranularity::ScrollByPixelWheelEvent,false,false,false,false);
                    frame.eventHandler().handleWheelEvent(wheel,{WheelEventProcessingSteps::SynchronousScrolling,WheelEventProcessingSteps::BlockingDOMEventDispatch});dirty=true;continue;
                }
                auto type=event.type==SACX_WINDOW_DOWN?PlatformEvent::Type::MousePressed:event.type==SACX_WINDOW_UP?PlatformEvent::Type::MouseReleased:PlatformEvent::Type::MouseMoved;
                WindowMouseEvent mouse(event,type);
                if(event.type==SACX_WINDOW_DOWN){held=1;frame.eventHandler().handleMousePressEvent(mouse);}
                else if(event.type==SACX_WINDOW_UP){held=0;frame.eventHandler().handleMouseReleaseEvent(mouse);}
                else if(event.type==SACX_WINDOW_MOVE)frame.eventHandler().handleMouseMoveEvent(mouse);
                dirty=true;
            }
            RunLoop::cycle();
            if(dirty){view->updateLayoutAndStyleIfNeededRecursive();{GraphicsContextCairo context(surface);view->paintContents(context,IntRect(0,0,width,height));}cairo_surface_flush(surface);present();}
            if(!closed)(void)sacx_user_call(SACX_USER_SLEEP_MS,8,0,0);
        }
        check(!sacx_user_window_close(window),"Page: close owned window");
        check(title->textContent()=="clicked in DihOS"_s,"Page: queued pointer click executed DOM JavaScript");
        check(pixels[guard+48*width+16]==0xff24ac68,"Page: input changed rendered CSS pixels");
#endif
#ifdef DIHOS_PAGE_CAPTURE
        // Diagnostic-only export over the checked log syscall. Every chunk
        // carries an index so repeated white pixels cannot be coalesced by
        // the runtime harness. No filesystem or surface privilege is added.
        api->log("PAGE_RGB_BEGIN 640 480");
        constexpr char hex[]="0123456789abcdef";
        for(unsigned first=0;first<width*height;first+=40){
            char chunk[254]={'P','I','X',':'};
            for(unsigned i=0;i<8;++i)chunk[4+i]=hex[(first>>(28-4*i))&15];
            chunk[12]=':';unsigned cursor=13;
            for(unsigned p=first;p<first+40 && p<width*height;++p){
                uint32_t pixel=pixels[guard+p];
                for(int shift=20;shift>=0;shift-=4)chunk[cursor++]=hex[(pixel>>shift)&15];
            }
            chunk[cursor]=0;api->log(chunk);
        }
        api->log("PAGE_RGB_END");
#endif
        cairo_surface_destroy(surface);
        check(!dihos_fc_io_denials(),"Page: font rendering attempted I/O");
#endif
    }
#ifdef DIHOS_PAGE_WINDOW
    api->log("SACX native Page window: checked frames, input JavaScript and close OK");return 120;
#else
    api->log("SACX native Page: local HTML, DOM JavaScript, layout and offscreen painting OK");return 115;
#endif
}
