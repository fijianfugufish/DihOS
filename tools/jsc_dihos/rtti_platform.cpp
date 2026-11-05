// Native failure/string support for the selected upstream RTTI implementation.
#include "sacx_user_api.h"
extern "C" [[noreturn]] void __abort_message(const char *format,...){
    sacx_user_log("C++ ABI fatal invariant:");
    if(format)sacx_user_log(format);
    sacx_user_exit(164,nullptr);
}
