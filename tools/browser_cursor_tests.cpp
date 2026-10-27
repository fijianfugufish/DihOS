#include <cassert>
#include <cstdint>
#include <cstdio>
enum kmouse_cursor{KMOUSE_CURSOR_ARROW,KMOUSE_CURSOR_BEAM,KMOUSE_CURSOR_LINK,KMOUSE_CURSOR_SIZE};
enum{SACX_TASK_UNUSED,SACX_TASK_READY,SACX_TASK_SLEEPING,SACX_TASK_EXITED};
struct window_slot{int handle;bool used;};
struct task{uint32_t requested_cursor;uint64_t cursor_request_ms;uint8_t cursor_request_valid,started;uint32_t state;window_slot windows[2];};
static task G_tasks[2],*G_current_task;static uint64_t now;static unsigned core;
static kmouse_cursor shape;static int receiving_window=1;
static unsigned smp_current_logical_id(){return core;}
static uint64_t dihos_time_monotonic_ms(){return now;}
static kmouse_cursor sacx_to_mouse_cursor(uint32_t c){return c<=KMOUSE_CURSOR_SIZE?(kmouse_cursor)c:KMOUSE_CURSOR_ARROW;}
static int kmouse_set_cursor(kmouse_cursor c){shape=c;return 0;}
static kmouse_cursor kmouse_current_cursor(){return shape;}
static int kmouse_x(){return 10;}static int kmouse_y(){return 20;}
static bool kwindow_point_can_receive_input(int w,int,int){return w==receiving_window;}
#include "../kernel/apps/sacx_cursor_service.inc"
int main(){
    auto &t=G_tasks[0];t.started=1;t.state=SACX_TASK_READY;t.windows[0]={1,true};G_current_task=&t;
    assert(!sacx_api_mouse_set_cursor(KMOUSE_CURSOR_LINK));G_current_task=nullptr;
    shape=KMOUSE_CURSOR_ARROW;++now;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_LINK);
    shape=KMOUSE_CURSOR_BEAM;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_BEAM);
    shape=KMOUSE_CURSOR_SIZE;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_SIZE);
    shape=KMOUSE_CURSOR_ARROW;receiving_window=2;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_ARROW);
    receiving_window=1;now=501;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_ARROW);
    now=1;t.state=SACX_TASK_EXITED;sacx_runtime_update_cursor();assert(shape==KMOUSE_CURSOR_ARROW);
    t.state=SACX_TASK_READY;G_current_task=&t;core=1;assert(sacx_api_mouse_set_cursor(KMOUSE_CURSOR_LINK)==-1);
    puts("PASS: app cursor survives frame reset, native widget/resize cursors win, occluded/expired/closed tasks cannot claim cursor, remote calls rejected");
}
