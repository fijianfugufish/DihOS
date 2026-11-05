#include "process/dihos_user_loader.h"
#include "process/dihos_user_runtime.h"
#include "process/dihos_user_elf.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"
#include "system/dihos_time.h"
static struct {
    dihos_user_load_status status;
    char path[256];
    KFile file;
    uint32_t opened;
    uint32_t network;
    uint32_t surface;
    uint8_t *data;
    uint64_t pages;
    uint64_t owner;
} job;
static void release(void){
    if(job.opened){kfile_close(&job.file);job.opened=0;}
    if(job.data){pmem_free_pages(job.data,job.pages);job.data=0;job.pages=0;}
}
static void failed(int rc){release();job.status.error=rc;job.status.state=4;}
int dihos_user_load_start(const char *path){
    if(!path || job.owner || job.status.state==1 || job.status.state==2)return -1;
    unsigned n=0;while(n<sizeof(job.path) && path[n])++n;
    if(!n || n>=sizeof(job.path))return -2;
    int rc=dihos_user_runtime_enable();if(rc)return rc;
    job.status=(dihos_user_load_status){0};
    job.network=0;
    job.surface=0;
    for(unsigned i=0;i<=n;++i)job.path[i]=path[i];
    job.status.state=1;return 0;
}
int dihos_user_load_start_network(const char *path){int rc=dihos_user_load_start(path);if(!rc)job.network=1;return rc;}
int dihos_user_load_start_window(const char *path){int rc=dihos_user_load_start(path);if(!rc)job.surface=1;return rc;}
int dihos_user_load_start_web(const char *path){int rc=dihos_user_load_start(path);if(!rc){job.surface=1;job.network=1;}return rc;}
int dihos_user_load_start_browser(const char *path,uint64_t owner){
    if(!owner)return -22;int rc=dihos_user_load_start_web(path);if(!rc)job.owner=owner;return rc;
}
int dihos_user_load_browser_result(uint64_t owner,dihos_user_load_status *out){
    if(!owner || job.owner!=owner || !out)return -9;
    *out=job.status;
    if(job.status.state==1 || job.status.state==2)return -11;
    job.owner=0;return 0;
}
void dihos_user_load_browser_cancel(uint64_t owner){
    if(!owner || job.owner!=owner)return;
    if(job.status.state==3)(void)dihos_user_runtime_stop(job.status.handle);
    else dihos_user_load_cancel();
    job.owner=0;
}
void dihos_user_load_poll(void){
    if(job.status.state==1){
        int rc=kfile_open(&job.file,job.path,KFILE_READ);if(rc){failed(-10);return;}
        job.opened=1;job.status.total_bytes=kfile_size(&job.file);
        if(job.status.total_bytes<64 || job.status.total_bytes>DIHOS_USER_ELF_MAX_BYTES){failed(-11);return;}
        job.pages=(job.status.total_bytes+4095)/4096;job.data=pmem_alloc_pages(job.pages);
        if(!job.data){failed(-12);return;}
        job.status.state=2;return;
    }
    if(job.status.state!=2)return;
    if(job.status.read_bytes<job.status.total_bytes){
        /* A 64 KiB FatFs call can wait for sixteen StorVSC DMA completions.
         * Return between 4 KiB requests without holding filesystem locks.
         * Ordinary launches retain a one-ms/64-KiB budget. Browser startup
         * gets four ms/256 KiB: its large engine should not spend most of
         * its launch waiting for the next desktop frame. Yield at every
         * 16 KiB browser boundary; ordinary launches retain 4 KiB reads.
         * Individual synchronous driver requests can exceed this budget;
         * record their duration instead of claiming a hard latency bound. */
        uint64_t started=dihos_time_monotonic_ms();
        unsigned max_calls=16;
        uint32_t read_max=job.owner?16384:4096;
        uint64_t budget_ms=job.owner?4:1;
        for(unsigned calls=0;calls<max_calls;++calls){
            uint64_t remaining=job.status.total_bytes-job.status.read_bytes;
            uint32_t n=remaining>read_max?read_max:(uint32_t)remaining,read=0;
            uint64_t before=dihos_time_monotonic_ms();
            int rc=kfile_read(&job.file,job.data+job.status.read_bytes,n,&read);
            uint64_t after=dihos_time_monotonic_ms();
            ++job.status.read_calls;job.status.last_read_ms=after-before;
            if(job.status.last_read_ms>job.status.max_read_ms)job.status.max_read_ms=job.status.last_read_ms;
            if(rc || !read || read>n){failed(-13);return;}
            job.status.read_bytes+=read;
            if(job.status.read_bytes==job.status.total_bytes || after-started>=budget_ms)return;
        }
        return;
    }
    /* Source bytes remain kernel-owned until the validated image is copied.
     * The task has no references into this staging buffer or open file. */
    uint64_t grants=(job.network?DIHOS_PROCESS_CAP_NETWORK:0)|(job.surface?DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE:0);
    int rc=job.owner?dihos_user_spawn_owned_browser_elf(job.data,job.status.total_bytes,job.pages,&job.status.handle):
        dihos_user_spawn_owned_elf_with_grants(job.data,job.status.total_bytes,job.pages,grants,&job.status.handle);
    if(rc){failed(rc);return;}
    job.data=0;job.pages=0;release();job.status.state=3;
}
void dihos_user_load_cancel(void){
    if(job.status.state==1 || job.status.state==2){release();job.status.state=5;}
}
void dihos_user_load_status_get(dihos_user_load_status *out){if(out)*out=job.status;}
