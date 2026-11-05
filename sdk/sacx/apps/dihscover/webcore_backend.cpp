#include "netsurf_backend.h"
#include "address.h"
#include "preferences.h"
#include "sacx_browser_frontend.h"
#include "sacx_window_keys.h"
#include <stdio.h>
extern "C" {
#include "netsurf/keypress.h"
}
static const sacx_api* api;
static const sacx_browser_frontend_api* bridge;
static dihscover_netsurf_callbacks callbacks;
static sacx_browser_info info;
static uint32_t width=960,height=568,session;
static uint32_t viewport_parent;
static bool starting,stopping;
static uint64_t stop_started;
static char navigation_url[DIHSCOVER_URL_CAP];
static char last_url[768],last_title[128];
static char last_error[128];
static int32_t last_x,last_y;static uint8_t last_buttons,have_mouse;
extern "C" const sacx_api* dihscover_netsurf_api(void){return api;}
static int send(sacx_browser_command command){
 if(!session)return -9;sacx_browser_send request={session,0,command};
 int rc=bridge->browser_session(SACX_BROWSER_SEND,&request,sizeof(request));
 if(rc && callbacks.status_changed)callbacks.status_changed(rc==-11?"Browser busy; input queue recovered":"WebCore command failed");
 return rc;
}
static void preferences(){
 sacx_browser_command c={};c.type=SACX_BROWSER_PREFS;c.a=browser_prefs.sans;c.b=browser_prefs.minimum;c.c=browser_prefs.zoom;
 c.d=browser_prefs.serif|(browser_prefs.mono<<8);(void)send(c);
}
static int open_renderer(){
 if(session)return 0;
 sacx_browser_open open={viewport_parent,width,height,0};int rc=bridge->browser_session(SACX_BROWSER_OPEN,&open,sizeof(open));
 if(rc)return rc;session=open.session;info={};info.session=session;info.state=SACX_BROWSER_STARTING;
 starting=true;stopping=false;have_mouse=0;last_url[0]=last_title[0]=last_error[0]=0;preferences();return 0;
}
static int close_renderer(){
 if(!session)return 0;
 int rc=bridge->browser_session(SACX_BROWSER_CLOSE,&session,sizeof(session));
 if(rc)return rc;
 session=0;starting=stopping=false;have_mouse=0;info={};info.state=SACX_BROWSER_READY;
 if(callbacks.pointer_changed)callbacks.pointer_changed(0);return 0;
}
int dihscover_netsurf_init(const sacx_api* input,uint32_t parent,const dihscover_netsurf_callbacks* cb){
 api=input;bridge=sacx_browser_frontend(input);if(!bridge || !cb)return -1;callbacks=*cb;
 viewport_parent=parent;navigation_url[0]=0;browser_preferences_load(api);return open_renderer();
}
void dihscover_netsurf_shutdown(){(void)close_renderer();}
int dihscover_netsurf_resize(uint32_t w,uint32_t h){
 if(w>SACX_BROWSER_MAX_WIDTH)w=SACX_BROWSER_MAX_WIDTH;if(h>SACX_BROWSER_MAX_HEIGHT)h=SACX_BROWSER_MAX_HEIGHT;if(!w)w=1;if(!h)h=1;
 if(w==width && h==height)return 0;
 if(!session){width=w;height=h;return 0;}
 sacx_browser_command c={};c.type=SACX_BROWSER_RESIZE;c.a=w;c.b=h;int rc=send(c);
 if(!rc){width=w;height=h;}return rc;
}
int dihscover_netsurf_navigate(const char* address){
 sacx_browser_command c={};c.type=SACX_BROWSER_NAVIGATE;
 if(dihscover_address_normalize(address,c.text,sizeof(c.text)))return -22;
 // Replacing a navigation during a pending Stop must not queue behind a
 // renderer which may never service that Stop.
 int rc=stopping?close_renderer():0;if(rc)return rc;
 rc=open_renderer();if(rc)return rc;
 rc=send(c);if(!rc){dihs_copy(navigation_url,sizeof(navigation_url),c.text);info.state=starting?SACX_BROWSER_STARTING:SACX_BROWSER_LOADING;if(callbacks.url_changed)callbacks.url_changed(c.text);}return rc;
}
static int simple(uint32_t type){sacx_browser_command c={};c.type=type;return send(c);}
int dihscover_netsurf_back(){if(!session)return -9;info.state=starting?SACX_BROWSER_STARTING:SACX_BROWSER_LOADING;return simple(SACX_BROWSER_BACK);}
int dihscover_netsurf_forward(){if(!session)return -9;info.state=starting?SACX_BROWSER_STARTING:SACX_BROWSER_LOADING;return simple(SACX_BROWSER_FORWARD);}
int dihscover_netsurf_reload(){if(stopping){int rc=close_renderer();if(rc)return rc;}if(!session)return navigation_url[0]?dihscover_netsurf_navigate(navigation_url):-9;info.state=starting?SACX_BROWSER_STARTING:SACX_BROWSER_LOADING;return simple(SACX_BROWSER_RELOAD);}
void dihscover_netsurf_stop(){
 if(!session)return;
 if(starting){if(close_renderer() && callbacks.status_changed)callbacks.status_changed("Could not cancel browser startup");return;}
 if(!stopping){(void)simple(SACX_BROWSER_STOP);stopping=true;stop_started=api->time_monotonic_ms();}
 info.state=SACX_BROWSER_READY;
}
void dihscover_netsurf_toggle_images(){if(callbacks.status_changed)callbacks.status_changed("WebCore image decoding support is unchanged");}
void dihscover_netsurf_zoom(int delta){
 int value=(int)browser_prefs.zoom+delta;browser_prefs.zoom=value<50?50:value>300?300:value;
 preferences();(void)browser_preferences_save(api);
}
void dihscover_netsurf_cycle_font(){browser_prefs.sans=(browser_prefs.sans+1)%3;preferences();(void)browser_preferences_save(api);}
void dihscover_netsurf_diagnostics(){
 char line[200];snprintf(line,sizeof(line),"Dihscover frontend: session=%u state=%u starting=%u stopping=%u error=%s",session,info.state,starting?1u:0u,stopping?1u:0u,info.error);
 if(api && api->log)api->log(line);if(session)(void)simple(SACX_BROWSER_DIAGNOSTICS);
}
uint32_t dihscover_netsurf_active_fetches(){return info.state==SACX_BROWSER_STARTING || info.state==SACX_BROWSER_LOADING;}
uint32_t dihscover_netsurf_completed_fetches(){return info.completed;}
uint32_t dihscover_netsurf_failed_fetches(){return info.failed;}
uint32_t dihscover_netsurf_page_state(){return info.state==SACX_BROWSER_STARTING?3:dihscover_netsurf_active_fetches()?0:2;}
void dihscover_netsurf_pump(uint64_t now){
 if(!session)return;sacx_browser_info current={};current.session=session;
 if(bridge->browser_session(SACX_BROWSER_STATUS,&current,sizeof(current)))return;
 if(current.state==SACX_BROWSER_EXITED || (current.state==SACX_BROWSER_FAILED &&
     (dihs_streq(current.error,"WebCore renderer faulted; close and reopen Dihscover") ||
      dihs_streq(current.error,"WebCore could not be loaded")))){
  // Dispose the dead process/session. Go and Reload must be able to create
  // another renderer rather than sending commands to a dead command queue.
  if(!close_renderer() && callbacks.status_changed)callbacks.status_changed("Browser renderer stopped; press Reload or enter an address to restart");
  return;
 }
 if(stopping){
  // Ignore stale Loading reports while Stop is queued. An unresponsive
  // renderer can always be disposed by its frontend owner.
  if(now-stop_started<1000)return;
  if(current.state==SACX_BROWSER_STARTING || current.state==SACX_BROWSER_LOADING){
   if(!close_renderer() && callbacks.status_changed)callbacks.status_changed("Stopped unresponsive page; Reload to try again");
   return;
  }
  stopping=false;
 }
 info=current;
 starting=current.state==SACX_BROWSER_STARTING;
 if(starting && callbacks.status_changed){
  sacx_browser_startup_progress progress={session,0,0,0};char text[128];
  int rc=bridge->browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress));
  if(!rc && progress.total_bytes && progress.read_bytes<=progress.total_bytes)
   snprintf(text,sizeof(text),"Starting browser: reading WebCore %u / %u KiB (%u%%)",(unsigned)(progress.read_bytes/1024),(unsigned)(progress.total_bytes/1024),(unsigned)(progress.read_bytes*100/progress.total_bytes));
  else dihs_copy(text,sizeof(text),"Starting browser: loading WebCore executable");
  callbacks.status_changed(text);
 }
 if(current.url[0] && !dihs_streq(current.url,"about:blank") && !dihs_streq(last_url,current.url)){
  dihs_copy(navigation_url,sizeof(navigation_url),current.url);
  dihs_copy(last_url,sizeof(last_url),current.url);if(callbacks.url_changed)callbacks.url_changed(current.url);
 }
 if(!dihs_streq(last_title,current.title)){dihs_copy(last_title,sizeof(last_title),current.title);if(callbacks.title_changed)callbacks.title_changed(current.title);}
 if(callbacks.pointer_changed)callbacks.pointer_changed(current.pointer);
 if(!dihs_streq(last_error,current.error)){
  dihs_copy(last_error,sizeof(last_error),current.error);
  if(current.error[0] && callbacks.status_changed)callbacks.status_changed(current.error);
 }
}
void dihscover_netsurf_mouse(int32_t x,int32_t y,uint8_t buttons,int32_t wheel){
 if(have_mouse && x==last_x && y==last_y && buttons==last_buttons && !wheel)return;
 sacx_browser_command c={};c.type=SACX_BROWSER_MOUSE;c.a=x;c.b=y;c.c=buttons&7;c.d=wheel<-120?-120:wheel>120?120:wheel;(void)send(c);
 last_x=x;last_y=y;last_buttons=buttons;have_mouse=1;
}
void dihscover_netsurf_key(uint32_t key){
 sacx_browser_command c={};c.type=SACX_BROWSER_KEY;c.a=SACX_KEY_A;
 if(key>='a' && key<='z'){c.a=SACX_KEY_A+key-'a';c.b=key;}
 else if(key>='A' && key<='Z'){c.a=SACX_KEY_A+key-'A';c.b=key;c.c=SACX_WINDOW_SHIFT;}
 else switch(key){
 case NS_KEY_DELETE_LEFT:c.a=SACX_KEY_BACKSPACE;break;case NS_KEY_DELETE_RIGHT:c.a=SACX_KEY_DELETE;break;
 case NS_KEY_CR:case NS_KEY_NL:c.a=SACX_KEY_ENTER;break;
 case NS_KEY_SHIFT_TAB:c.c=SACX_WINDOW_SHIFT;[[fallthrough]];case NS_KEY_TAB:c.a=SACX_KEY_TAB;break;
 case NS_KEY_ESCAPE:c.a=SACX_KEY_ESCAPE;break;case NS_KEY_LEFT:c.a=SACX_KEY_LEFT;break;case NS_KEY_RIGHT:c.a=SACX_KEY_RIGHT;break;
 case NS_KEY_UP:c.a=SACX_KEY_UP;break;case NS_KEY_DOWN:c.a=SACX_KEY_DOWN;break;case NS_KEY_LINE_START:c.a=SACX_KEY_HOME;break;
 case NS_KEY_LINE_END:c.a=SACX_KEY_END;break;case NS_KEY_PAGE_UP:c.a=SACX_KEY_PAGEUP;break;case NS_KEY_PAGE_DOWN:c.a=SACX_KEY_PAGEDOWN;break;
 default:
  if(key<32 || key>126)return;c.b=key;
  for(unsigned usage=SACX_KEY_A;usage<=SACX_KEY_SLASH;++usage){
   if(sacx_window_key_text(usage,0)==key){c.a=usage;break;}
   if(sacx_window_key_text(usage,SACX_WINDOW_SHIFT)==key){c.a=usage;c.c=SACX_WINDOW_SHIFT;break;}
  }
  break;
 }
 (void)send(c);
}
