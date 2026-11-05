#include <cassert>
#include <vector>
#include <cstdio>
#include "../kernel/apps/dihos_user_window_desktop.cpp"
static dihos_user_window_backend test_backend;
static std::vector<sacx_user_window_event> events;
static bool focus=true,down[256],pressed[256];static uint64_t clock_ms;
static kgfx_obj objects[2];
extern "C" {
int dihos_user_window_set_backend(const dihos_user_window_backend* b){test_backend=*b;return 0;}
int dihos_user_window_push(uint64_t id,const sacx_user_window_event* e){assert(id==257);events.push_back(*e);return 0;}
uint64_t dihos_time_monotonic_ms(){return clock_ms;}
int kinput_key_down(uint8_t u){return down[u];}int kinput_key_pressed(uint8_t u){return pressed[u];}
uint8_t kmouse_buttons(){return 0;}int32_t kmouse_x(){return -10;}int32_t kmouse_y(){return -10;}int32_t kmouse_wheel(){return 0;}
kwindow_handle kwindow_create(int32_t,int32_t,uint32_t,uint32_t,int32_t,const kfont*,const char*,const kwindow_style*){return {0};}
int kwindow_destroy(kwindow_handle){return 0;}int kwindow_raise(kwindow_handle){return 0;}
int kwindow_set_close_deferred(kwindow_handle,uint8_t){return 0;}int kwindow_close_requested(kwindow_handle){return 0;}
int kwindow_focused(kwindow_handle){return focus;}int kwindow_point_can_receive_input(kwindow_handle,int32_t,int32_t){return focus;}
kgfx_obj_handle kwindow_root(kwindow_handle){return {0};}
kgfx_obj_handle kgfx_obj_add_image(const uint32_t*,uint32_t w,uint32_t h,int32_t,int32_t,uint32_t){objects[1].u.image.w=w;objects[1].u.image.h=h;return {1};}
kgfx_obj* kgfx_obj_ref(kgfx_obj_handle h){return &objects[h.idx];}int kgfx_obj_destroy(kgfx_obj_handle){return 0;}
}
int main(){
 kfont font={};dihos_user_window_desktop_init(&font);sacx_user_window_desc d={640,480,"Test"};uint32_t p=0;
 assert(!test_backend.create(257,&d,&p));
 // Held keys from the previously focused application must not leak or repeat.
 down[KEY_A]=true;dihos_user_window_desktop_poll();clock_ms=1000;dihos_user_window_desktop_poll();assert(events.empty());
 down[KEY_A]=false;dihos_user_window_desktop_poll();events.clear();
 down[KEY_B]=pressed[KEY_B]=true;dihos_user_window_desktop_poll();pressed[KEY_B]=false;
 assert(events.size()==1 && events[0].type==SACX_WINDOW_KEY_DOWN && events[0].y=='b');
 clock_ms=1399;dihos_user_window_desktop_poll();assert(events.size()==1);
 clock_ms=1400;dihos_user_window_desktop_poll();assert(events.size()==2 && (events[1].delta&SACX_WINDOW_REPEAT));
 down[KEY_B]=false;dihos_user_window_desktop_poll();assert(events.back().type==SACX_WINDOW_KEY_UP);
 focus=false;events.clear();dihos_user_window_desktop_poll();assert(events.size()==1 && events[0].type==SACX_WINDOW_RESET);
 events.clear();down[KEY_C]=pressed[KEY_C]=true;dihos_user_window_desktop_poll();assert(events.empty());pressed[KEY_C]=false;
 focus=true;dihos_user_window_desktop_poll();clock_ms=2000;dihos_user_window_desktop_poll();assert(events.empty());
 puts("PASS: focused keyboard routing, held-key isolation, repeats, releases and blur reset");
}
