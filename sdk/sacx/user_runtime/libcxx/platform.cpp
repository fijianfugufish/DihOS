#include <__config>
#include <__verbose_abort>
#include "sacx_user_api.h"
#include "sacx_user_thread_context.h"
#include "dihos_user_abort_trace.h"
extern "C" __attribute__((noreturn)) void dihos_user_libcxx_abort(const char *reason) noexcept {
    char message[257];
    sacx_user_stack_bounds bounds{};
    (void)sacx_user_thread_stack(sacx_user_thread_self(),&bounds);
    dihos_user_abort_message(message,reason,
        reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
        reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),bounds.begin,bounds.end);
    sacx_user_exit(151,message);
}
_LIBCPP_BEGIN_NAMESPACE_STD
void __libcpp_verbose_abort(const char *reason,...) noexcept {dihos_user_libcxx_abort(reason);}
_LIBCPP_END_NAMESPACE_STD
