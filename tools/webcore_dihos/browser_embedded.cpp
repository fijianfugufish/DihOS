#include "config.h"
#include "navigation_page.h"
#include "navigation_input.h"
#include "browser_diagnostics.h"
#include "ResourceLoad.h"
#include "error_page.h"
#include "SubstituteData.h"
#include "SharedBuffer.h"
#include "../../sdk/sacx/include/sacx_browser_session.h"
#include "Page.h"
#include "Settings.h"
#include "LocalFrameView.h"
#include "DocumentView.h"
#include "Document.h"
#include "FrameLoadRequest.h"
#include "BackForwardController.h"
#include "GraphicsContextCairo.h"
#include "PlatformMouseEvent.h"
#include "PlatformWheelEvent.h"
#include "HandleUserInputEventResult.h"
#include "ScrollingCoordinatorTypes.h"
#include "FocusController.h"
#include "HitTestResult.h"
#include <wtf/RunLoop.h>
#include <cairo.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <sdk/sacx_user_resources.h>
namespace DihosNavigation {
using namespace WebCore;using namespace WTF;
static int bridge(uint32_t op,void* data,uint32_t bytes){return (int)sacx_user_call(SACX_USER_BROWSER_SESSION,op,(uintptr_t)data,bytes);}
static void checked(bool ok,const char* why){if(!ok)sacx_user_exit(182,why);}
class EmbeddedPointer final:public PlatformMouseEvent {
public:EmbeddedPointer(int x,int y,unsigned buttons,PlatformEvent::Type type)
 :PlatformMouseEvent(DoublePoint(x,y),DoublePoint(x,y),type==PlatformEvent::Type::MouseMoved?MouseButton::None:MouseButton::Left,
 type,1,{},MonotonicTime::now(),0,SyntheticClickType::NoTap,MouseEventInputSource::UserDriven){m_buttons=buttons&1;}
};
static void copy(char* out,size_t cap,const CString& string){size_t n=std::min(cap-1,string.length());for(size_t i=0;i<n;++i)out[i]=string.data()[i];out[n]=0;}
int runEmbeddedBrowser(LocalFrame& frame,Page& page,const sacx_api* api,NavigationStatus& status){
 sacx_browser_info info={};auto deadline=MonotonicTime::now()+10_s;int rc;
 while((rc=bridge(SACX_BROWSER_DESCRIBE,&info,sizeof(info)))==-9 && MonotonicTime::now()<deadline)
  (void)sacx_user_call(SACX_USER_SLEEP_MS,8,0,0);
 checked(!rc,"Dihscover: frontend connection unavailable");
 uint64_t window=0;cairo_surface_t* surface=nullptr;std::vector<uint32_t> pixels,lastPixels,regionPixels;
 sacx_user_info abi{};
 bool regionUploads=!sacx_user_call(SACX_USER_QUERY,(uintptr_t)&abi,sizeof(abi),0) && (abi.operations&(1ull<<SACX_USER_WINDOW_REGION));
 if(regionUploads)regionPixels.reserve(SACX_USER_WINDOW_WRITE_MAX);
 auto resize=[&](unsigned w,unsigned h){
  if(window){checked(!sacx_user_window_close(window),"Dihscover: release old page surface");window=0;}
  if(surface)cairo_surface_destroy(surface);
  pixels.assign(w*h,0xffffffff);lastPixels.clear();sacx_user_window_desc d={w,h,"Dihscover WebCore page"};
  int64_t created=sacx_user_window_create(&d);checked(created>0,"Dihscover: attach page to existing viewport");window=created;
  surface=cairo_image_surface_create_for_data((unsigned char*)pixels.data(),CAIRO_FORMAT_ARGB32,w,h,w*4);
  checked(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS,"Dihscover: page pixels");
  frame.view()->resize(w,h);frame.view()->setNeedsLayoutAfterViewConfigurationChange();info.width=w;info.height=h;status.repaint.resize(w,h);status.dirty=true;
 };
 page.focusController().setActive(true);page.focusController().setFocused(true);
 resize(info.width,info.height);
 bool loading=false,held=false,havePointer=false,pointerChanged=false;int pointerX=-1,pointerY=-1;
 String diagnosticText;
 unsigned completed=0,failed=0;auto lastPaint=MonotonicTime::now(),lastReport=lastPaint,lastWheel=lastPaint;
 BrowserWheel pendingWheel;
 uint64_t frames=0,lastCpuUs=0,lastWallMs=0,lastUploadCalls=0,lastPaintedPixels=0,lastUploadedPixels=0;
 auto cpu=[](){sacx_user_resources resources{};return sacx_user_resources_get(&resources)?0:resources.process_user_ns+resources.process_kernel_ns;};
 auto lastScroll=frame.view()->scrollPosition();
 auto mouse=[&](const sacx_browser_command& c){
  unsigned buttons=c.c&1;
  bool moved=!havePointer || c.a!=pointerX || c.b!=pointerY;
  if(moved){EmbeddedPointer move(c.a,c.b,buttons,PlatformEvent::Type::MouseMoved);frame.eventHandler().handleMouseMoveEvent(move);}
  if((bool)buttons!=held)status.dirty=true;
  if(buttons && !held){EmbeddedPointer press(c.a,c.b,buttons,PlatformEvent::Type::MousePressed);frame.eventHandler().handleMousePressEvent(press);}
  if(!buttons && held){EmbeddedPointer release(c.a,c.b,0,PlatformEvent::Type::MouseReleased);frame.eventHandler().handleMouseReleaseEvent(release);}
  held=buttons;
  if(moved || !havePointer){auto scroll=frame.view()->scrollPosition();
   auto hit=frame.eventHandler().hitTestResultAtPoint(LayoutPoint(c.a+scroll.x(),c.b+scroll.y()),{HitTestRequest::Type::ReadOnly});
   unsigned pointer=hit.isOverLink()?11:hit.isContentEditable() || hit.isOverTextInsideFormControlElement()?1:0;
   pointerChanged|=info.pointer!=pointer;info.pointer=pointer;
  }
  havePointer=true;pointerX=c.a;pointerY=c.b;if(c.d)pendingWheel.add(c.d);
 };
 for(;;){
  for(unsigned n=0;n<16;++n){
   sacx_browser_command c={};rc=bridge(SACX_BROWSER_RECEIVE,&c,sizeof(c));
   if(rc==-11)break;if(rc==-9)goto closed;checked(!rc,"Dihscover: receive frontend command");
   switch(c.type){
   case SACX_BROWSER_NAVIGATE:{
    URL url{{},String::fromUTF8(c.text)};
    if(!url.isValid() || !url.protocolIsInHTTPFamily() || !url.user().isEmpty() || !url.password().isEmpty()){status.error=-22;break;}
    browserDiagnostics={};diagnosticText=String();
    pendingWheel.pixels=0;loading=true;status.error=0;frame.loader().load(FrameLoadRequest(frame,ResourceRequest(WTF::move(url))));break;
   }
   case SACX_BROWSER_BACK:loading=page.backForward().goBack();status.error=0;break;
   case SACX_BROWSER_FORWARD:loading=page.backForward().goForward();status.error=0;break;
   case SACX_BROWSER_RELOAD:
    loading=true;
    if(status.error && status.errorURL.protocolIsInHTTPFamily())frame.loader().load(FrameLoadRequest(frame,ResourceRequest(URL(status.errorURL))));
    else frame.loader().reload({},true);
    status.error=0;break;
   case SACX_BROWSER_STOP:frame.loader().stopAllLoaders();loading=false;status.error=0;break;
   case SACX_BROWSER_RESIZE:if((unsigned)c.a!=info.width || (unsigned)c.b!=info.height)resize(c.a,c.b);break;
   case SACX_BROWSER_PREFS:{
    const char* families[]={"Noto Sans","Noto Serif","Noto Sans Mono"};
    page.settings().setStandardFontFamily(String::fromUTF8(families[c.a]));page.settings().setSansSerifFontFamily(String::fromUTF8(families[c.a]));
    page.settings().setSerifFontFamily(String::fromUTF8(families[c.d&255]));page.settings().setFixedFontFamily(String::fromUTF8(families[(c.d>>8)&255]));
    page.settings().setMinimumFontSize(c.b);frame.setPageZoomFactor(c.c/100.0f);break;
   }
   case SACX_BROWSER_MOUSE:mouse(c);break;
   case SACX_BROWSER_KEY:{
    sacx_user_window_event e={};e.type=SACX_WINDOW_KEY_DOWN;e.x=c.a;e.y=c.b;e.delta=c.c;
    e.time_ms=(uint64_t)(MonotonicTime::now().secondsSinceEpoch().value()*1000);documentKey(frame,e);
    e.type=SACX_WINDOW_KEY_UP;documentKey(frame,e);break;
   }
   case SACX_BROWSER_RESET:if(held){sacx_browser_command reset={};reset.a=reset.b=-1;mouse(reset);}break;
   case SACX_BROWSER_DIAGNOSTICS:{char line[256];std::snprintf(line,sizeof(line),"Dihscover paint: frames=%llu CPU_us=%llu wall_ms=%llu painted=%llu uploaded_bytes=%llu uploads=%llu wheel_pending=%d",(unsigned long long)frames,(unsigned long long)lastCpuUs,(unsigned long long)lastWallMs,(unsigned long long)lastPaintedPixels,(unsigned long long)(lastUploadedPixels*4),(unsigned long long)lastUploadCalls,pendingWheel.pixels);api->log(line);
    std::snprintf(line,sizeof(line),"Dihscover diagnostics: console errors=%u warnings=%u request failures=%u",browserDiagnostics.errors,browserDiagnostics.warnings,browserDiagnostics.failures);api->log(line);
    api->log(browserDiagnostics.summary());const char* network=DihOSWeb::ResourceLoad::diagnostics();api->log(network);
    diagnosticText=String::fromUTF8(network);pointerChanged=true;break;}
   }
   if(c.type!=SACX_BROWSER_MOUSE && c.type!=SACX_BROWSER_DIAGNOSTICS)status.dirty=true;
  }
  RunLoop::cycle();
  if(pendingWheel.pixels && MonotonicTime::now()-lastWheel>=16_ms){
   int pixels=pendingWheel.take();PlatformWheelEvent wheel(IntPoint(pointerX,pointerY),IntPoint(pointerX,pointerY),0,pixels,0,pixels/40.0f,PlatformWheelEventGranularity::ScrollByPixelWheelEvent,false,false,false,false);
   frame.eventHandler().handleWheelEvent(wheel,{WheelEventProcessingSteps::SynchronousScrolling,WheelEventProcessingSteps::BlockingDOMEventDispatch});
   status.dirty=true;lastWheel=MonotonicTime::now();
  }
  if(status.errorPagePending){
   // Defer replacement until outside FrameLoader's failure callback.
   status.errorPagePending=false;
   auto url=status.errorURL.string().utf8(),reason=status.errorDescription.utf8();
   auto html=errorPage(std::string_view(url.data(),url.length()),std::string_view(reason.data(),reason.length()),status.error);
   URL local{{},"about:blank"_s};
   ResourceResponse response(URL(local),"text/html"_s,html.size(),"UTF-8"_s);
   SubstituteData data(SharedBuffer::create(std::span{html.data(),html.size()}),URL(status.errorURL),WTF::move(response),SessionHistoryVisibility::Hidden);
   frame.loader().load(FrameLoadRequest(frame,ResourceRequest(WTF::move(local)),WTF::move(data)));
   loading=false;status.dirty=true;
  }
  if(status.completed!=completed || status.failed!=failed){completed=status.completed;failed=status.failed;loading=false;status.dirty=true;}
  auto now=MonotonicTime::now();
  bool scheduled=page.nextRenderingUpdateTimestamp().has_value() && now-lastPaint>=page.preferredRenderingUpdateInterval();
  bool rendered=false;
  // Coalesce animation/style invalidations without delaying command handling.
  if(now-lastPaint>=16_ms && (status.dirty || scheduled || status.repaint.pending || frame.view()->scrollPosition()!=lastScroll)){
   bool full=status.dirty;status.dirty=false;
   auto started=MonotonicTime::now();uint64_t before=cpu();
   // Run WebCore's complete software rendering lifecycle: layout alone does
   // not dispatch resize/scroll events, media-query changes or rAF callbacks.
   page.updateRendering();page.finalizeRenderingUpdate({});
   frame.view()->updateLayoutAndStyleIfNeededRecursive();
   auto scroll=frame.view()->scrollPosition();if(full || scroll!=lastScroll)status.repaint.full();lastScroll=scroll;
   if(status.repaint.pending){auto damage=status.repaint.take();IntRect rect(damage.x,damage.y,damage.w,damage.h);
   cairo_t* cr=cairo_create(surface);cairo_rectangle(cr,damage.x,damage.y,damage.w,damage.h);cairo_clip(cr);cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);cairo_destroy(cr);
   {GraphicsContextCairo context(surface);context.clip(FloatRect(rect));frame.view()->paint(context,rect);}
   cairo_surface_flush(surface);
   BrowserRect uploadDamage=lastPixels.size()!=pixels.size()?damage:browserChangedRect(pixels.data(),lastPixels.data(),info.width,damage);
   bool changed=uploadDamage.w>0 && uploadDamage.h>0;
   lastUploadCalls=0;lastUploadedPixels=0;
   if(changed){
    unsigned regionArea=(unsigned)uploadDamage.w*uploadDamage.h;
    if(regionUploads && lastPixels.size()==pixels.size() && regionArea<=SACX_USER_WINDOW_WRITE_MAX){
     regionPixels.resize(regionArea);
     for(int y=0;y<uploadDamage.h;++y)memcpy(regionPixels.data()+y*uploadDamage.w,pixels.data()+(uploadDamage.y+y)*info.width+uploadDamage.x,uploadDamage.w*4);
     sacx_user_window_region r={(unsigned)uploadDamage.x,(unsigned)uploadDamage.y,(unsigned)uploadDamage.w,(unsigned)uploadDamage.h};
     checked(!sacx_user_window_region_begin(window,&r),"Dihscover: begin page damage");
     checked(!sacx_user_window_write(window,regionPixels.data(),0,regionArea),"Dihscover: upload page damage");lastUploadCalls=1;lastUploadedPixels=regionArea;
    }else for(unsigned at=0;at<pixels.size();){unsigned count=std::min<unsigned>(SACX_USER_WINDOW_WRITE_MAX,pixels.size()-at);
     checked(!sacx_user_window_write(window,pixels.data()+at,at,count),"Dihscover: checked page upload");at+=count;++lastUploadCalls;lastUploadedPixels+=count;}
    checked(!sacx_user_window_present(window),"Dihscover: present page in existing UI");
    if(lastPixels.size()!=pixels.size())lastPixels=pixels;
    else for(int y=damage.y;y<damage.y+damage.h;++y){unsigned at=y*info.width+damage.x;memcpy(lastPixels.data()+at,pixels.data()+at,damage.w*4);}
   }
   ++frames;lastPaintedPixels=(uint64_t)damage.w*damage.h;
   }
   uint64_t after=cpu();lastCpuUs=after>=before?(after-before)/1000:0;lastWallMs=(MonotonicTime::now()-started).milliseconds();
   lastPaint=MonotonicTime::now();rendered=true;
  }
  if(rendered || pointerChanged || now-lastReport>250_ms){
   info.state=status.error?SACX_BROWSER_FAILED:loading?SACX_BROWSER_LOADING:SACX_BROWSER_READY;info.completed=completed;info.failed=failed;
   copy(info.url,sizeof(info.url),frame.document()->url().string().utf8());copy(info.title,sizeof(info.title),frame.document()->title().utf8());
   if(status.error && !status.errorURL.isEmpty())copy(info.url,sizeof(info.url),status.errorURL.string().utf8());
   info.error[0]=0;if(status.error){const char* error=status.error==-13?"HTTPS authentication failed":status.error==-22?"Invalid address":"WebCore load failed; see terminal";
    unsigned i=0;for(;error[i] && i+1<sizeof(info.error);++i)info.error[i]=error[i];info.error[i]=0;}
   if(status.error && !status.errorDescription.isEmpty())copy(info.error,sizeof(info.error),status.errorDescription.utf8());
   else if(!diagnosticText.isEmpty())copy(info.error,sizeof(info.error),diagnosticText.utf8());
   rc=bridge(SACX_BROWSER_REPORT,&info,sizeof(info));if(rc==-9)goto closed;checked(!rc,"Dihscover: report page state");lastReport=MonotonicTime::now();pointerChanged=false;
  }
  (void)sacx_user_call(SACX_USER_SLEEP_MS,8,0,0);
 }
closed:
 frame.loader().stopAllLoaders();if(window)(void)sacx_user_window_close(window);if(surface)cairo_surface_destroy(surface);
 return 123;
}
}
