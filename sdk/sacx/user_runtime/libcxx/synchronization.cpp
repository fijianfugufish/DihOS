// Selected libc++ ABI support over SACX. Upstream headers stay unmodified.
#include <mutex>
#include <__system_error/throw_system_error.h>
#include "sacx_user_api.h"
_LIBCPP_BEGIN_NAMESPACE_STD
void __throw_system_error(int,const char *what){sacx_user_exit(156,what);}
mutex::~mutex() noexcept {
    if(__libcpp_mutex_destroy(&__m_))sacx_user_exit(156,"C++ destroying locked mutex");
}
void mutex::lock(){int rc=__libcpp_mutex_lock(&__m_);if(rc)__throw_system_error(rc,"C++ mutex lock failed");}
bool mutex::try_lock() noexcept{return __libcpp_mutex_trylock(&__m_);}
void mutex::unlock() noexcept{if(__libcpp_mutex_unlock(&__m_))sacx_user_exit(156,"C++ mutex unlock ownership failed");}
recursive_mutex::recursive_mutex(){__libcpp_recursive_mutex_init(&__m_);}
recursive_mutex::~recursive_mutex(){if(__libcpp_recursive_mutex_destroy(&__m_))sacx_user_exit(156,"C++ destroying locked recursive mutex");}
void recursive_mutex::lock(){int rc=__libcpp_recursive_mutex_lock(&__m_);if(rc)__throw_system_error(rc,"C++ recursive mutex lock failed");}
bool recursive_mutex::try_lock() noexcept{return __libcpp_recursive_mutex_trylock(&__m_);}
void recursive_mutex::unlock() noexcept{if(__libcpp_recursive_mutex_unlock(&__m_))sacx_user_exit(156,"C++ recursive mutex unlock ownership failed");}
void __call_once(volatile once_flag::_State_type &state,void *argument,void (*function)(void*)){
    for(;;){
        auto value=__atomic_load_n(&state,__ATOMIC_ACQUIRE);
        if(value==once_flag::_Complete)return;
        if(value==once_flag::_Unset){
            auto expected=once_flag::_Unset;
            if(__atomic_compare_exchange_n(&state,&expected,once_flag::_Pending,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
                function(argument);
                __atomic_store_n(&state,once_flag::_Complete,__ATOMIC_RELEASE);
                if(sacx_user_wake32((uint32_t*)&state,4)<0)sacx_user_exit(156,"C++ once wake failed");return;
            }
        }else{
            int rc=sacx_user_wait32((uint32_t*)&state,1,60000);
            if(rc && rc!=-11 && rc!=-110)sacx_user_exit(156,"C++ once wait failed");
        }
    }
}
_LIBCPP_END_NAMESPACE_STD
