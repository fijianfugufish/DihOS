#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../sdk/sacx/apps/dihscover/main.cpp"
#include "../sdk/sacx/apps/dihscover/webcore_backend.cpp"
static sacx_update_fn updater;
static unsigned windows,buttons,textboxes,next_handle=1;
static bool focus_address,visible=true,focus_window=true;static uint8_t pressed_key;
static std::string address_text,saved_preferences;
static std::vector<sacx_browser_command> sent;
static sacx_browser_info engine;
static unsigned opened,closed;static uint64_t test_now=100;
static uint32_t mock_line_height=32;
static std::vector<std::string> diagnostic_logs;
template<typename R,typename... A> static R zero(A...){return R();}
template<typename R,typename... A> static void noop(R(*&p)(A...)){p=zero<R,A...>;}
void* dihs_memcpy(void* d,const void* s,__SIZE_TYPE__ n){return memcpy(d,s,n);}
void* dihs_memset(void* d,int c,__SIZE_TYPE__ n){return memset(d,c,n);}
uint32_t dihs_strlen(const char* s){return s?(uint32_t)strlen(s):0;}
int dihs_streq(const char* a,const char* b){return a && b && !strcmp(a,b);}
void dihs_copy(char* d,uint32_t n,const char* s){snprintf(d,n,"%s",s?s:"");}
int browser_heap_init(){return 0;}void browser_heap_set_api(const sacx_api*){}
extern "C" void dihscover_schedule_api(const sacx_api*){}
extern "C" void dihscover_schedule_window(uint32_t){}
static int session_call(uint32_t op,void* payload,uint32_t bytes){
 if(op==SACX_BROWSER_OPEN){assert(bytes==sizeof(sacx_browser_open));auto* p=(sacx_browser_open*)payload;assert(p->parent && p->width==960);p->session=55+(++opened);engine={};engine.session=p->session;engine.state=SACX_BROWSER_STARTING;return 0;}
 if(op==SACX_BROWSER_SEND){auto* p=(sacx_browser_send*)payload;assert(bytes==sizeof(*p) && p->session==engine.session);sent.push_back(p->command);return 0;}
 if(op==SACX_BROWSER_STATUS){assert(bytes==sizeof(engine));*(sacx_browser_info*)payload=engine;return 0;}
 if(op==SACX_BROWSER_STARTUP_PROGRESS){auto* p=(sacx_browser_startup_progress*)payload;assert(bytes==sizeof(*p) && p->session==engine.session);p->state=2;p->read_bytes=57ull*1024*1024;p->total_bytes=100ull*1024*1024;return 0;}
 if(op==SACX_BROWSER_CLOSE){assert(*(uint32_t*)payload==engine.session);++closed;return 0;}return -1;
}
int main(){
 sacx_browser_frontend_api full={};auto& a=full.base;a.abi_version=1;a.struct_size=sizeof(full);full.browser_session=session_call;
 noop(a.app_checkpoint);noop(a.net_response_headers_read);noop(a.net_request_start_stream);noop(a.app_set_console_visible);noop(a.log);
 a.log=[](const char* line){diagnostic_logs.emplace_back(line?line:"");};
 noop(a.window_set_visible);noop(a.window_set_title);noop(a.gfx_obj_set_parent);noop(a.gfx_obj_set_clip_to_parent);noop(a.gfx_obj_set_rect);
 noop(a.gfx_text_set_pos);noop(a.gfx_obj_set_visible);noop(a.gfx_text_set);noop(a.button_pressed);noop(a.textbox_set_bounds);
 noop(a.textbox_set_max_len);noop(a.textbox_select);noop(a.mouse_get_state);noop(a.window_point_can_receive_input);noop(a.mouse_set_cursor);
 a.mouse_get_state=[](sacx_mouse_state* state){*state={};return 0;};
 noop(a.file_close);noop(a.file_mkdir);noop(a.app_yield);
 a.text_line_height=[](uint32_t scale,int32_t spacing){assert(scale==1 && !spacing);return mock_line_height;};
 a.app_exit=[](int,const char*){return -1;};a.app_set_update=[](sacx_update_fn fn){updater=fn;return 0;};
 a.time_monotonic_ms=[]()->uint64_t{return test_now;};a.input_key_down=[](uint8_t){return 0;};a.input_key_pressed=[](uint8_t key){return key==pressed_key?1:0;};
 a.window_create_ex=[](int32_t,int32_t,uint32_t w,uint32_t h,int32_t,const char*,const sacx_window_style*,uint32_t* out){assert(w==960 && h==680);++windows;*out=next_handle++;return 0;};
 a.window_root=[](uint32_t,uint32_t* out){*out=next_handle++;return 0;};a.window_visible=[](uint32_t){return visible?1:0;};
 a.window_focused=[](uint32_t){return focus_window?1:0;};
 a.button_add_rect=[](int32_t,int32_t,uint32_t,uint32_t,int32_t,const sacx_button_style*,sacx_button_on_click_fn,void*,uint32_t* out){++buttons;*out=next_handle++;return 0;};
 a.button_root=[](uint32_t,uint32_t* out){*out=next_handle++;return 0;};
 a.gfx_obj_add_text=[](const char*,int32_t,int32_t,int32_t,sacx_color,uint8_t,uint32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t* out){*out=next_handle++;return 0;};
 a.gfx_obj_add_rect=[](int32_t,int32_t,uint32_t,uint32_t,int32_t,sacx_color,uint32_t,uint32_t* out){*out=next_handle++;return 0;};
 a.gfx_obj_get_rect=[](uint32_t,int32_t* x,int32_t* y,uint32_t* w,uint32_t* h){*x=90;*y=56;*w=960;*h=680;return 0;};
 a.textbox_add_rect=[](int32_t,int32_t,uint32_t,uint32_t,int32_t,const sacx_textbox_style*,sacx_textbox_on_submit_fn,void*,uint32_t* out){++textboxes;*out=next_handle++;return 0;};
 a.textbox_root=[](uint32_t,uint32_t* out){*out=next_handle++;return 0;};
 a.textbox_set_text=[](uint32_t,const char* text){address_text=text;return 0;};
 a.textbox_text_copy=[](uint32_t,char* text,uint32_t n){snprintf(text,n,"%s",address_text.c_str());return(int)address_text.size();};
 a.textbox_focused=[](uint32_t){return focus_address?1:0;};
 a.file_open=[](const char*,uint32_t flags,uint32_t* out){*out=flags&SACX_FILE_WRITE?2:1;return 0;};
 a.file_read=[](uint32_t,void* dst,uint32_t,uint32_t* got){const char* text="zoom=125\nsans=1\nminimum=10\n";*got=(uint32_t)strlen(text);memcpy(dst,text,*got);return 0;};
 a.file_write=[](uint32_t,const void* text,uint32_t n,uint32_t* written){saved_preferences.assign((const char*)text,n);*written=n;return 0;};
 assert(!sacx_main(&a) && updater && windows==1 && buttons==5 && textboxes==1);
 assert(g_progress_y>=UI_STATUS_Y+mock_line_height+6 && g_document_y>=g_progress_y+12);
 assert(address_text=="https://www.google.com/");
 assert(sent.size()==2 && sent[0].type==SACX_BROWSER_PREFS && sent[0].a==1 && sent[0].b==10 && sent[0].c==125);
 assert(sent[1].type==SACX_BROWSER_NAVIGATE && !strcmp(sent[1].text,"https://www.google.com/"));
 mock_line_height=48;layout_ui();assert(g_progress_y==138 && g_document_y==150);mock_line_height=32;layout_ui();
 assert(!updater(&a));assert(strstr(g_status_text,"Starting browser: reading WebCore") && strstr(g_status_text,"57%"));
 pressed_key=SACX_KEY_ESCAPE;assert(!updater(&a));assert(closed==1 && !session && !g_loading && g_status_text==std::string("Stopped"));
 unsigned stopped=(unsigned)sent.size();
 pressed_key=0;engine.state=SACX_BROWSER_READY;for(unsigned i=0;i<4;++i)assert(!updater(&a));
 assert(sent.size()==stopped); // No repeated startup navigation after Stop.
 address_text="cats & dogs";focus_address=true;pressed_key=SACX_KEY_ENTER;focus_window=false;unsigned before=(unsigned)sent.size();assert(!updater(&a) && sent.size()==before);
 focus_window=true;assert(!updater(&a));
 assert(sent.back().type==SACX_BROWSER_NAVIGATE && !strcmp(sent.back().text,"https://www.google.com/search?q=cats+%26+dogs"));
 assert(opened==2 && session==57); // A new navigation can reopen after startup cancellation.
 pressed_key=0;focus_address=false;engine.state=SACX_BROWSER_LOADING;assert(!updater(&a));
 pressed_key=SACX_KEY_ESCAPE;assert(!updater(&a));assert(sent.back().type==SACX_BROWSER_STOP && !g_loading);
 pressed_key=0;test_now+=500;assert(!updater(&a));assert(!g_loading && closed==1); // Ignore stale Loading.
 test_now+=501;assert(!updater(&a));assert(!session && closed==2 && !g_loading); // No renderer acknowledgement.
 button_cb(0,(void*)ACTION_RELOAD);assert(!updater(&a));assert(opened==3 && sent.back().type==SACX_BROWSER_NAVIGATE);
 engine.state=SACX_BROWSER_LOADING;assert(!updater(&a));
 pressed_key=SACX_KEY_ESCAPE;assert(!updater(&a));engine.state=SACX_BROWSER_READY;pressed_key=0;test_now+=1001;assert(!updater(&a));assert(session && closed==2 && !stopping && !g_loading);
 dihscover_netsurf_stop();assert(stopping);assert(!dihscover_netsurf_reload());assert(opened==4 && closed==3 && !stopping && sent.back().type==SACX_BROWSER_NAVIGATE);
 pressed_key=0;focus_address=false;button_cb(0,(void*)ACTION_BACK);assert(!updater(&a));assert(sent.back().type==SACX_BROWSER_BACK);
 button_cb(0,(void*)ACTION_FONT);assert(!updater(&a));assert(sent.back().type==SACX_BROWSER_PREFS && !saved_preferences.empty());
 unsigned count=(unsigned)sent.size();dihscover_netsurf_mouse(10,20,0,0);dihscover_netsurf_mouse(10,20,0,0);assert(sent.size()==count+1);
 dihscover_netsurf_mouse(10,20,1,0);dihscover_netsurf_mouse(10,20,0,0);assert(sent.size()==count+3);
 dihscover_netsurf_key('1');assert(sent.back().a==SACX_KEY_1 && sent.back().b=='1');
 assert(!dihscover_netsurf_resize(1280,900));assert(sent.back().type==SACX_BROWSER_RESIZE && sent.back().a==1280 && sent.back().b==900);
 strcpy(engine.title,"Example Domain");strcpy(engine.url,"https://example.com/");assert(!updater(&a));assert(address_text=="https://example.com/");
 unsigned recovered=opened;engine.state=SACX_BROWSER_EXITED;strcpy(engine.error,"WebCore renderer stopped");assert(!updater(&a));assert(!session && !g_loading);
 focus_address=true;pressed_key=SACX_KEY_F12;unsigned log_count=diagnostic_logs.size();assert(!updater(&a));assert(diagnostic_logs.size()==log_count+1 && diagnostic_logs.back().find("Dihscover frontend:")==0);pressed_key=0;
 button_cb(0,(void*)ACTION_RELOAD);assert(!updater(&a));assert(opened==recovered+1 && sent.back().type==SACX_BROWSER_NAVIGATE && !strcmp(sent.back().text,"https://example.com/"));
 engine.state=SACX_BROWSER_FAILED;strcpy(engine.error,"ordinary page failure");assert(!updater(&a));assert(session); // A failed page is not a dead process.
 strcpy(engine.error,"WebCore renderer faulted; close and reopen Dihscover");assert(!updater(&a));assert(!session);
 address_text="https://example.com/recovered";focus_address=true;pressed_key=SACX_KEY_ENTER;assert(!updater(&a));assert(opened==recovered+2 && session);pressed_key=0;
 pressed_key=SACX_KEY_F12;assert(!updater(&a));assert(sent.back().type==SACX_BROWSER_DIAGNOSTICS);pressed_key=0;
 visible=false;assert(updater(&a)==-1);
 puts("PASS: original Dihscover main.cpp creates its existing window, five buttons and search field; Go, Back, font preferences, URL callbacks and close use the WebCore bridge");
 puts("PASS: startup progress, immediate startup cancellation, stale Loading suppression, bounded unresponsive Stop, graceful Stop and reopening without repeated startup navigation");
 puts("PASS: status text, progress bar and viewport stay separated at 32- and 48-pixel font line heights");
 puts("PASS: stopped/faulted renderer cleanup and explicit Reload/Go recovery; ordinary page errors retain their renderer");
 puts("PASS: F12 works with address focus and a stopped renderer, reports frontend state and forwards live renderer diagnostics");
}
