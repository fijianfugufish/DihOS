#include "config.h"
#include "navigation_page.h"
#include "navigation_controls.h"
#include "navigation_input.h"
#include "../../sdk/sacx/include/sacx_user_window.h"
#include "Page.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "LocalFrameInlines.h"
#include "DocumentView.h"
#include "Document.h"
#include "FrameLoadRequest.h"
#include "EventHandler.h"
#include "HandleUserInputEventResult.h"
#include "PlatformMouseEvent.h"
#include "PlatformKeyboardEvent.h"
#include "BackForwardController.h"
#include "Settings.h"
#include "Editor.h"
#include "PlatformWheelEvent.h"
#include "ScrollingCoordinatorTypes.h"
#include "FocusController.h"
#include "GraphicsContextCairo.h"
#include <wtf/RunLoop.h>
#include <cairo.h>
#include <vector>

namespace DihosNavigation {
using namespace WebCore;
using namespace WTF;
static void check(bool ok,const char* why){if(!ok)sacx_user_exit(181,why);}
class Pointer final : public PlatformMouseEvent {
public:
    Pointer(const sacx_user_window_event& event,PlatformEvent::Type type)
        :PlatformMouseEvent(DoublePoint(event.x,event.y-36),DoublePoint(event.x,event.y),
            type==PlatformEvent::Type::MouseMoved?MouseButton::None:MouseButton::Left,
            type,1,{},MonotonicTime::fromRawSeconds(event.time_ms/1000.0),0,
            SyntheticClickType::NoTap,MouseEventInputSource::UserDriven){m_buttons=event.buttons&1;}
};
int runNavigationPage(LocalFrame& frame,Page& page,const sacx_api* api,NavigationStatus& status)
{
    constexpr unsigned width=640,height=480,toolbar=36,contentHeight=height-toolbar;
    frame.view()->resize(width,contentHeight);
    sacx_user_window_desc description={width,height,"Dihscover WebCore: native browsing preview"};
    int64_t window=sacx_user_window_create(&description);check(window>0,"Navigation: create owned window");
    page.focusController().setActive(true);page.focusController().setFocused(true);
    std::vector<uint32_t> pixels(width*height,0xffffffff);
    AddressControl address;std::string displayedURL;unsigned fontChoice=0;float zoom=1;
    auto* surface=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels.data()),CAIRO_FORMAT_ARGB32,width,height,width*4);
    check(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"Navigation: owned pixels");
    auto navigate=[&](URL&& url){
        status.dirty=true;status.error=0;address.blur();address.set(url.string().utf8().data());
        frame.loader().load(FrameLoadRequest(frame,ResourceRequest(WTF::move(url))));
    };
    auto go=[&]{
        char normalized[768];
        if(dihscover_address_normalize(address.text.c_str(),normalized,sizeof(normalized))){status.error=-22;status.dirty=true;return;}
        URL url{{},String::fromUTF8(normalized)};
        if(!url.isValid() || !url.protocolIsInHTTPFamily() || !url.user().isEmpty() || !url.password().isEmpty()){
            status.error=-22;status.dirty=true;return;
        }
        navigate(WTF::move(url));
    };
    auto action=[&](unsigned hit){
        status.error=0;status.dirty=true;
        switch(hit){
        case 1:address.blur();page.backForward().goBack();break;
        case 2:address.blur();page.backForward().goForward();break;
        case 3:address.blur();frame.loader().reload({},true);break;
        case 4:frame.loader().stopAllLoaders();break;
        case 5:address.focus();break;
        case 6:{const char* families[]={"Noto Serif","Noto Sans","Noto Sans Mono"};fontChoice=(fontChoice+1)%3;
            page.settings().setStandardFontFamily(String::fromUTF8(families[fontChoice]));break;}
        case 7:go();break;
        }
    };
    auto hitTest=[&](int x,int y)->unsigned{
        if(y<0 || y>=static_cast<int>(toolbar) || x<0 || x>=static_cast<int>(width))return 0;
        if(x<32)return 1;if(x<64)return 2;if(x<96)return 3;if(x<132)return 4;
        if(x<544)return 5;if(x<584)return 6;return 7;
    };
    auto paint=[&]{
        auto url=frame.document()->url().string().utf8();
        if(displayedURL!=url.data()){
            displayedURL=url.data();
            if(!address.focused && !displayedURL.empty() && displayedURL!="about:blank")address.set(displayedURL.c_str());
        }
        cairo_t* cr=cairo_create(surface);cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);
        cairo_save(cr);cairo_rectangle(cr,0,toolbar,width,contentHeight);cairo_clip(cr);cairo_translate(cr,0,toolbar);
        if(auto* view=frame.view()){
            view->updateLayoutAndStyleIfNeededRecursive();
            cairo_surface_t* viewSurface=cairo_surface_create_for_rectangle(surface,0,toolbar,width,contentHeight);
            {GraphicsContextCairo context(viewSurface);view->paintContents(context,IntRect(0,0,width,contentHeight));}
            cairo_surface_destroy(viewSurface);
        }
        cairo_restore(cr);
        cairo_set_source_rgb(cr,0.12,0.15,0.18);cairo_rectangle(cr,0,0,width,toolbar);cairo_fill(cr);
        cairo_select_font_face(cr,"Noto Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,13);
        cairo_set_source_rgb(cr,1,1,1);
        const char* labels[]={"<",">","R","X","Aa","Go"};const int positions[]={10,42,74,108,552,594};
        for(unsigned i=0;i<6;++i){cairo_move_to(cr,positions[i],23);cairo_show_text(cr,labels[i]);}
        cairo_set_source_rgb(cr,address.focused?1:0.94,address.focused?1:0.95,1);
        cairo_rectangle(cr,136,4,404,28);cairo_fill(cr);
        cairo_save(cr);cairo_rectangle(cr,140,5,396,26);cairo_clip(cr);
        cairo_text_extents_t extents;std::string before=address.text.substr(0,address.caret);
        cairo_text_extents(cr,before.c_str(),&extents);double origin=142;
        if(address.focused && extents.x_advance>386)origin-=extents.x_advance-386;
        if(address.focused && address.anchor!=address.caret){
            auto left=address.text.substr(0,std::min(address.anchor,address.caret));
            auto right=address.text.substr(0,std::max(address.anchor,address.caret));
            cairo_text_extents_t a,b;cairo_text_extents(cr,left.c_str(),&a);cairo_text_extents(cr,right.c_str(),&b);
            cairo_set_source_rgb(cr,0.65,0.8,1);cairo_rectangle(cr,origin+a.x_advance,7,b.x_advance-a.x_advance,22);cairo_fill(cr);
        }
        cairo_set_source_rgb(cr,0.08,0.1,0.12);cairo_move_to(cr,origin,23);cairo_show_text(cr,address.text.c_str());
        if(address.focused){cairo_rectangle(cr,origin+extents.x_advance,8,1,20);cairo_fill(cr);}
        cairo_restore(cr);
        if(status.error){
            cairo_set_source_rgb(cr,0.65,0.05,0.05);cairo_move_to(cr,8,toolbar+18);
            cairo_show_text(cr,status.error==-13?"HTTPS authentication failed":status.error==-22?"Invalid or unsupported address":"Load failed; see terminal");
        }
        cairo_destroy(cr);cairo_surface_flush(surface);
        for(unsigned offset=0;offset<pixels.size();){unsigned count=std::min<unsigned>(SACX_USER_WINDOW_WRITE_MAX,pixels.size()-offset);
            check(!sacx_user_window_write(window,pixels.data()+offset,offset,count),"Navigation: checked frame upload");offset+=count;}
        check(!sacx_user_window_present(window),"Navigation: completed frame");
    };
#ifdef DIHOS_PAGE_HTTPS
    paint();navigate(URL{{},"https://example.com/"_s});
#else
    paint();navigate(URL{{},"http://example.com/"_s});
#endif
    bool closed=false,held=false;unsigned toolbarPress=0,seenCompleted=0;auto lastPaint=MonotonicTime::now();
    while(!closed){
        for(unsigned n=0;n<16;++n){
            sacx_user_window_event event={};int rc=sacx_user_window_event_next(window,&event);
            if(rc==-11)break;check(!rc,"Navigation: event queue");
            if(event.type==SACX_WINDOW_CLOSE){closed=true;break;}
            if(event.type==SACX_WINDOW_RESET){if(held){event.x=event.y=-1;event.buttons=0;Pointer p(event,PlatformEvent::Type::MouseReleased);frame.eventHandler().handleMouseReleaseEvent(p);}held=false;toolbarPress=0;address.blur();status.dirty=true;continue;}
            if(event.type==SACX_WINDOW_KEY_DOWN || event.type==SACX_WINDOW_KEY_UP){
                bool down=event.type==SACX_WINDOW_KEY_DOWN;unsigned key=event.x,mods=event.delta;
                if(down && (mods&SACX_WINDOW_CTRL) && key==SACX_KEY_L){address.focus();status.dirty=true;continue;}
                if(address.focused){if(down && address.key(key,event.y,mods))go();status.dirty=true;continue;}
                if(down && key==SACX_KEY_F5){action(3);continue;}
                if(down && key==SACX_KEY_ESCAPE){action(4);continue;}
                if(down && (mods&SACX_WINDOW_ALT) && (key==SACX_KEY_LEFT || key==SACX_KEY_RIGHT)){action(key==SACX_KEY_LEFT?1:2);continue;}
                if(down && (mods&SACX_WINDOW_CTRL) && (key==SACX_KEY_EQUAL || key==SACX_KEY_MINUS || key==SACX_KEY_0)){
                    zoom=key==SACX_KEY_0?1:std::clamp(zoom+(key==SACX_KEY_EQUAL?0.1f:-0.1f),0.5f,3.0f);
                    frame.setPageZoomFactor(zoom);status.dirty=true;continue;
                }
                documentKey(frame,event);
                status.dirty=true;continue;
            }
            bool inToolbar=event.y>=0 && event.y<static_cast<int>(toolbar);
            if(toolbarPress || (inToolbar && !held)){
                unsigned hit=hitTest(event.x,event.y);
                if(event.type==SACX_WINDOW_DOWN)toolbarPress=hit;
                if(event.type==SACX_WINDOW_UP){
                    if(hit && hit==toolbarPress){
                        action(hit);
                    }
                    toolbarPress=0;
                }
                continue;
            }
            if(event.type==SACX_WINDOW_WHEEL){
                PlatformWheelEvent wheel(IntPoint(event.x,event.y-toolbar),IntPoint(event.x,event.y),0,event.delta*40.0f,0,event.delta,PlatformWheelEventGranularity::ScrollByPixelWheelEvent,false,false,false,false);
                frame.eventHandler().handleWheelEvent(wheel,{WheelEventProcessingSteps::SynchronousScrolling,WheelEventProcessingSteps::BlockingDOMEventDispatch});
            }else{
                auto type=event.type==SACX_WINDOW_DOWN?PlatformEvent::Type::MousePressed:event.type==SACX_WINDOW_UP?PlatformEvent::Type::MouseReleased:PlatformEvent::Type::MouseMoved;
                Pointer pointer(event,type);
                if(event.type==SACX_WINDOW_DOWN){address.blur();held=true;frame.eventHandler().handleMousePressEvent(pointer);}
                else if(event.type==SACX_WINDOW_UP){held=false;frame.eventHandler().handleMouseReleaseEvent(pointer);}
                else if(event.type==SACX_WINDOW_MOVE)frame.eventHandler().handleMouseMoveEvent(pointer);
            }
            status.dirty=true;
        }
        RunLoop::cycle();
        // Until ChromeClient damage notifications are connected, also refresh
        // timer-driven DOM changes at a bounded diagnostic cadence.
        if(!closed && (status.dirty || MonotonicTime::now()-lastPaint>250_ms)){
            status.dirty=false;paint();lastPaint=MonotonicTime::now();
            if(status.completed!=seenCompleted){seenCompleted=status.completed;api->log("Native navigation: fetched document painted");}
        }
        if(!closed)(void)sacx_user_call(SACX_USER_SLEEP_MS,8,0,0);
    }
    frame.loader().stopAllLoaders();check(!sacx_user_window_close(window),"Navigation: close owned window");
    cairo_surface_destroy(surface);
    api->log("SACX native navigation: window closed and requests canceled");return 122;
}
}
