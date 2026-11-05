#include "dihos_user_allocator.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#if defined(_WIN32)
#include <malloc.h>
#endif
static void *backing[32];
static size_t backing_bytes[32],total_backing,total_limit=SIZE_MAX;
static unsigned active,acquires;
static bool fail_acquire;
static uint32_t acquire_limit=64*1024*1024;static unsigned rejected_large;
static std::mutex wait_mutex;
static std::condition_variable wait_condition;
extern "C" int dihos_user_allocator_wait(uint32_t *word,uint32_t expected){
    std::unique_lock lock(wait_mutex);
    wait_condition.wait_for(lock,std::chrono::milliseconds(100),[&]{return __atomic_load_n(word,__ATOMIC_ACQUIRE)!=expected;});return 0;
}
extern "C" int dihos_user_allocator_wake(uint32_t*){std::lock_guard lock(wait_mutex);wait_condition.notify_all();return 0;}
extern "C" int dihos_user_region_acquire(uint32_t bytes,void **out){
    assert(bytes>=256*1024 && bytes<=64*1024*1024);*out=nullptr;
    if(bytes>acquire_limit){++rejected_large;return -12;}
    if(total_backing>total_limit || bytes>total_limit-total_backing)return -12;
    if(fail_acquire || active==32)return -12;
    for(auto &slot:backing)if(!slot){
#if defined(_WIN32)
        slot=_aligned_malloc(bytes,4096);
#else
        slot=std::aligned_alloc(4096,bytes);
#endif
        assert(slot);backing_bytes[&slot-backing]=bytes;total_backing+=bytes;std::memset(slot,0xa5,bytes);*out=slot;++active;++acquires;return 0;
    }
    std::abort();
}
extern "C" int dihos_user_region_release(void *pointer){
    for(auto &slot:backing)if(slot==pointer){
        total_backing-=backing_bytes[&slot-backing];backing_bytes[&slot-backing]=0;
#if defined(_WIN32)
        _aligned_free(slot);
#else
        std::free(slot);
#endif
        slot=nullptr;--active;return 0;
    }
    std::abort();
}
extern "C" __attribute__((noreturn)) void dihos_user_allocator_panic(const char *reason){std::fprintf(stderr,"allocator panic: %s\n",reason);std::abort();}
extern "C" int dihos_user_allocator_printf(const char *format,...){va_list args;va_start(args,format);int n=std::vprintf(format,args);va_end(args);return n;}
extern "C" void dihos_user_allocator_failure(size_t,uint64_t,uint64_t,unsigned,unsigned){}
static void empty(){
    assert(!dihos_user_allocator_check());dihos_user_allocator_stats s;dihos_user_allocator_get_stats(&s);
    assert(!s.allocations && !s.live_bytes);assert(!dihos_user_allocator_trim());
    dihos_user_allocator_get_stats(&s);assert(!active && !total_backing && !s.reserved && !s.pools && !dihos_user_allocator_check());
}
int main(){
    // Keep one live object in each of sixteen small pools, then request the
    // exact allocation size from the guest failure. No pool is recyclable.
    acquire_limit=256*1024;
    void *pinned[16];
    for(unsigned i=0;i<16;++i){pinned[i]=dihos_user_malloc(200*1024);assert(pinned[i]);std::memset(pinned[i],i+1,200*1024);}
    assert(active==16);acquire_limit=64*1024*1024;
    unsigned before=acquires;
    auto large_script=dihos_user_malloc(0xa46bbf);
#ifdef EXPECT_LEGACY_POOL_FAILURE
    assert(!large_script && active==16 && acquires==before);
#else
    assert(large_script && active==17 && acquires>before);
    std::memset(large_script,0x5b,0xa46bbf);dihos_user_free(large_script);
#endif
    for(unsigned i=0;i<16;++i){for(unsigned j=0;j<200*1024;++j)assert(static_cast<unsigned char*>(pinned[i])[j]==i+1);dihos_user_free(pinned[i]);}
    empty();
#ifdef EXPECT_LEGACY_POOL_FAILURE
    std::puts("PASS: legacy sixteen live pools reject the guest script allocation without a backing attempt");return 0;
#endif
    std::puts("PASS: guest 0xa46bbf script allocation succeeds beside sixteen live pools; existing data and cleanup preserved");
    auto anchor=static_cast<unsigned char*>(dihos_user_malloc(32));assert(anchor);anchor[0]=0x42;
    void* cached[3];for(auto& p:cached){p=dihos_user_malloc(12*1024*1024);assert(p);}
    auto live=static_cast<unsigned char*>(cached[2]);live[0]=0x67;live[12*1024*1024-1]=0x89;
    dihos_user_free(cached[0]);dihos_user_free(cached[1]);total_limit=total_backing;
    auto pressure=dihos_user_malloc(24*1024*1024);
#ifdef EXPECT_NO_PRESSURE_RECLAIM
    assert(!pressure);
#else
    assert(pressure && active==3 && total_backing<=total_limit);
    dihos_user_free(pressure);
#endif
    assert(anchor[0]==0x42 && live[0]==0x67 && live[12*1024*1024-1]==0x89);
    dihos_user_free(anchor);dihos_user_free(live);empty();total_limit=SIZE_MAX;
#ifdef EXPECT_NO_PRESSURE_RECLAIM
    std::puts("PASS: prior allocator retains idle pools and fails under the same fixed byte quota");return 0;
#endif
    std::puts("PASS: pressure reclaims two idle pools for a larger allocation under an unchanged byte quota; live data and cleanup preserved");
    unsigned invalid_acquires=acquires;
    assert(!dihos_user_malloc(0) && !dihos_user_malloc(SIZE_MAX));
    assert(!dihos_user_calloc(SIZE_MAX,2) && !dihos_user_aligned_alloc(256,257));
    assert(!dihos_user_memalign(3,128) && !dihos_user_memalign(131072,128) && acquires==invalid_acquires);
    fail_acquire=true;assert(!dihos_user_malloc(1) && !active);fail_acquire=false;empty();
    auto css=static_cast<unsigned char*>(dihos_user_malloc(12*1024*1024));assert(css);
    std::memset(css,0x67,12*1024*1024);assert(css[12*1024*1024-1]==0x67);
    dihos_user_free(css);empty();
    auto script=static_cast<unsigned char*>(dihos_user_memalign(65536,24*1024*1024));assert(script && !(reinterpret_cast<uintptr_t>(script)&65535));
    std::memset(script,0x37,24*1024*1024);fail_acquire=true;
    assert(!dihos_user_realloc(script,40*1024*1024));fail_acquire=false;
    assert(script[0]==0x37 && script[24*1024*1024-1]==0x37);
    auto expanded=static_cast<unsigned char*>(dihos_user_realloc(script,40*1024*1024));assert(expanded);
    for(size_t i=0;i<24*1024*1024;++i)assert(expanded[i]==0x37);
    dihos_user_free(expanded);empty();assert(!dihos_user_malloc(64*1024*1024));
    auto near_ceiling=dihos_user_malloc(64*1024*1024-65536);
    if(near_ceiling)dihos_user_free(near_ceiling);empty();
    std::puts("PASS: 24 MiB aligned allocation, 40 MiB realloc, failed-growth preservation, owned cleanup and bounded 64 MiB rejection");
    void *small[1024];
    for(unsigned i=0;i<1024;++i){small[i]=dihos_user_malloc(32);assert(small[i] && !(reinterpret_cast<uintptr_t>(small[i])&15));std::memset(small[i],i&255,32);}
    assert(active==1);
    for(unsigned i=0;i<1024;++i){for(unsigned j=0;j<32;++j)assert(static_cast<unsigned char*>(small[i])[j]==(i&255));dihos_user_free(small[i]);}
    unsigned warm_acquires=acquires;
    for(unsigned i=0;i<1024;++i){void *p=dihos_user_malloc(32);assert(p);dihos_user_free(p);}
    assert(acquires==warm_acquires);empty();
    for(unsigned align=1;align<=65536;align*=2){
        void *p=dihos_user_aligned_alloc(align,align);assert(p && !(reinterpret_cast<uintptr_t>(p)&(align-1)));dihos_user_free(p);
    }
    auto z=static_cast<unsigned char*>(dihos_user_calloc(100,7));assert(z);for(unsigned i=0;i<700;++i)assert(!z[i]);dihos_user_free(z);empty();
    auto preserved=static_cast<unsigned char*>(dihos_user_malloc(200*1024));assert(preserved);preserved[0]=42;preserved[200*1024-1]=91;
    fail_acquire=true;assert(!dihos_user_realloc(preserved,240*1024));fail_acquire=false;
    assert(preserved[0]==42 && preserved[200*1024-1]==91);dihos_user_free(preserved);empty();
    struct Allocation {unsigned char *pointer;size_t size;unsigned char pattern;} slots[512]={};
    uint32_t random=0x12345678;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned iteration=0;iteration<30000;++iteration){
        auto &slot=slots[next()%512];
        if(slot.pointer)for(size_t i=0;i<slot.size;++i)assert(slot.pointer[i]==slot.pattern);
        unsigned action=next()%3;size_t size=next()%8192+1;
        if(!slot.pointer){
            unsigned alignment=16u<<(next()%9);
            slot.pointer=static_cast<unsigned char*>(dihos_user_memalign(alignment,size));
            if(slot.pointer){assert(!(reinterpret_cast<uintptr_t>(slot.pointer)&(alignment-1)));slot.size=size;slot.pattern=next();std::memset(slot.pointer,slot.pattern,size);}
        }else if(action==0){dihos_user_free(slot.pointer);slot={};}
        else {
            auto replacement=static_cast<unsigned char*>(dihos_user_realloc(slot.pointer,size));
            if(replacement){for(size_t i=0;i<(size<slot.size?size:slot.size);++i)assert(replacement[i]==slot.pattern);slot.pointer=replacement;slot.size=size;std::memset(slot.pointer,slot.pattern,size);}
            else for(size_t i=0;i<slot.size;++i)assert(slot.pointer[i]==slot.pattern);
        }
        if(!(iteration%256)){assert(!dihos_user_allocator_check());dihos_user_allocator_trim();}
        assert(active<=32);
    }
    for(auto &slot:slots)if(slot.pointer)dihos_user_free(slot.pointer);empty();
    acquire_limit=1024*1024;rejected_large=0;
    void *large[1024];unsigned large_count=0;while(large_count<1024 && (large[large_count]=dihos_user_malloc(240*1024)))++large_count;assert(large_count>8 && large_count<1024);
    assert(active==32 && !dihos_user_malloc(240*1024));
    assert(rejected_large);acquire_limit=64*1024*1024;
    dihos_user_free(large[3]);large[3]=nullptr;
    auto recycled=dihos_user_malloc(24*1024*1024);assert(recycled && active==32);
    dihos_user_free(recycled);
    large[3]=dihos_user_malloc(240*1024);assert(large[3] && active==32);
    for(unsigned i=0;i<large_count;++i)dihos_user_free(large[i]);empty();
    // Exercise TLSF bin boundaries close to the pool ceiling; failure is valid,
    // but allocation attempts must remain bounded and fully trimmable.
    for(size_t size=240000;size<262144;size+=127){void *p=dihos_user_malloc(size);if(p)dihos_user_free(p);empty();}
    void *wide=dihos_user_aligned_alloc(65536,131072);assert(wide && !(reinterpret_cast<uintptr_t>(wide)%65536));dihos_user_free(wide);empty();
    std::thread workers[4];
    for(unsigned w=0;w<4;++w)workers[w]=std::thread([w]{
        for(unsigned i=0;i<10000;++i){size_t size=(i*37+w*13)%2048+1;
            auto p=static_cast<unsigned char*>(dihos_user_malloc(size));assert(p);std::memset(p,static_cast<int>(w+1),size);
            for(size_t n=0;n<size;++n)assert(p[n]==w+1);dihos_user_free(p);
        }});
    for(auto &worker:workers)worker.join();empty();
    std::puts("PASS: TLSF arena pooling, warm reuse, alignment, calloc overflow/zeroing, realloc failure preservation, 30000 randomized operations, exhaustion, trimming and pool boundaries; four concurrent workers/40000 allocations");
}
