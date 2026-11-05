// EL0 allocator; contention parks through process-local SACX wait/wake.
#include "dihos_user_allocator.h"
#include "tlsf.h"
extern "C" int dihos_user_allocator_printf(const char*,...);
namespace {
// Keep room for TLSF metadata/alignment inside one bounded owned region.
constexpr size_t pool_bytes=256*1024,max_pools=32,max_pool_bytes=64*1024*1024,max_allocation=max_pool_bytes-65536;
struct Pool {void *backing;pool_t pool;size_t live,bytes;};
Pool pools[max_pools];
tlsf_t control;
dihos_user_allocator_stats stats;
uint32_t mutex_word;
// Failure diagnostics: 1=request bounds, 2=live pool slots, 3=backing/quota,
// 4=TLSF could not fit the request even after bounded growth.
unsigned failure_reason;
struct Lock {
    Lock(){
        uint32_t expected=0;
        if(__atomic_compare_exchange_n(&mutex_word,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
        while(__atomic_exchange_n(&mutex_word,2,__ATOMIC_ACQUIRE)){
            int rc=dihos_user_allocator_wait(&mutex_word,2);
            if(rc && rc!=-11 && rc!=-110)dihos_user_allocator_panic("Allocator wait failed");
        }
    }
    ~Lock(){if(__atomic_exchange_n(&mutex_word,0,__ATOMIC_RELEASE)==2 && dihos_user_allocator_wake(&mutex_word)<0)dihos_user_allocator_panic("Allocator wake failed");}
};
Pool *owner(const void *p){
    uintptr_t address=reinterpret_cast<uintptr_t>(p);
    for(auto &pool:pools)if(pool.backing && address>=reinterpret_cast<uintptr_t>(pool.pool) &&
       address-reinterpret_cast<uintptr_t>(pool.backing)<pool.bytes)return &pool;
    return nullptr;
}
void release(Pool &);
bool reclaim_idle(){
    bool reclaimed=false;
    for(size_t i=1;i<max_pools;++i)if(pools[i].backing && !pools[i].live){
        tlsf_remove_pool(control,pools[i].pool);release(pools[i]);reclaimed=true;
    }
    return reclaimed;
}
bool grow(size_t needed){
    Pool *slot=nullptr;for(auto &p:pools)if(!p.backing){slot=&p;break;}
    // Cold small pools must not exhaust all region slots while larger working
    // sets arrive. Preserve the control pool and every pool with live objects.
    if(!slot)for(size_t i=1;i<max_pools;++i)if(!pools[i].live){
        tlsf_remove_pool(control,pools[i].pool);release(pools[i]);slot=&pools[i];break;
    }
    if(!slot){failure_reason=2;return false;}
    size_t index=(size_t)(slot-pools),bytes=pool_bytes;
    if(index>=8){bytes=1024*1024;for(size_t i=8;i<index && bytes<max_pool_bytes;++i)bytes*=2;}
    while(bytes<needed && bytes<max_pool_bytes)bytes*=2;
    if(bytes<needed)return false;
    // The control/root pool stays small even when the first allocation is large.
    if(!control && bytes!=pool_bytes){if(!grow(1))return false;return grow(needed);}
    void *backing=nullptr;
    bool reclaimed=false;
    for(;;){
        if(!dihos_user_region_acquire((uint32_t)bytes,&backing))break;
        // Free cached, wholly idle pools before failing under quota/physical
        // pressure. Never relocate live objects or discard the TLSF control.
        // Re-select the slot after reclamation; it may have released it.
        if(!reclaimed){reclaimed=true;if(reclaim_idle())return grow(needed);}
        // Keep attempts bounded by the geometric sizes. A preferred growth
        // block may exceed the remaining quota while the requested block fits.
        if(bytes/2<needed || bytes/2<pool_bytes){failure_reason=3;return false;}
        bytes/=2;
    }
    pool_t pool;
    if(!control){
        if(tlsf_size()+tlsf_pool_overhead()>=pool_bytes)dihos_user_allocator_panic("TLSF metadata exceeds pool");
        control=tlsf_create(backing);
        if(!control)dihos_user_allocator_panic("TLSF control initialization failed");
        pool=tlsf_add_pool(control,static_cast<unsigned char*>(backing)+tlsf_size(),bytes-tlsf_size());
    }else pool=tlsf_add_pool(control,backing,bytes);
    if(!pool)dihos_user_allocator_panic("TLSF pool initialization failed");
    *slot={backing,pool,0,bytes};stats.reserved+=bytes;++stats.pools;return true;
}
void release(Pool &p){
    if(dihos_user_region_release(p.backing))dihos_user_allocator_panic("SACX pool release failed");
    stats.reserved-=p.bytes;p={};--stats.pools;
}
}
static void *arena_memalign(size_t alignment,size_t bytes){
    failure_reason=1;
    if(!bytes || !alignment || (alignment&(alignment-1)) || alignment>65536 || bytes>max_allocation)return nullptr;
    if(alignment<16)alignment=16;
    // The TLSF alignment gap is bounded; reject requests that cannot fit in a
    // whole secondary pool before repeatedly acquiring unusable backing.
    size_t adjusted=(bytes+tlsf_align_size()-1)&~(tlsf_align_size()-1);
    size_t needed=(adjusted+alignment+tlsf_block_size_min()+tlsf_alloc_overhead()+alignment-1)&~(alignment-1);
    if(needed>max_pool_bytes-8192)return nullptr;
    bool grew=false;
    for(;;){
        void *pointer=control?tlsf_memalign(control,alignment,bytes):nullptr;
        if(pointer){
            Pool *pool=owner(pointer);if(!pool)dihos_user_allocator_panic("TLSF returned foreign memory");
            ++pool->live;++stats.allocations;stats.live_bytes+=tlsf_block_size(pointer);
            if(stats.live_bytes>stats.peak_bytes)stats.peak_bytes=stats.live_bytes;
            return pointer;
        }
        // A fresh pool which still cannot satisfy TLSF's bin/alignment
        // rounding must fail, rather than recycle empty pools indefinitely.
        if(grew){failure_reason=4;return nullptr;}
        if(!grow(needed+8192))return nullptr;
        grew=true;
    }
}
static void arena_free(void *pointer){
    if(!pointer)return;
    Pool *pool=owner(pointer);if(!pool || !pool->live)dihos_user_allocator_panic("Invalid arena free");
    size_t bytes=tlsf_block_size(pointer);
    if(!stats.allocations || bytes>stats.live_bytes)dihos_user_allocator_panic("Invalid arena accounting");
    tlsf_free(control,pointer);--pool->live;--stats.allocations;stats.live_bytes-=bytes;
}
extern "C" void *dihos_user_memalign(size_t alignment,size_t bytes){Lock lock;return arena_memalign(alignment,bytes);}
extern "C" void *dihos_user_malloc(size_t bytes){
    Lock lock;void *p=arena_memalign(16,bytes);
    if(!p && bytes>=1024*1024)dihos_user_allocator_failure(bytes,stats.reserved,stats.live_bytes,(unsigned)stats.pools,failure_reason);
    return p;
}
extern "C" void dihos_user_free(void *pointer){Lock lock;arena_free(pointer);}
extern "C" void *dihos_user_calloc(size_t count,size_t bytes){
    Lock lock;
    if(count && bytes>SIZE_MAX/count)return nullptr;
    size_t total=count*bytes;auto p=static_cast<unsigned char*>(arena_memalign(16,total));
    if(p)for(size_t i=0;i<total;++i)p[i]=0;return p;
}
extern "C" void *dihos_user_realloc(void *pointer,size_t bytes){
    Lock lock;
    if(!pointer)return arena_memalign(16,bytes);
    if(!bytes){arena_free(pointer);return nullptr;}
    if(!owner(pointer))dihos_user_allocator_panic("Invalid arena realloc");
    size_t old=tlsf_block_size(pointer);
    if(bytes<=old)return pointer;
    auto replacement=static_cast<unsigned char*>(arena_memalign(16,bytes));if(!replacement)return nullptr;
    auto source=static_cast<unsigned char*>(pointer);for(size_t i=0;i<old;++i)replacement[i]=source[i];
    arena_free(pointer);return replacement;
}
extern "C" void *dihos_user_aligned_alloc(size_t alignment,size_t bytes){
    if(!alignment || bytes%alignment)return nullptr;
    return dihos_user_memalign(alignment,bytes);
}
extern "C" int dihos_user_allocator_trim(){
    Lock lock;
    for(size_t i=1;i<max_pools;++i)if(pools[i].backing && !pools[i].live){
        tlsf_remove_pool(control,pools[i].pool);release(pools[i]);
    }
    if(control && !stats.allocations){
        tlsf_destroy(control);control=nullptr;release(pools[0]);
    }
    return 0;
}
extern "C" int dihos_user_allocator_check(){
    Lock lock;
    if(!control)return stats.allocations || stats.reserved;
    int result=tlsf_check(control);
    for(auto &p:pools)if(p.backing)result|=tlsf_check_pool(p.pool);
    return result;
}
extern "C" void dihos_user_allocator_get_stats(dihos_user_allocator_stats *out){Lock lock;if(out)*out=stats;}
