#include "config.h"
#include <wtf/OSAllocator.h>
#include "sacx_user_vm.h"
#include "sacx_user_api.h"
#include "sacx_user_resources.h"
#include "vm_failure_report.h"
namespace WTF {
static void checked(int result,const char* operation,void* base,size_t bytes,void* caller){
    if(!result)return;
    sacx_user_resources resources{};
    (void)sacx_user_resources_get(&resources);
    char message[257];
    DihOSVM::describe(message,{operation,result,(uintptr_t)base,bytes,(uintptr_t)caller,
        resources.private_bytes,resources.heap_reserved_bytes,resources.heap_committed_bytes,resources.heap_limit_bytes});
    sacx_user_exit(175,message);
}
void* OSAllocator::tryReserveUncommittedAligned(size_t bytes,size_t alignment,Usage,void *address,bool,bool executable,bool cage,unsigned guards){
    if(address || executable || cage)return nullptr;
    return dihos_user_vm_reserve(bytes,alignment,guards);
}
void* OSAllocator::tryReserveUncommitted(size_t bytes,Usage usage,void *address,bool writable,bool executable,bool cage,unsigned guards){
    return tryReserveUncommittedAligned(bytes,4096,usage,address,writable,executable,cage,guards);
}
void* OSAllocator::reserveUncommitted(size_t bytes,Usage usage,void *address,bool writable,bool executable,bool cage,unsigned guards){
    void *base=tryReserveUncommitted(bytes,usage,address,writable,executable,cage,guards);
    if(!base)sacx_user_exit(175,"WTF native reservation failed");return base;
}
void* OSAllocator::tryReserveAndCommitImpl(size_t bytes,Usage usage,void *address,bool writable,bool executable,bool cage,unsigned guards,bool uncommitted){
    void *base=tryReserveUncommitted(bytes,usage,address,writable,executable,cage,guards);
    if(base && !uncommitted && dihos_user_vm_commit(base,bytes,writable)){
        checked(dihos_user_vm_release(base,bytes),"rollback",base,bytes,__builtin_return_address(0));return nullptr;
    }
    return base;
}
void* OSAllocator::tryReserveAndCommit(size_t bytes,Usage usage,void *address,bool writable,bool executable,bool cage,unsigned guards){
    return tryReserveAndCommitImpl(bytes,usage,address,writable,executable,cage,guards,false);
}
void* OSAllocator::reserveAndCommit(size_t bytes,Usage usage,void *address,bool writable,bool executable,bool cage,unsigned guards){
    void *base=tryReserveAndCommit(bytes,usage,address,writable,executable,cage,guards);
    if(!base)sacx_user_exit(175,"WTF native allocation failed");return base;
}
void OSAllocator::releaseDecommitted(void *base,size_t bytes,unsigned){checked(dihos_user_vm_release(base,bytes),"release",base,bytes,__builtin_return_address(0));}
void OSAllocator::commit(void *base,size_t bytes,bool writable,bool executable){
    if(executable)sacx_user_exit(175,"WTF executable memory denied");checked(dihos_user_vm_commit(base,bytes,writable),"commit",base,bytes,__builtin_return_address(0));
}
void OSAllocator::decommit(void *base,size_t bytes){checked(dihos_user_vm_decommit(base,bytes),"decommit",base,bytes,__builtin_return_address(0));}
bool OSAllocator::tryProtect(void *base,size_t bytes,bool readable,bool writable){return !dihos_user_vm_protect(base,bytes,readable,writable);}
void OSAllocator::protect(void *base,size_t bytes,bool readable,bool writable){checked(dihos_user_vm_protect(base,bytes,readable,writable),"protect",base,bytes,__builtin_return_address(0));}
void OSAllocator::zeroFill(void *base,size_t bytes){
    auto p=static_cast<unsigned char*>(base);
    while(bytes){size_t n=bytes<65536?bytes:65536;__builtin_memset(p,0,n);p+=n;bytes-=n;sacx_user_yield();}
}
// Advisory only: preserve live contents. Explicit decommit returns backing.
void OSAllocator::hintMemoryNotNeededSoon(void*,size_t){}
}
