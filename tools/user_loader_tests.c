#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process/dihos_user_loader.h"
#include "process/dihos_user_elf.h"
#include "kwrappers/kfile.h"
#include "user_program.inc"
static int blocks,opens,closes,admissions,missing,oom,short_file,reject_admission;
static uint64_t cursor,clock_ms,read_delay,expected_grants;
static unsigned read_calls;
static unsigned browser_admissions;
static int large_payload;
static unsigned expected_read_max=4096;
#define LARGE_BYTES (100ull*1024*1024)
uint64_t dihos_time_monotonic_ms(void){return clock_ms;}
static char opened_path[256];
void *pmem_alloc_pages(uint64_t pages){if(oom)return 0;++blocks;return malloc((size_t)pages*4096);}
void pmem_free_pages(void *p,uint64_t pages){(void)pages;assert(blocks>0);--blocks;free(p);}
int dihos_user_runtime_enable(void){return 0;}
int dihos_user_runtime_stop(dihos_process_handle handle){assert(handle.generation==42);return 0;}
int dihos_user_spawn_owned_elf_with_grants(void *p,uint64_t bytes,uint64_t pages,uint64_t grants,dihos_process_handle *out){
    assert(grants==expected_grants);
    if(reject_admission)return -309;
    if(large_payload)assert(bytes==LARGE_BYTES && ((unsigned char*)p)[0]==0x5a && ((unsigned char*)p)[bytes-1]==0x5a);
    else assert(bytes==sizeof(user_exit_elf) && !memcmp(p,user_exit_elf,(size_t)bytes));++admissions;*out=(dihos_process_handle){1,42};pmem_free_pages(p,pages);return 0;
}
int dihos_user_spawn_owned_browser_elf(void *p,uint64_t bytes,uint64_t pages,dihos_process_handle *out){
    ++browser_admissions;
    return dihos_user_spawn_owned_elf_with_grants(p,bytes,pages,DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE,out);
}
int kfile_open(KFile *f,const char *p,uint32_t flags){(void)f;assert(flags==KFILE_READ);if(missing)return -1;++opens;cursor=0;strcpy(opened_path,p);return 0;}
uint64_t kfile_size(KFile *f){(void)f;return large_payload?LARGE_BYTES:sizeof(user_exit_elf);}
int kfile_read(KFile *f,void *p,uint32_t bytes,uint32_t *out){
    (void)f;assert(bytes<=expected_read_max);++read_calls;clock_ms+=read_delay;if(short_file){*out=0;return 0;}
    if(large_payload){assert(cursor+bytes<=LARGE_BYTES);memset(p,0x5a,bytes);cursor+=bytes;*out=bytes;return 0;}
    if(bytes>17)bytes=17;memcpy(p,user_exit_elf+cursor,bytes);cursor+=bytes;*out=bytes;return 0;
}
void kfile_close(KFile *f){(void)f;++closes;}
int main(void){
    dihos_user_load_status status;
    char path[]="0:/OS/System/Programs/UserRuntime/exit.elf";
    assert(!dihos_user_load_start(path));path[0]='X';
    assert(dihos_user_load_start("other")<0);assert(!opens && !blocks && !admissions);
    dihos_user_load_poll();assert(opens==1 && blocks==1 && opened_path[0]=='0');
    for(unsigned i=0;i<10000 && !admissions;++i)dihos_user_load_poll();
    dihos_user_load_status_get(&status);assert(status.state==3 && status.handle.generation==42 && !blocks && closes==opens);
    assert(!dihos_user_load_start("cancel"));dihos_user_load_poll();dihos_user_load_poll();dihos_user_load_cancel();
    dihos_user_load_status_get(&status);assert(status.state==5 && !blocks && closes==opens);
    assert(!dihos_user_load_start("queued-cancel"));dihos_user_load_cancel();assert(!blocks && closes==opens);
    missing=1;assert(!dihos_user_load_start("missing"));dihos_user_load_poll();missing=0;
    dihos_user_load_status_get(&status);assert(status.state==4 && !blocks && closes==opens);
    oom=1;assert(!dihos_user_load_start("oom"));dihos_user_load_poll();oom=0;
    dihos_user_load_status_get(&status);assert(status.state==4 && !blocks && closes==opens);
    short_file=1;assert(!dihos_user_load_start("truncated"));dihos_user_load_poll();dihos_user_load_poll();short_file=0;
    dihos_user_load_status_get(&status);assert(status.state==4 && !blocks && closes==opens);
    reject_admission=1;assert(!dihos_user_load_start("rejected"));
    do {dihos_user_load_poll();dihos_user_load_status_get(&status);}while(status.state==1 || status.state==2);
    reject_admission=0;assert(status.state==4 && status.error==-309 && !blocks && closes==opens);
    assert(!dihos_user_load_start("frozen-clock"));dihos_user_load_poll();
    unsigned before=read_calls;dihos_user_load_poll();assert(read_calls-before==16);dihos_user_load_cancel();assert(!blocks);
    read_delay=5;assert(!dihos_user_load_start("slow-disk"));dihos_user_load_poll();before=read_calls;
    dihos_user_load_poll();assert(read_calls-before==1);dihos_user_load_status_get(&status);
    assert(status.read_calls==1 && status.last_read_ms==5 && status.max_read_ms==5);dihos_user_load_cancel();read_delay=0;
    expected_grants=DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;assert(!dihos_user_load_start_window("window"));
    do{dihos_user_load_poll();dihos_user_load_status_get(&status);}while(status.state==1 || status.state==2);assert(status.state==3 && !blocks);
    large_payload=1;expected_grants=0;read_delay=1;unsigned normal_polls=0,browser_polls=0;
    assert(!dihos_user_load_start("large-normal"));
    do{dihos_user_load_poll();++normal_polls;clock_ms+=16;dihos_user_load_status_get(&status);assert(normal_polls<100000);}while(status.state==1 || status.state==2);
    assert(status.state==3 && status.read_calls==LARGE_BYTES/4096 && !blocks);
    expected_grants=DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;
    expected_read_max=16384;assert(!dihos_user_load_start_browser("large-browser",77));
    do{dihos_user_load_poll();++browser_polls;clock_ms+=16;dihos_user_load_status_get(&status);assert(browser_polls<100000);}while(status.state==1 || status.state==2);
    assert(status.state==3 && status.read_calls==LARGE_BYTES/16384 && !blocks);
    assert((browser_polls-2)*16==normal_polls-2);assert(!dihos_user_load_browser_result(77,&status));
    read_delay=0;assert(!dihos_user_load_start_browser("large-frozen-clock",77));dihos_user_load_poll();before=read_calls;
    dihos_user_load_poll();assert(read_calls-before==16);dihos_user_load_browser_cancel(77);assert(!blocks);
    read_delay=7;assert(!dihos_user_load_start_browser("large-slow-disk",77));dihos_user_load_poll();before=read_calls;
    dihos_user_load_poll();assert(read_calls-before==1);dihos_user_load_status_get(&status);assert(status.max_read_ms==7);
    dihos_user_load_browser_cancel(77);assert(!blocks && opens==closes);large_payload=0;read_delay=0;
    printf("PASS: modeled 100 MiB browser read uses %u polls versus %u ordinary; four-ms budget, 16 KiB requests, 16-call cap, slow-read yield and cancellation\n",browser_polls,normal_polls);
    expected_grants=DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;
    assert(!dihos_user_load_start_browser("browser",77));assert(dihos_user_load_start("other"));
    assert(dihos_user_load_browser_result(78,&status)==-9);assert(dihos_user_load_browser_result(77,&status)==-11);
    do{dihos_user_load_poll();dihos_user_load_status_get(&status);}while(status.state==1 || status.state==2);
    assert(dihos_user_load_start("cannot-steal-completion"));assert(!dihos_user_load_browser_result(77,&status) && status.state==3);
    assert(!dihos_user_load_start_browser("cancel-browser",77));dihos_user_load_browser_cancel(78);
    assert(dihos_user_load_start("still-owned"));dihos_user_load_browser_cancel(77);
    expected_read_max=4096;expected_grants=DIHOS_PROCESS_CAP_NETWORK;assert(!dihos_user_load_start_network("network"));
    do{dihos_user_load_poll();dihos_user_load_status_get(&status);}while(status.state==1 || status.state==2);assert(status.state==3 && !blocks);
    unsigned trusted_before=browser_admissions;assert(trusted_before);
    expected_grants=DIHOS_PROCESS_CAP_NETWORK|DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE;assert(!dihos_user_load_start_web("web"));
    do{dihos_user_load_poll();dihos_user_load_status_get(&status);}while(status.state==1 || status.state==2);assert(status.state==3 && !blocks);
    assert(browser_admissions==trusted_before);
    puts("PASS: trusted browser launch uses its dedicated budget; ordinary web launches retain ordinary admission");
    puts("PASS: one-ms loader budget, 4 KiB calls, frozen-clock hard cap, slow-read timing and explicit window/network grants");
    puts("PASS: queued owned paths, bounded partial reads, ownership transfer/rejection cleanup, cancellation, missing files, OOM and EOF");
}
