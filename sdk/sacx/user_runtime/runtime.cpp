// Freestanding bring-up runtime, not a replacement for libc++/ICU/pthreads.
#include "sacx_user_api.h"
#include <stddef.h>
#include <errno.h>
#include "dihos_user_allocator.h"
#include "dihos_user_sync.h"
extern "C" {
typedef void (*initializer)(void);
extern initializer __init_array_start[],__init_array_end[];
int dihos_user_main(const sacx_api *api);
void *__dso_handle=&__dso_handle;
static thread_local int native_errno;
int *__errno_location(){return &native_errno;}
static struct {void (*function)(void*);void *argument,*dso;} destructors[32];
static unsigned destructor_count;
static uint32_t destructor_mutex;
int __cxa_atexit(void (*function)(void*),void *argument,void *dso){
    dihos_user_lock lock(destructor_mutex);
    if(!function || destructor_count==32)return -1;
    destructors[destructor_count++]={function,argument,dso};return 0;
}
void __cxa_finalize(void *dso){
    for(;;){
        void (*function)(void*)=nullptr;void *argument=nullptr;
        {
            dihos_user_lock lock(destructor_mutex);
            for(unsigned i=destructor_count;i>0;--i){
                auto &entry=destructors[i-1];
                if(entry.function && (!dso || entry.dso==dso)){
                    function=entry.function;argument=entry.argument;entry.function=nullptr;break;
                }
            }
            while(destructor_count && !destructors[destructor_count-1].function)--destructor_count;
        }
        if(!function)return;
        function(argument); // Never invoke user code with the registry locked.
    }
}
__attribute__((noreturn)) void __cxa_pure_virtual(){sacx_user_exit(125,"C++ pure virtual call");}
__attribute__((noreturn)) void abort(){sacx_user_exit(191,"C runtime: abort");}
void *memcpy(void *out,const void *in,size_t bytes){
    auto d=static_cast<unsigned char*>(out);auto s=static_cast<const unsigned char*>(in);
    for(size_t i=0;i<bytes;++i)d[i]=s[i];return out;
}
void *memmove(void *out,const void *in,size_t bytes){
    auto d=static_cast<unsigned char*>(out);auto s=static_cast<const unsigned char*>(in);
    if(reinterpret_cast<uintptr_t>(d)<reinterpret_cast<uintptr_t>(s))for(size_t i=0;i<bytes;++i)d[i]=s[i];
    else for(size_t i=bytes;i>0;--i)d[i-1]=s[i-1];return out;
}
void *memset(void *out,int value,size_t bytes){
    auto d=static_cast<unsigned char*>(out);for(size_t i=0;i<bytes;++i)d[i]=static_cast<unsigned char>(value);return out;
}
int memcmp(const void *a,const void *b,size_t bytes){
    auto x=static_cast<const unsigned char*>(a);auto y=static_cast<const unsigned char*>(b);
    for(size_t i=0;i<bytes;++i)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;
}
void *memmem(const void *data,size_t bytes,const void *needle,size_t length){
    if(!length)return const_cast<void*>(data);
    if(length>bytes)return nullptr;
    auto input=static_cast<const unsigned char*>(data),pattern=static_cast<const unsigned char*>(needle);
    for(size_t i=0;i<=bytes-length;++i)
        if(input[i]==pattern[0] && !memcmp(input+i,pattern,length))return const_cast<unsigned char*>(input+i);
    return nullptr;
}
size_t strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
int strcmp(const char *a,const char *b){
    auto x=reinterpret_cast<const unsigned char*>(a),y=reinterpret_cast<const unsigned char*>(b);
    while(*x && *x==*y){++x;++y;}
    return static_cast<int>(*x)-static_cast<int>(*y);
}
void *malloc(size_t bytes){return dihos_user_malloc(bytes);}
void free(void *p){dihos_user_free(p);}
void *calloc(size_t count,size_t bytes){return dihos_user_calloc(count,bytes);}
void *realloc(void *p,size_t bytes){return dihos_user_realloc(p,bytes);}
void *aligned_alloc(size_t alignment,size_t bytes){return dihos_user_aligned_alloc(alignment,bytes);}
__attribute__((noreturn)) void _start(){
    for(auto at=__init_array_start;at!=__init_array_end;++at)(*at)();
    int result=dihos_user_main(sacx_userspace_api());
    if(dihos_user_thread_finalize)dihos_user_thread_finalize();
    __cxa_finalize(nullptr);dihos_user_allocator_trim();sacx_user_exit(result,nullptr);
}
}
void *operator new(size_t bytes){
    void *result=dihos_user_malloc(bytes?bytes:1);
    if(!result)
        sacx_user_exit(126,"C++ allocation failed");
    return result;
}
void *operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void *p) noexcept{dihos_user_free(p);}
void operator delete[](void *p) noexcept{::operator delete(p);}
void operator delete(void *p,size_t) noexcept{::operator delete(p);}
void operator delete[](void *p,size_t) noexcept{::operator delete(p);}
namespace std {enum class align_val_t:size_t {};}
void *operator new(size_t bytes,std::align_val_t alignment){
    void *result=dihos_user_memalign(static_cast<size_t>(alignment),bytes?bytes:1);
    if(!result)sacx_user_exit(126,"C++ aligned allocation failed");return result;
}
void *operator new[](size_t bytes,std::align_val_t a){return ::operator new(bytes,a);}
void operator delete(void *p,std::align_val_t) noexcept{dihos_user_free(p);}
void operator delete[](void *p,std::align_val_t) noexcept{dihos_user_free(p);}
void operator delete(void *p,size_t,std::align_val_t) noexcept{dihos_user_free(p);}
void operator delete[](void *p,size_t,std::align_val_t) noexcept{dihos_user_free(p);}
