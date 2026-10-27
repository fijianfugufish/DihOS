#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
extern "C" {
#include "system/kfiber.h"
}
#include "../sdk/sacx/apps/dihscover/scroll.h"
enum {SACX_TASK_UNUSED,SACX_TASK_READY,SACX_TASK_SLEEPING,SACX_TASK_EXITED,SACX_TASK_FAULTED,KFILE_READ=1};
#define SACX_MAX_FILE_BYTES (32u*1024u*1024u)
struct sacx_runtime_io{void *user;};
struct sacx_task{
    uint32_t task_id,state;uint64_t wake_tick;
    uint8_t launch_pending,launch_cancelled;
    char launch_path[256],friendly_path[256],launch_arg_raw[256],launch_arg_friendly[256],exit_message[128];
    sacx_runtime_io io;
};
static sacx_task G_tasks[4],*G_current_task,*G_launch_active;
static kfiber G_launch_fiber;static void *G_launch_stack;
static uint64_t G_launch_slice_end,G_launch_deadline,now;
static int G_launch_result;static uint32_t G_next_task_id=1;
static unsigned allocations,opens,closes,reads,loads,api_inits,failures;
static int fault;static uint32_t file_size=196608;
static uint64_t dihos_time_monotonic_ms(){return now;}
static uint64_t dihos_time_ticks(){return now;}
static void kbusy_pump(){}
static void terminal_print(const char*){}
static void terminal_warn(const char*){}
static void sacx_copy_trunc(char *out,uint32_t cap,const char *in){snprintf(out,cap,"%s",in);}
static void *pmem_alloc_pages(uint64_t pages){++allocations;return malloc((size_t)pages*4096);}
static void pmem_free_pages(void *p,uint64_t){assert(p&&allocations);--allocations;free(p);}
static sacx_task *sacx_alloc_task_slot(){for(auto &task:G_tasks)if(!task.state)return &task;return nullptr;}
static void sacx_task_reset(sacx_task *task){*task={};}
static void sacx_task_init_api(sacx_task*){++api_inits;}
static void sacx_task_finish(sacx_task *task,int,const char *message,uint32_t state){assert(!task->launch_pending);task->state=state;if(state==SACX_TASK_FAULTED)++failures;char copy[128];sacx_copy_trunc(copy,sizeof copy,message);sacx_copy_trunc(task->exit_message,sizeof task->exit_message,copy);}
#include "../kernel/apps/sacx_launch_checkpoint.inc"
struct KFile{uint32_t offset;};
static int kfile_open(KFile *f,const char *path,uint32_t){++opens;assert(!strcmp(path,"0:/browser.sacx"));f->offset=0;return fault==1?-1:0;}
static uint64_t kfile_size(KFile*){return file_size;}
static void kfile_close(KFile*){++closes;}
static int kfile_read(KFile *f,void *out,uint32_t bytes,uint32_t *got){++reads;++now;if(fault==2){*got=0;return -1;}memset(out,0x42,bytes);*got=bytes;f->offset+=bytes;return 0;}
#include "../kernel/apps/sacx_launch_read.inc"
static int sacx_load_image(sacx_task*,const uint8_t *data,uint32_t size,const char*){
    ++loads;assert(size==file_size&&data[0]==0x42);
    for(unsigned i=0;i<40;++i){++now;if(sacx_loader_checkpoint())return -1;}
    return fault==3?-1:0;
}
#include "../kernel/apps/sacx_launch_service.inc"
#include "../kernel/apps/sacx_launch_start.inc"
static void drain(){for(unsigned i=0;i<1000;++i){uint64_t before=now;sacx_runtime_launch_poll();assert(now-before<=2);bool pending=false;for(auto &t:G_tasks)pending|=t.launch_pending;if(!pending)return;}assert(false);}
static sacx_task *queue(){uint32_t id;char path[]="0:/browser.sacx";assert(!sacx_runtime_launch_ex(path,"Browser",nullptr,nullptr,nullptr,&id));path[3]='X';for(auto &t:G_tasks)if(t.task_id==id)return &t;assert(false);return nullptr;}
int main(){
    auto *task=queue();assert(!opens&&!reads&&!loads&&task->launch_pending&&task->state==SACX_TASK_SLEEPING);
    G_current_task=task;sacx_runtime_launch_poll();assert(!opens);G_current_task=nullptr;
    drain();assert(task->state==SACX_TASK_READY&&opens==1&&closes==1&&loads==1&&allocations==1);
    *task={};task=queue();sacx_runtime_launch_poll();assert(G_launch_active==task);task->launch_cancelled=1;drain();assert(task->state==SACX_TASK_EXITED&&closes==2&&allocations==1);
    *task={};task=queue();task->launch_cancelled=1;unsigned before=opens;drain();assert(opens==before&&task->state==SACX_TASK_EXITED);
    *task={};fault=2;task=queue();drain();assert(task->state==SACX_TASK_FAULTED&&allocations==1);
    *task={};fault=3;task=queue();drain();assert(task->state==SACX_TASK_FAULTED&&allocations==1);
    *task={};fault=0;task=queue();sacx_runtime_launch_poll();now+=60000;drain();assert(task->state==SACX_TASK_FAULTED&&strstr(task->exit_message,"timed out")&&allocations==1);
    *task={};file_size=0;task=queue();drain();assert(task->state==SACX_TASK_FAULTED&&allocations==1);
    assert(browser_scroll_clamp(900,200,600)==0);
    assert(browser_scroll_clamp(900,1200,600)==600);
    assert(browser_scroll_clamp(-1,1200,600)==0);
    assert(browser_scroll_clamp(INT64_MAX,INT32_MAX,1)==INT32_MAX-1);
    pmem_free_pages(G_launch_stack,64);assert(!allocations);
    puts("PASS: owned queued launch, bounded loader slices, no app reentry, cancellation, timeout, file/load failures, buffer cleanup and short-page scroll bounds");
}
