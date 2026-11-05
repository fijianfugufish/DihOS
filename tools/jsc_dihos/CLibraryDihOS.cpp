// Closed script-process profile: no inherited host environment or locale state.
// C exit callbacks share the native C++ registry and its tested finalization.
#include <stddef.h>
extern "C" {
int strcmp(const char*,const char*);
int __cxa_atexit(void (*)(void*),void*,void*);
static char *empty_environment[]={nullptr};
char **__environ=empty_environment;
extern char **environ __attribute__((alias("__environ")));
static void invoke_c_exit(void *callback){reinterpret_cast<void(*)()>(callback)();}
int atexit(void (*callback)()){
    if(!callback)return -1;
    return __cxa_atexit(invoke_c_exit,reinterpret_cast<void*>(callback),nullptr);
}
char *setlocale(int category,const char *requested){
    // LC_ALL is 0 in the native SACX C ABI. Other categories are not exposed.
    static char c_locale[]="C";
    if(category!=0)return nullptr;
    if(!requested || !*requested || !strcmp(requested,"C") || !strcmp(requested,"POSIX"))return c_locale;
    return nullptr;
}
}
