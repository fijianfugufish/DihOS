#include "sacx_user_vm.h"
#include "sacx_user_api.h"
#include "sacx_user_sparse_vm.h"
#include "dihos_user_sync.h"
#include <stdlib.h>
namespace {
constexpr size_t page=4096,maximum=4ull*1024*1024*1024;
// Bookkeeping only. Kernel ownership/quota/permissions remain authoritative.
// Allocate bitmaps only for touched 1 MiB spans, never for the virtual size.
struct Bits {Bits *next;size_t index;uint64_t words[4];};
struct Region {uintptr_t base;size_t bytes;Bits *bits;bool closing;};
Region regions[16];uint32_t mutex;
bool valid(uintptr_t address,size_t bytes){return bytes && !((address|bytes)&(page-1)) && address+bytes>=address;}
Region *find(uintptr_t address,size_t bytes){
    for(auto &r:regions)if(r.base && address>=r.base && address-r.base<r.bytes && bytes<=r.bytes-(address-r.base))return &r;
    return nullptr;
}
Bits *bits(Bits *list,size_t index){for(;list;list=list->next)if(list->index==index)return list;return nullptr;}
bool committed(const Region &r,size_t index){auto b=bits(r.bits,index/256);return b && (b->words[(index%256)/64]&(1ull<<(index%64)));}
void free_bits(Bits *list){while(list){auto next=list->next;free(list);list=next;}}
void trim_bits(Region &r){
    auto at=&r.bits;
    while(*at){auto b=*at;if(!(b->words[0]|b->words[1]|b->words[2]|b->words[3])){*at=b->next;free(b);}else at=&b->next;}
}
// All callers hold the userspace registry lock. It does not confer kernel authority.
int change(Region &r,uintptr_t address,size_t bytes,int kind,unsigned permissions){
    if(r.closing)return -16;
    size_t first=(address-r.base)/page,end=first+bytes/page;
    if(kind==1)for(size_t i=first;i<end;++i)if(!committed(r,i))return -14;
    for(size_t i=first;i<end;){
        bool was_committed=committed(r,i);size_t next=i+1;
        while(next<end && next-i<16 && committed(r,next)==was_committed)++next;
        if(kind==2 && !was_committed){i=next;continue;}
        unsigned op=kind==2?SACX_USER_MEMORY_DECOMMIT:
            (kind==1 || was_committed?SACX_USER_MEMORY_PROTECT:SACX_USER_MEMORY_COMMIT);
        Bits *pending=nullptr;
        if(op==SACX_USER_MEMORY_COMMIT)for(size_t j=i;j<next;++j){
            if(bits(r.bits,j/256) || bits(pending,j/256))continue;
            auto node=static_cast<Bits*>(calloc(1,sizeof(Bits)));
            if(!node){free_bits(pending);return -12;}
            node->index=j/256;node->next=pending;pending=node;
        }
        int64_t result=sacx_user_call(op,r.base+i*page,(next-i)*page,permissions);
        if(result){free_bits(pending);return static_cast<int>(result);}
        while(pending){auto node=pending;pending=node->next;node->next=r.bits;r.bits=node;}
        for(size_t j=i;j<next;++j){
            auto b=bits(r.bits,j/256);
            if(kind==2)b->words[(j%256)/64]&=~(1ull<<(j%64));
            else if(!kind)b->words[(j%256)/64]|=1ull<<(j%64);
        }
        if(kind==2)trim_bits(r);
        i=next;
    }
    return 0;
}
}
extern "C" void *dihos_user_vm_reserve(size_t bytes,size_t alignment,unsigned guards){
    if(!valid(0,bytes) || alignment<page || alignment>maximum || (alignment&(alignment-1)) || guards>16)return nullptr;
    if(bytes>maximum)return nullptr;
    dihos_user_lock lock(mutex);Region *slot=nullptr;
    for(auto &r:regions)if(!r.base){slot=&r;break;}
    if(!slot)return nullptr;
    int64_t raw=sacx_user_sparse_reserve(bytes,alignment,guards);
    if(raw<0)return nullptr;
    *slot=Region{};slot->base=static_cast<uintptr_t>(raw);slot->bytes=bytes;
    return reinterpret_cast<void*>(slot->base);
}
extern "C" int dihos_user_vm_commit(void *base,size_t bytes,int writable){
    uintptr_t address=reinterpret_cast<uintptr_t>(base);if(!valid(address,bytes))return -22;
    dihos_user_lock lock(mutex);auto r=find(address,bytes);if(!r)return -14;
    return change(*r,address,bytes,0,writable?3:1);
}
extern "C" int dihos_user_vm_decommit(void *base,size_t bytes){
    uintptr_t address=reinterpret_cast<uintptr_t>(base);if(!valid(address,bytes))return -22;
    dihos_user_lock lock(mutex);auto r=find(address,bytes);if(!r)return -14;
    return change(*r,address,bytes,2,0);
}
extern "C" int dihos_user_vm_protect(void *base,size_t bytes,int readable,int writable){
    uintptr_t address=reinterpret_cast<uintptr_t>(base);if(!valid(address,bytes) || (writable && !readable))return -22;
    dihos_user_lock lock(mutex);auto r=find(address,bytes);if(!r)return -14;
    return change(*r,address,bytes,1,readable?(writable?3:1):0);
}
extern "C" int dihos_user_vm_zero(void *base,size_t bytes){
    uintptr_t address=reinterpret_cast<uintptr_t>(base);if(!valid(address,bytes))return -22;
    dihos_user_lock lock(mutex);auto r=find(address,bytes);if(!r)return -14;
    // Decommit then recommit, never write through a read-only/no-access alias.
    int result=change(*r,address,bytes,2,0);if(result)return result;
    return change(*r,address,bytes,0,3);
}
extern "C" int dihos_user_vm_release(void *base,size_t bytes){
    dihos_user_lock lock(mutex);auto r=find(reinterpret_cast<uintptr_t>(base),bytes);
    if(!r || r->base!=reinterpret_cast<uintptr_t>(base) || r->bytes!=bytes)return -22;
    r->closing=true;
    int64_t result=sacx_user_sparse_release(r->base);
    if(!result){free_bits(r->bits);*r=Region{};}
    return static_cast<int>(result);
}
