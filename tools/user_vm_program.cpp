#include "sacx_user_vm.h"
#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include "sacx_user_resources.h"
#ifndef USER_VM_KIND
#define USER_VM_KIND 1
#endif
static void check(bool value){if(!value)sacx_user_exit(176,"SACX VM fixture failed");}
static uint64_t worker(uint64_t value){
    for(unsigned repeat=0;repeat<3;++repeat){
        auto p=static_cast<unsigned char*>(dihos_user_vm_reserve(65536,65536,1));check(p && !((uintptr_t)p&65535));
        check(!dihos_user_vm_commit(p,65536,1));p[65535]=(unsigned char)value;
        check(!dihos_user_vm_zero(p,65536) && !p[65535]);
        check(!dihos_user_vm_release(p,65536));
    }
    return value;
}
extern "C" int dihos_user_main(const sacx_api *api){
    constexpr size_t bytes=512*1024;
    check(!dihos_user_vm_reserve(bytes,8193,0) && !dihos_user_vm_reserve((4ull<<30)+4096,4096,0));
    auto formerlyTooLarge=dihos_user_vm_reserve(16*1024*1024,8192,1);
    check(formerlyTooLarge && !dihos_user_vm_release(formerlyTooLarge,16*1024*1024));
    auto p=static_cast<unsigned char*>(dihos_user_vm_reserve(bytes,2*1024*1024,2));
    check(p && !((uintptr_t)p&((2*1024*1024)-1)));
    check(!dihos_user_vm_commit(p,bytes,1));
    for(size_t i=0;i<bytes;++i){check(!p[i]);p[i]=0xa5;}
    if(USER_VM_KIND==2){volatile auto bad=p[-1];(void)bad;return 177;}
    check(!dihos_user_vm_decommit(p+63*4096,8192));
    if(USER_VM_KIND==3){volatile auto bad=p[63*4096];(void)bad;return 177;}
    check(!dihos_user_vm_decommit(p+63*4096,8192)); // idempotent adapter discard
    check(!dihos_user_vm_commit(p+63*4096,8192,1));
    for(size_t i=63*4096;i<65*4096;++i)check(!p[i]);
    check(p[62*4096]==0xa5 && p[65*4096]==0xa5);
    check(!dihos_user_vm_protect(p,bytes,1,0));
    if(USER_VM_KIND==4){p[0]=0;return 177;}
    check(!dihos_user_vm_protect(p,bytes,0,0));
    check(!dihos_user_vm_commit(p,bytes,1) && p[0]==0xa5); // recommit already mapped preserves data
    check(!dihos_user_vm_zero(p,bytes));for(size_t i=0;i<bytes;++i)check(!p[i]);
    check(dihos_user_vm_release(p+4096,bytes-4096)==-22);
    check(dihos_user_vm_protect(p,bytes,0,1)==-22);
    check(!dihos_user_vm_release(p,bytes) && dihos_user_vm_release(p,bytes)==-22);
    auto a=sacx_user_thread_create(worker,1),b=sacx_user_thread_create(worker,2);check(a>0 && b>0);
    uint64_t result;check(!sacx_user_thread_join(a,&result,60000) && result==1);
    check(!sacx_user_thread_join(b,&result,60000) && result==2);
    void *slots[16];unsigned count=0;
    while(count<16 && (slots[count]=dihos_user_vm_reserve(4096,4096,0)))++count;
    check(count==16 && !dihos_user_vm_reserve(4096,4096,0));
    for(unsigned i=0;i<count;++i)check(!dihos_user_vm_release(slots[i],4096));
    sacx_user_resources before,after;check(!sacx_user_resources_get(&before));
    constexpr size_t largeBytes=1ull<<30;
    auto large=static_cast<unsigned char*>(dihos_user_vm_reserve(largeBytes,largeBytes,1));
    check(large && !((uintptr_t)large&(largeBytes-1)));
    check(!sacx_user_resources_get(&after) && after.private_bytes-before.private_bytes<=4096);
    check(!dihos_user_vm_commit(large+largeBytes-4096,4096,1));large[largeBytes-1]=91;
    check(!dihos_user_vm_commit(large+255*4096,8192,1)); // bitmap-node boundary
    large[255*4096]=42;large[256*4096]=73;
    check(!dihos_user_vm_commit(large+255*4096,8192,0) && large[255*4096]==42 && large[256*4096]==73);
    check(!dihos_user_vm_commit(large+255*4096,8192,1));
    check(!dihos_user_vm_decommit(large+255*4096,8192));
    check(!dihos_user_vm_decommit(large+255*4096,8192));
    check(!dihos_user_vm_commit(large+255*4096,8192,1) && !large[255*4096] && !large[256*4096]);
    check(large[largeBytes-1]==91 && !sacx_user_resources_get(&after) &&
          after.private_bytes-before.private_bytes<1024*1024);
    check(!dihos_user_vm_release(large,largeBytes));
    check(!sacx_user_resources_get(&after) && after.heap_reserved_bytes-before.heap_reserved_bytes<1024*1024);
    api->log("SACX VM: sparse GiB alignment, guards, discard/recommit, protection and concurrent cleanup OK");return 90;
}
