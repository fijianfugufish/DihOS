#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "../sdk/sacx/apps/dihscover/dihscover.h"
static void *pool;static unsigned attempts;
static int allocate(uint32_t size,void **out){++attempts;if(size>32*1024*1024)return -1;*out=pool=std::malloc(size);return *out?0:-1;}
static int release(void *p){std::free(p);return 0;}
void *dihs_memset(void *p,int value,__SIZE_TYPE__ bytes){return std::memset(p,value,bytes);}
int main(){
 sacx_api api={};api.struct_size=sizeof(api);api.mem_alloc=allocate;api.mem_free=release;browser_heap_set_api(&api);
 assert(!browser_heap_init()&&attempts==3);assert(!browser_heap_used());
 void *p=browser_heap_alloc(100);assert(p&&browser_heap_used()>=100);memset(p,0x5a,100);
 auto used=browser_heap_used();assert(!browser_heap_realloc(p,64*1024*1024)&&browser_heap_used()==used);assert(((unsigned char*)p)[99]==0x5a);
 p=browser_heap_realloc(p,2048);assert(p&&browser_heap_used()>=2048&&((unsigned char*)p)[99]==0x5a);browser_heap_free(p);assert(!browser_heap_used());
 void *slots[40]={};unsigned count=0;while(count<40&&(slots[count]=browser_heap_alloc(1024*1024)))++count;assert(count>0&&count<40);
 assert(browser_heap_peak()>=browser_heap_used());while(count)browser_heap_free(slots[--count]);assert(!browser_heap_used());
 p=browser_heap_alloc(128);assert(p);assert(!browser_heap_realloc(p,0)&&!browser_heap_used());
 std::free(pool);puts("PASS: browser heap fallback, failed realloc retains ownership, exhausted-memory recovery and zero retained allocations");
}
