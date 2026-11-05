#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "process/dihos_user_sacx.h"
#include "../sdk/sacx/include/sacx_user_resources.h"
#include "../sdk/sacx/include/sacx_user_network.h"
#include "../sdk/sacx/include/sacx_user_http_request.h"
#include "../sdk/sacx/include/sacx_user_window.h"
static unsigned char user[512];static unsigned copies;
static unsigned random_calls;static int random_error;
static int utc_error;
int dihos_time_utc_ms(uint64_t *out){*out=1700000000123ull;return utc_error;}
int aarch64_user_vm_check_buffer(const aarch64_user_vm *vm,uint64_t va,uint64_t n,int write){
    (void)vm;(void)write;return va<0x1000 || n>sizeof(user) || va-0x1000>sizeof(user)-n?-14:0;
}
int dihos_entropy_fill(void *out,size_t bytes){++random_calls;if(random_error)return random_error;memset(out,0x5a,bytes);return 0;}
int aarch64_user_vm_copy_in(const aarch64_user_vm *vm,void *out,uint64_t va,uint64_t n){
    (void)vm;++copies;if(va<0x1000 || n>sizeof(user) || va-0x1000>sizeof(user)-n)return -14;memcpy(out,user+va-0x1000,(size_t)n);return 0;
}
int aarch64_user_vm_copy_out(const aarch64_user_vm *vm,uint64_t va,const void *in,uint64_t n){
    (void)vm;++copies;if(va<0x1000 || n>sizeof(user) || va-0x1000>sizeof(user)-n)return -14;memcpy(user+va-0x1000,in,(size_t)n);return 0;
}
int main(void){
    _Static_assert(sizeof(sacx_user_info)==32,"wire ABI size");
    dihos_user_sacx_reply r;uint64_t a[3]={0x1000,sizeof(sacx_user_info),0};
    dihos_user_sacx_service(0,0,1,SACX_USER_QUERY,a,123,&r);assert(!r.result && copies==1);
    sacx_user_info info;memcpy(&info,user,sizeof(info));assert(info.version==1 && !info.capabilities && info.max_buffer==256 && info.operations==0x3ffffffffffull);
    dihos_user_sacx_service(0,0,1,SACX_USER_WINDOW_REGION,a,123,&r);assert(r.result==-1 && !r.action);
    dihos_user_sacx_service(0,SACX_USER_CAP_SURFACE,1,SACX_USER_WINDOW_REGION,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_WINDOW);
    for(unsigned op=SACX_USER_WINDOW_CREATE;op<=SACX_USER_WINDOW_CLOSE;++op){
        copies=0;dihos_user_sacx_service(0,0,1,op,a,123,&r);assert(r.result==-1 && !r.action && !copies);
        dihos_user_sacx_service(0,SACX_USER_CAP_SURFACE,1,op,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_WINDOW && !copies);
    }
    copies=0;dihos_user_sacx_service(0,0,1,SACX_USER_LOG,a,123,&r);assert(r.result==-1 && !copies);
    dihos_user_sacx_service(0,0,1,SACX_USER_MONOTONIC_MS,a,123,&r);assert(r.result==-1);
    dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_MONOTONIC_MS,a,123,&r);assert(r.result==123);
    dihos_user_sacx_service(0,~0ull,2,SACX_USER_LOG,a,123,&r);assert(r.result==-93 && !copies);
    a[1]=257;dihos_user_sacx_service(0,SACX_USER_CAP_LOG,1,SACX_USER_LOG,a,123,&r);assert(r.result==-7 && !copies);
    a[0]=0;a[1]=3;dihos_user_sacx_service(0,SACX_USER_CAP_LOG,1,SACX_USER_LOG,a,123,&r);assert(r.result==-14 && !r.log_bytes);
    a[0]=0x1000;memcpy(user,"abc",3);dihos_user_sacx_service(0,SACX_USER_CAP_LOG,1,SACX_USER_LOG,a,123,&r);
    memset(user,0,3);assert(!r.result && r.log_bytes==3 && !strcmp(r.log,"abc"));
    a[0]=60001;dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_SLEEP_MS,a,123,&r);assert(r.result==-22 && !r.action);
    a[0]=5;dihos_user_sacx_service(0,0,1,SACX_USER_SLEEP_MS,a,123,&r);assert(r.result==-1 && !r.action);
    dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_SLEEP_MS,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_SLEEP && r.argument==5);
    dihos_user_sacx_service(0,0,1,SACX_USER_EXIT,a,123,&r);assert(r.action==DIHOS_SACX_EXIT && r.argument==5);
    dihos_user_sacx_service(0,0,1,SACX_USER_YIELD,a,123,&r);assert(!r.result && !r.action);
    dihos_user_sacx_service(0,~0ull,1,UINT64_MAX,a,123,&r);assert(r.result==-38 && !r.action);
    for(unsigned op=SACX_USER_MEMORY_RESERVE;op<=SACX_USER_MEMORY_RELEASE;++op){
        dihos_user_sacx_service(0,0,1,op,a,123,&r);assert(r.result==-1 && !r.action);
        dihos_user_sacx_service(0,SACX_USER_CAP_MEMORY,1,op,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_MEMORY);
    }
    for(unsigned op=SACX_USER_SPARSE_RESERVE;op<=SACX_USER_SPARSE_RELEASE;++op){
        dihos_user_sacx_service(0,0,1,op,a,123,&r);assert(r.result==-1 && !r.action);
        dihos_user_sacx_service(0,SACX_USER_CAP_MEMORY,1,op,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_MEMORY);
    }
    for(unsigned op=SACX_USER_THREAD_CREATE;op<=SACX_USER_WAKE32;++op){
        dihos_user_sacx_service(0,0,1,op,a,123,&r);assert(r.result==-1 && !r.action);
        dihos_user_sacx_service(0,SACX_USER_CAP_THREADS,1,op,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_THREAD);
    }
    for(unsigned op=SACX_USER_THREAD_PAUSE;op<=SACX_USER_THREAD_DETACH;++op){
        dihos_user_sacx_service(0,0,1,op,a,123,&r);assert(r.result==-1 && !r.action);
        dihos_user_sacx_service(0,SACX_USER_CAP_THREADS,1,op,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_THREAD);
        dihos_user_sacx_service(0,~0ull,2,op,a,123,&r);assert(r.result==-93 && !r.action);
    }
    dihos_user_sacx_service(0,0,1,SACX_USER_MEMORY_DECOMMIT,a,123,&r);assert(r.result==-1 && !r.action);
    dihos_user_sacx_service(0,SACX_USER_CAP_MEMORY,1,SACX_USER_MEMORY_DECOMMIT,a,123,&r);assert(!r.result && r.action==DIHOS_SACX_MEMORY);
    copies=0;a[0]=0x1000;a[1]=256;a[2]=0;
    dihos_user_sacx_service(0,0,1,SACX_USER_RANDOM,a,0,&r);assert(r.result==-1 && !random_calls && !copies);
    a[0]=0;dihos_user_sacx_service(0,SACX_USER_CAP_RANDOM,1,SACX_USER_RANDOM,a,0,&r);assert(r.result==-14 && !random_calls);
    a[0]=0x1000;a[1]=257;dihos_user_sacx_service(0,SACX_USER_CAP_RANDOM,1,SACX_USER_RANDOM,a,0,&r);assert(r.result==-7 && !random_calls);
    a[1]=256;a[2]=1;dihos_user_sacx_service(0,SACX_USER_CAP_RANDOM,1,SACX_USER_RANDOM,a,0,&r);assert(r.result==-22 && !random_calls);
    a[2]=0;random_error=-11;memset(user,0xa5,256);dihos_user_sacx_service(0,SACX_USER_CAP_RANDOM,1,SACX_USER_RANDOM,a,0,&r);
    assert(r.result==-11 && !copies && user[0]==0xa5);
    random_error=0;dihos_user_sacx_service(0,SACX_USER_CAP_RANDOM,1,SACX_USER_RANDOM,a,0,&r);assert(!r.result && copies==1 && user[0]==0x5a && user[255]==0x5a);
    a[0]=a[1]=a[2]=0;
    dihos_user_sacx_service(0,0,1,SACX_USER_UTC_MS,a,0,&r);assert(r.result==-1);
    utc_error=-1;dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_UTC_MS,a,0,&r);assert(r.result==-61);
    utc_error=0;dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_UTC_MS,a,0,&r);assert(r.result==1700000000123ull);
    a[1]=1;dihos_user_sacx_service(0,SACX_USER_CAP_CLOCK,1,SACX_USER_UTC_MS,a,0,&r);assert(r.result==-22);
    a[0]=a[1]=a[2]=0;dihos_user_sacx_service(0,0,1,SACX_USER_PROCESS_SELF,a,0,&r);assert(!r.result && r.action==DIHOS_SACX_IDENTITY);
    a[2]=1;dihos_user_sacx_service(0,0,1,SACX_USER_PROCESS_SELF,a,0,&r);assert(r.result==-22 && !r.action);
    _Static_assert(sizeof(sacx_user_resources)==80,"resource wire ABI size");
    a[0]=0x1000;a[1]=sizeof(sacx_user_resources);a[2]=0;
    for(unsigned i=0;i<3;++i){
        uint64_t caps=i==0?0:i==1?SACX_USER_CAP_CLOCK:SACX_USER_CAP_MEMORY;
        dihos_user_sacx_service(0,caps,1,SACX_USER_RESOURCES,a,0,&r);assert(r.result==-1 && !r.action);
    }
    uint64_t caps=SACX_USER_CAP_CLOCK|SACX_USER_CAP_MEMORY;
    dihos_user_sacx_service(0,caps,1,SACX_USER_RESOURCES,a,0,&r);assert(!r.result && r.action==DIHOS_SACX_RESOURCES);
    a[2]=1;dihos_user_sacx_service(0,caps,1,SACX_USER_RESOURCES,a,0,&r);assert(r.result==-22 && !r.action);
    a[2]=0;--a[1];dihos_user_sacx_service(0,caps,1,SACX_USER_RESOURCES,a,0,&r);assert(r.result==-22 && !r.action);
    a[1]=sizeof(sacx_user_resources);a[0]=0;
    dihos_user_sacx_service(0,caps,1,SACX_USER_RESOURCES,a,0,&r);assert(r.result==-14 && !r.action);
    a[0]=0x1000;dihos_user_sacx_service(0,caps,2,SACX_USER_RESOURCES,a,0,&r);assert(r.result==-93 && !r.action);
    for(unsigned op=SACX_USER_HTTP_START;op<=SACX_USER_HTTP_START_MANUAL;++op){
        dihos_user_sacx_service(0,0,1,op,a,0,&r);assert(r.result==-1 && !r.action);
        dihos_user_sacx_service(0,SACX_USER_CAP_NETWORK,1,op,a,0,&r);assert(!r.result && r.action==DIHOS_SACX_NETWORK);
    }
    dihos_user_sacx_service(0,0,1,SACX_USER_HTTP_REQUEST,a,0,&r);assert(r.result==-1 && !r.action);
    dihos_user_sacx_service(0,SACX_USER_CAP_NETWORK,1,SACX_USER_HTTP_REQUEST,a,0,&r);assert(!r.result && r.action==DIHOS_SACX_NETWORK);
    dihos_user_sacx_service(0,0,1,SACX_USER_HTTP_SEND,a,0,&r);assert(r.result==-1 && !r.action);
    dihos_user_sacx_service(0,SACX_USER_CAP_NETWORK,1,SACX_USER_HTTP_SEND,a,0,&r);assert(!r.result && r.action==DIHOS_SACX_NETWORK);
    puts("PASS: SACX ABI negotiation, denied capabilities, owned buffers, bounded lengths, unknown calls and nonblocking sleep replies");
}
