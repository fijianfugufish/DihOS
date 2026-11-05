#include "sacx_user_api.h"
#include <bmalloc/VMAllocate.h>
#include <wtf/OSAllocator.h>
static void check(bool value){if(!value)sacx_user_exit(176,"WebKit native VM adapter fixture failed");}
extern "C" int dihos_user_main(const sacx_api *api){
    constexpr size_t bytes=256*1024;
    auto p=static_cast<unsigned char*>(bmalloc::tryVMAllocate(1024*1024,bytes));
    check(p && !((uintptr_t)p&((1024*1024)-1)));
    p[0]=p[bytes-1]=0xa5;
    bmalloc::vmRevokePermissions(p,bytes);bmalloc::vmAllocatePhysicalPages(p,bytes);
    check(p[0]==0xa5 && p[bytes-1]==0xa5);
    bmalloc::vmZeroAndPurge(p,bytes,bmalloc::VMTag::Malloc);check(!p[0] && !p[bytes-1]);
    p[0]=0xa5;bmalloc::vmDeallocatePhysicalPages(p,bytes);bmalloc::vmAllocatePhysicalPages(p,bytes);
    check(!p[0]);bmalloc::vmDeallocate(p,bytes);
    using Allocator=WTF::OSAllocator;
    check(!Allocator::tryReserveAndCommit(4096,Allocator::UnknownUsage,nullptr,true,true));
    check(!Allocator::tryReserveUncommitted(4096,Allocator::UnknownUsage,(void*)4096));
    auto q=static_cast<unsigned char*>(Allocator::tryReserveUncommittedAligned(bytes,1024*1024,Allocator::UnknownUsage,nullptr,true,false,false,2));
    check(q && !((uintptr_t)q&((1024*1024)-1)));
    Allocator::commit(q,bytes,true,false);q[0]=0xa5;
    Allocator::protect(q,bytes,true,false);check(q[0]==0xa5);
    Allocator::protect(q,bytes,true,true);Allocator::zeroFill(q,bytes);check(!q[0]);
    q[0]=0xa5;Allocator::decommit(q,bytes);Allocator::commit(q,bytes,true,false);check(!q[0]);
    Allocator::releaseDecommitted(q,bytes,2);
    constexpr size_t sparseBytes=1ull<<30;
    auto large=static_cast<unsigned char*>(Allocator::tryReserveUncommittedAligned(sparseBytes,sparseBytes,Allocator::UnknownUsage));
    check(large && !((uintptr_t)large&(sparseBytes-1)));
    Allocator::commit(large+sparseBytes-4096,4096,true,false);large[sparseBytes-1]=42;
    Allocator::decommit(large+sparseBytes-4096,4096);
    Allocator::commit(large+sparseBytes-4096,4096,true,false);check(!large[sparseBytes-1]);
    Allocator::releaseDecommitted(large,sparseBytes,0);
    api->log("WebKit VM: actual sparse bmalloc/WTF adapters, GiB alignment, discard and protection OK");return 91;
}
