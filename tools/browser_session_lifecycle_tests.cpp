#include <cassert>
#include <cstring>
#include <cstdio>
extern "C" {
#include "process/dihos_browser_bridge.h"
#include "process/dihos_user_loader.h"
#include "process/dihos_user_runtime.h"
#include "process/dihos_user_window.h"
#include "kwrappers/kgfx.h"
}
struct sacx_task {uint32_t task_id;};static sacx_task task={1},other={2};static sacx_task* G_current_task=&task;
struct sacx_gfx_slot {kgfx_obj_handle handle;};static sacx_gfx_slot parent={{0}};
static kgfx_obj objects[2];static bool alive[2]={true,false};static unsigned stopped,cancelled,destroyed;
static uint64_t load_owner;static uint32_t load_state=1;static uint64_t surface_window;
static uint32_t runtime_state=DIHOS_USER_READY;
static sacx_gfx_slot* sacx_gfx_from_handle(sacx_task*,uint32_t handle){return handle==7?&parent:nullptr;}
extern "C" {
uint32_t smp_current_logical_id(void){return 0;}
int aarch64_user_vm_check_buffer(const aarch64_user_vm*,uint64_t p,uint64_t n,int){return p && n<=4096?0:-14;}
int aarch64_user_vm_copy_out(const aarch64_user_vm*,uint64_t p,const void* source,uint64_t n){if(!p)return -14;memcpy((void*)p,source,n);return 0;}
int aarch64_user_vm_copy_in(const aarch64_user_vm*,void* target,uint64_t p,uint64_t n){if(!p)return -14;memcpy(target,(void*)p,n);return 0;}
int dihos_user_load_start_browser(const char* path,uint64_t owner){assert(!strcmp(path,"0:/OS/System/Programs/Dihscover/WebCore.bin"));if(load_owner)return -16;load_owner=owner;load_state=1;return 0;}
int dihos_user_load_browser_result(uint64_t owner,dihos_user_load_status* status){assert(load_owner==owner);status->state=load_state;status->read_bytes=57ull*1024*1024;status->total_bytes=100ull*1024*1024;if(load_state==1 || load_state==2)return -11;status->handle={0,1};load_owner=0;return 0;}
void dihos_user_load_browser_cancel(uint64_t owner){assert(owner==load_owner);load_owner=0;++cancelled;}
uint32_t dihos_user_runtime_snapshot(dihos_user_task_info* info,uint32_t){*info={};info->handle={0,1};info->state=runtime_state;return 1;}
int dihos_user_runtime_set_interactive(dihos_process_handle handle,uint32_t us){assert(handle.slot==0 && handle.generation==1 && us==4000);return 0;}
int sacx_browser_surface_destroy(uint64_t);
int dihos_user_runtime_stop(dihos_process_handle handle){assert(handle.slot==0 && handle.generation==1);++stopped;if(surface_window)sacx_browser_surface_destroy(surface_window);return 0;}
uint64_t dihos_user_window_owner(uint64_t id){assert(id==surface_window);return 257;}
kgfx_obj* kgfx_obj_ref(kgfx_obj_handle h){return h.idx>=0 && h.idx<2 && alive[h.idx]?&objects[h.idx]:nullptr;}
kgfx_obj_handle kgfx_obj_add_image(const uint32_t* pixels,uint32_t w,uint32_t h,int32_t,int32_t,uint32_t){assert(!alive[1]);alive[1]=true;objects[1]={};objects[1].kind=KGFX_OBJ_IMAGE;objects[1].u.image.argb=pixels;objects[1].u.image.w=w;objects[1].u.image.h=h;return {1};}
int kgfx_obj_destroy(kgfx_obj_handle h){assert(h.idx==1 && alive[1]);alive[1]=false;++destroyed;return 0;}
}
#include "../kernel/apps/sacx_browser_session.inc"
int main(){
 objects[0].kind=KGFX_OBJ_RECT;
 sacx_browser_open open={7,640,480,0};assert(!sacx_api_browser_session(SACX_BROWSER_OPEN,&open,sizeof(open)) && open.session);
 sacx_browser_info info={};info.session=open.session;G_current_task=&other;assert(sacx_api_browser_session(SACX_BROWSER_STATUS,&info,sizeof(info))==-9);G_current_task=&task;
 assert(!sacx_api_browser_session(SACX_BROWSER_STATUS,&info,sizeof(info)) && info.state==SACX_BROWSER_STARTING);
 sacx_browser_startup_progress progress={open.session,0,0,0};load_state=2;
 assert(sacx_api_browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress)-1)==-22);
 ++progress.session;assert(sacx_api_browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress))==-9);--progress.session;
 G_current_task=&other;assert(sacx_api_browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress))==-9);G_current_task=&task;
 assert(!sacx_api_browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress)) && progress.state==2 && progress.read_bytes==57ull*1024*1024 && progress.total_bytes==100ull*1024*1024);
 assert(load_owner==1); // Progress must not consume the pending load job.
 load_state=3;surface_window=513;uint32_t first=0xff00ff00,second=0xff112233;sacx_user_window_desc desc={640,480,"WebCore"};
 assert(!sacx_browser_surface_create(surface_window,&desc,&first) && alive[1]);assert(dihos_browser_renderer(1,open.session)==257);
 progress={open.session,0,0,0};assert(!sacx_api_browser_session(SACX_BROWSER_STARTUP_PROGRESS,&progress,sizeof(progress)) && !progress.state && !progress.total_bytes);
 assert(sacx_browser_surface_present(surface_window,&second)==1 && objects[1].u.image.argb==&second);
 runtime_state=DIHOS_USER_FAILED;info.session=open.session;assert(!sacx_api_browser_session(SACX_BROWSER_STATUS,&info,sizeof(info)) && info.state==SACX_BROWSER_EXITED);runtime_state=DIHOS_USER_READY;
 uint32_t wrong=open.session+1;assert(sacx_api_browser_session(SACX_BROWSER_CLOSE,&wrong,sizeof(wrong))==-9);
 assert(!sacx_api_browser_session(SACX_BROWSER_CLOSE,&open.session,sizeof(open.session)));assert(stopped==1 && destroyed==1 && !alive[1]);surface_window=0;
 open.session=0;assert(!sacx_api_browser_session(SACX_BROWSER_OPEN,&open,sizeof(open)));load_state=2;sacx_browser_close_task(1);assert(cancelled==1 && !load_owner);
 assert(!sacx_api_browser_session(SACX_BROWSER_OPEN,&open,sizeof(open)));load_state=4;info.session=open.session;
 assert(!sacx_api_browser_session(SACX_BROWSER_STATUS,&info,sizeof(info)) && info.state==SACX_BROWSER_FAILED);
 sacx_browser_close_task(1);assert(stopped==1 && destroyed==1);
 puts("PASS: actual frontend bridge attaches owned pixels to its viewport, rejects foreign callers, binds the right child and cleans up close, pending load and launch failure");
 puts("PASS: executable-read progress preserves pending ownership and rejects foreign owners, sessions and malformed sizes");
}
