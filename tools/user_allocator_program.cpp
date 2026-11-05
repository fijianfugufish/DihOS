#include "sacx_user_api.h"
#include "dihos_user_allocator.h"
extern "C" void *malloc(size_t);
extern "C" void free(void*);
extern "C" void *calloc(size_t,size_t);
extern "C" void *realloc(void*,size_t);
extern "C" void *aligned_alloc(size_t,size_t);
static void *objects[1024];
struct alignas(256) Wide {volatile unsigned value;Wide():value(91){}};
__attribute__((noinline)) static void check_wide(Wide *p){
    if(reinterpret_cast<uintptr_t>(p)%256 || p->value!=91)sacx_user_exit(146,"Arena aligned new failed");
}
extern "C" int dihos_user_main(const sacx_api *api){
    for(unsigned i=0;i<1024;++i){
        objects[i]=malloc(32);
        if(!objects[i] || reinterpret_cast<uintptr_t>(objects[i])%16)sacx_user_exit(140,"Arena small allocations failed");
        static_cast<unsigned char*>(objects[i])[0]=static_cast<unsigned char>(i);
    }
    dihos_user_allocator_stats stats;dihos_user_allocator_get_stats(&stats);
    if(stats.pools!=1 || stats.allocations!=1024 || dihos_user_allocator_check())sacx_user_exit(141,"Arena pooling failed");
    for(unsigned i=0;i<1024;++i){
        if(static_cast<unsigned char*>(objects[i])[0]!=static_cast<unsigned char>(i))sacx_user_exit(142,"Arena allocation overlap");
        free(objects[i]);
    }
    auto p=static_cast<unsigned char*>(calloc(100,7));if(!p)sacx_user_exit(143,"Arena calloc failed");
    for(unsigned i=0;i<700;++i){if(p[i])sacx_user_exit(143,"Arena calloc not zeroed");p[i]=static_cast<unsigned char>(i);}
    auto grown=static_cast<unsigned char*>(realloc(p,4000));if(!grown)sacx_user_exit(144,"Arena realloc failed");
    for(unsigned i=0;i<700;++i)if(grown[i]!=static_cast<unsigned char>(i))sacx_user_exit(144,"Arena realloc lost data");
    if(realloc(grown,SIZE_MAX) || grown[699]!=static_cast<unsigned char>(699))sacx_user_exit(145,"Arena failed realloc changed source");
    free(grown);
    if(calloc(SIZE_MAX,2) || aligned_alloc(256,257))sacx_user_exit(145,"Arena overflow/alignment check failed");
    for(unsigned alignment=16;alignment<=65536;alignment*=4){
        void *p=aligned_alloc(alignment,alignment);
        if(!p || reinterpret_cast<uintptr_t>(p)%alignment)sacx_user_exit(146,"Arena aligned alloc failed");free(p);
    }
    Wide *wide=new Wide;check_wide(wide);delete wide;
    dihos_user_allocator_trim();dihos_user_allocator_get_stats(&stats);
    if(stats.reserved || stats.allocations || dihos_user_allocator_check())sacx_user_exit(147,"Arena trim failed");
    // Retain all large blocks until the pool limit is reached.
    unsigned count=0;
    while(count<1024 && (objects[count]=malloc(240*1024)))++count;
    if(count<=8 || count==1024 || malloc(240*1024))sacx_user_exit(148,"Arena pool exhaustion failed");
    for(unsigned i=0;i<count;++i)free(objects[i]);
    dihos_user_allocator_trim();dihos_user_allocator_get_stats(&stats);
    if(stats.reserved || stats.allocations || dihos_user_allocator_check())sacx_user_exit(149,"Arena exhaustion recovery failed");
    if(api->app_yield())sacx_user_exit(150,"Arena yield failed");
    api->log("SACX arena: 1024 objects, alignment, realloc and exhaustion OK");return 76;
}
