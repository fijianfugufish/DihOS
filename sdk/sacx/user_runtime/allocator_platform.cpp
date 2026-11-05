#include "dihos_user_allocator.h"
#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include "sacx_user_resources.h"
#include <stdarg.h>
extern "C" int dihos_user_allocator_printf(const char*,...);
extern "C" int dihos_user_region_acquire(uint32_t bytes,void **out){return sacx_user_mem_alloc(bytes,out);}
extern "C" int dihos_user_region_release(void *p){return sacx_user_mem_free(p);}
extern "C" void dihos_user_allocator_failure(size_t bytes,uint64_t reserved,uint64_t live,unsigned pools,unsigned reason){
    sacx_user_resources r{};
    int rc=sacx_user_resources_get(&r);
    dihos_user_allocator_printf("Allocator: bytes=%p reserved=%p live=%p pools=%u reason=%u private=%p committed=%p limit=%p query=%u\n",
        (void*)bytes,(void*)(uintptr_t)reserved,(void*)(uintptr_t)live,pools,reason,
        (void*)(uintptr_t)r.private_bytes,(void*)(uintptr_t)r.heap_committed_bytes,(void*)(uintptr_t)r.heap_limit_bytes,(unsigned)rc);
}
extern "C" int dihos_user_allocator_wait(uint32_t *word,uint32_t expected){return sacx_user_wait32(word,expected,60000);}
extern "C" int dihos_user_allocator_wake(uint32_t *word){return sacx_user_wake32(word,1);}
extern "C" __attribute__((noreturn)) void dihos_user_allocator_panic(const char *reason){sacx_user_exit(128,reason);}
// TLSF's diagnostic formats use only these conversions. This is a private
// adapter, not a declaration of a complete libc stdio implementation.
extern "C" int dihos_user_allocator_printf(const char *format,...){
    char output[257];unsigned used=0,total=0;
    auto emit=[&](char c){output[used++]=c;++total;if(used==256){output[used]=0;sacx_user_log(output);used=0;}};
    va_list args;va_start(args,format);
    while(*format){
        if(*format!='%'){emit(*format++);continue;}
        char conversion=*++format;if(!conversion)dihos_user_allocator_panic("Invalid TLSF diagnostic format");++format;
        if(conversion=='%'){emit('%');continue;}
        if(conversion=='s'){const char *s=va_arg(args,const char*);if(!s)s="(null)";while(*s)emit(*s++);continue;}
        uint64_t value;unsigned base=10;
        if(conversion=='p'){value=reinterpret_cast<uintptr_t>(va_arg(args,void*));base=16;emit('0');emit('x');}
        else if(conversion=='x'){value=va_arg(args,unsigned);base=16;}
        else if(conversion=='u')value=va_arg(args,unsigned);
        else dihos_user_allocator_panic("Unsupported TLSF diagnostic format");
        char digits[32];unsigned n=0;do{digits[n++]="0123456789abcdef"[value%base];value/=base;}while(value);
        while(n)emit(digits[--n]);
    }
    va_end(args);if(used){output[used]=0;sacx_user_log(output);}return static_cast<int>(total);
}
