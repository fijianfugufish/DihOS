#include "sacx_user_api.h"
#include "sacx_user_sparse_vm.h"
#include "sacx_user_resources.h"
#ifndef USER_SPARSE_KIND
#define USER_SPARSE_KIND 1
#endif
#define GIB (1ull<<30)
static void require(int ok,const char *message){if(!ok)sacx_user_exit(164,message);}
void _start(void){
    sacx_user_info info;
    require(!sacx_user_call(SACX_USER_QUERY,(uintptr_t)&info,sizeof(info),0) &&
        (info.operations&(1ull<<SACX_USER_SPARSE_RESERVE)),"sparse VM unavailable");
    sacx_user_resources before,after;
    require(!sacx_user_resources_get(&before),"sparse accounting unavailable");
    int64_t raw=sacx_user_sparse_reserve(GIB,GIB,1);
    require(raw>0 && !((uint64_t)raw&(GIB-1)),"sparse GiB aligned reservation failed");
    volatile unsigned char *p=(volatile unsigned char*)(uintptr_t)raw;
    require(!sacx_user_resources_get(&after) && !after.heap_committed_bytes &&
        after.private_bytes-before.private_bytes<=4096 && after.heap_reserved_bytes>=GIB,
        "virtual reserve allocated payload backing");
    require(!sacx_user_call(SACX_USER_MEMORY_COMMIT,raw,65536,3),"sparse first commit failed");
    for(unsigned i=0;i<65536;++i)require(!p[i],"sparse page not zero");
    p[0]=42;p[65535]=91;
#if USER_SPARSE_KIND==2
    p[-1]=1; /* Hardware lower guard fault, isolated by the kernel. */
#elif USER_SPARSE_KIND==3
    require(!sacx_user_call(SACX_USER_MEMORY_DECOMMIT,raw,65536,0),"sparse decommit failed");
    p[0]=1;
#elif USER_SPARSE_KIND==4
    require(!sacx_user_call(SACX_USER_MEMORY_PROTECT,raw,4096,1),"sparse protection failed");
    p[0]=1;
#elif USER_SPARSE_KIND==5
    sacx_user_log("SACX sparse VM: resident pages ready for cancellation");
    for(;;){}
#elif USER_SPARSE_KIND==6
    p[0]=0xc0;p[1]=0x03;p[2]=0x5f;p[3]=0xd6; /* AArch64 RET; heap must be NX. */
    ((void(*)(void))(uintptr_t)raw)();
#else
    require(!sacx_user_call(SACX_USER_MEMORY_COMMIT,raw+GIB-4096,4096,3),"sparse distant commit failed");
    p[GIB-1]=17;
    require(!sacx_user_call(SACX_USER_MEMORY_COMMIT,raw+GIB/2,4096,3),"sparse middle commit failed");
    p[GIB/2]=73;
    require(!sacx_user_resources_get(&after) && after.heap_committed_bytes==73728 &&
        after.private_bytes-before.private_bytes<1024*1024,"sparse accounting charged virtual span");
    require(sacx_user_call(SACX_USER_MEMORY_COMMIT,raw,4096,3)==-17 && p[0]==42,
        "duplicate commit modified live data");
    require(!sacx_user_call(SACX_USER_MEMORY_PROTECT,raw,4096,1) &&
        sacx_user_call(SACX_USER_QUERY,raw,sizeof(info),0)==-14 && p[0]==42,
        "sparse read-only boundary failed");
    require(!sacx_user_call(SACX_USER_MEMORY_PROTECT,raw,4096,3),"sparse RW restore failed");
    require(!sacx_user_call(SACX_USER_MEMORY_DECOMMIT,raw+GIB/2,4096,0) &&
        !sacx_user_call(SACX_USER_MEMORY_COMMIT,raw+GIB/2,4096,3) && !p[GIB/2],
        "sparse decommit/recommit did not zero");
    require(p[65535]==91 && p[GIB-1]==17,"sparse offsets corrupted");
    require(sacx_user_call(SACX_USER_MEMORY_PROTECT,raw,4096,7)==-22,
        "sparse executable memory accepted");
    require(sacx_user_sparse_release_step(raw+4096)==-22,"interior sparse release accepted");
    require(sacx_user_sparse_release_step(raw)==-11,"sparse release did not bound its first batch");
    require(sacx_user_call(SACX_USER_MEMORY_COMMIT,raw,4096,3)==-16,"closing sparse range accepted mutations");
    require(!sacx_user_sparse_release(raw),"incremental sparse release failed");
    require(!sacx_user_resources_get(&after) && !after.heap_reserved_bytes && !after.heap_committed_bytes,
        "sparse release left reservations");
    require(sacx_user_sparse_release(raw)==-22,"sparse double release accepted");
    for(unsigned repeat=0;repeat<12;++repeat){
        raw=sacx_user_sparse_reserve(4*GIB,4*GIB,0);require(raw>0,"sparse large VA reuse failed");
        require(!sacx_user_call(SACX_USER_MEMORY_COMMIT,raw+repeat*2097152,4096,3),"sparse sparse-offset reuse failed");
        require(!sacx_user_sparse_release(raw),"sparse reuse cleanup failed");
    }
    sacx_user_exit(101,"SACX sparse VM: GiB reservations, distant pages, zeroing, protection and release OK");
#endif
    sacx_user_exit(164,"sparse fault or infinite loop unexpectedly returned");
}
