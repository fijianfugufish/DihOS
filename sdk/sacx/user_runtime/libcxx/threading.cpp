// Selected libc++ 22 ABI adaptation; upstream templates remain unmodified.
#include <thread>
#include <mutex>
#include <condition_variable>
#include <__chrono/steady_clock.h>
#include "sacx_user_keys.h"
#include "sacx_user_api.h"
extern "C" void *malloc(size_t);
extern "C" void free(void*);
namespace {
struct Start {void *(*function)(void*);void *argument;};
uint64_t start(uint64_t argument){
    auto *data=reinterpret_cast<Start*>(argument);
    auto function=data->function;void *value=data->argument;free(data);
    return reinterpret_cast<uintptr_t>(function(value));
}
uint64_t now(){
    int64_t result=sacx_sync_now();if(result<0)sacx_user_exit(159,"SACX C++ clock unavailable");return (uint64_t)result;
}
}
_LIBCPP_BEGIN_NAMESPACE_STD
int __libcpp_thread_create(__libcpp_thread_t *thread,void *(*function)(void*),void *argument){
    if(!thread || !function)return 22;
    auto data=static_cast<Start*>(malloc(sizeof(Start)));if(!data)return 12;
    *data={function,argument};int64_t result=sacx_user_thread_create(start,(uintptr_t)data);
    if(result<0){free(data);return (int)-result;}
    *thread=(uint64_t)result;return 0;
}
int __libcpp_thread_join(__libcpp_thread_t *thread){
    if(!thread || !*thread)return 22;
    int result;do {result=sacx_user_thread_join(*thread,nullptr,60000);}while(result==-110);
    return result<0?-result:0;
}
int __libcpp_thread_detach(__libcpp_thread_t*){return 95;}
int __libcpp_tls_create(__libcpp_tls_key *key,void (*destructor)(void*)){return sacx_user_key_create(key,destructor);}
void *__libcpp_tls_get(__libcpp_tls_key key){return sacx_user_key_get(key);}
int __libcpp_tls_set(__libcpp_tls_key key,void *pointer){return sacx_user_key_set(key,pointer);}
int __libcpp_execute_once(__libcpp_exec_once_flag *flag,void (*function)()){
    if(!flag || !function)return 22;
    __call_once(*flag,&function,[](void *argument){(*static_cast<void (**)()>(argument))();});return 0;
}
void __libcpp_thread_sleep_for(const chrono::nanoseconds &duration){
    auto ns=duration.count();if(ns<=0)return;
    uint64_t ms=(uint64_t)(ns/1000000)+(ns%1000000!=0),begin=now();
    uint64_t deadline=UINT64_MAX-begin<ms?UINT64_MAX:begin+ms;
    for(uint64_t current=begin;current<deadline;current=now()){
        uint64_t amount=deadline-current;if(amount>60000)amount=60000;
        if(sacx_user_call(SACX_USER_SLEEP_MS,amount,0,0))sacx_user_exit(159,"SACX C++ sleep failed");
    }
}
namespace chrono {
steady_clock::time_point steady_clock::now() noexcept {
    uint64_t ms=::now(),max=(uint64_t)INT64_MAX/1000000;
    return time_point(duration(ms>max?INT64_MAX:(int64_t)(ms*1000000)));
}
}
namespace this_thread {void sleep_for(const chrono::nanoseconds &duration){__libcpp_thread_sleep_for(duration);}}
thread::~thread(){if(joinable())terminate();}
void thread::join(){
    int error=__libcpp_thread_join(&__t_);
    if(error)__throw_system_error(error,"std::thread join failed");__t_=0;
}
void thread::detach(){__throw_system_error(95,"std::thread detach unsupported; join the worker");}
unsigned thread::hardware_concurrency() noexcept{return 1;}

// The private header ABI expects an owned exit-notification structure. Futures
// remain unimplemented (__make_ready_at_thread_exit has no success stub).
class __thread_struct_imp {
public:
    struct Notification {condition_variable *condition;mutex *lock;};
    Notification notifications[8];unsigned count;
};
__thread_struct::__thread_struct():__p_(new __thread_struct_imp{}){}
__thread_struct::~__thread_struct(){
    for(unsigned i=0;i<__p_->count;++i){auto entry=__p_->notifications[i];entry.lock->unlock();entry.condition->notify_all();}
    delete __p_;
}
void __thread_struct::notify_all_at_thread_exit(condition_variable *condition,mutex *lock){
    if(__p_->count==8)__throw_system_error(11,"Thread exit notification limit exceeded");
    __p_->notifications[__p_->count++]={condition,lock};
}
__thread_specific_ptr<__thread_struct> &__thread_local_data(){
    // Deliberately process-lifetime storage: no destruction while workers run.
    alignas(__thread_specific_ptr<__thread_struct>) static unsigned char storage[sizeof(__thread_specific_ptr<__thread_struct>)];
    static auto pointer=new(storage) __thread_specific_ptr<__thread_struct>;
    return *pointer;
}
condition_variable::~condition_variable(){if(__libcpp_condvar_destroy(&__cv_))sacx_user_exit(156,"C++ destroying busy condition variable");}
void condition_variable::notify_one() noexcept{if(__libcpp_condvar_signal(&__cv_))sacx_user_exit(156,"C++ condition signal failed");}
void condition_variable::notify_all() noexcept{if(__libcpp_condvar_broadcast(&__cv_))sacx_user_exit(156,"C++ condition broadcast failed");}
void condition_variable::wait(unique_lock<mutex> &lock) noexcept {
    if(!lock.owns_lock())__throw_system_error(1,"C++ condition wait without lock");
    int error=__libcpp_condvar_wait(&__cv_,lock.mutex()->native_handle());
    if(error)__throw_system_error(error,"C++ condition wait failed");
}
void notify_all_at_thread_exit(condition_variable &condition,unique_lock<mutex> lock){
    auto &data=__thread_local_data();if(!data.get())data.set_pointer(new __thread_struct);
    data->notify_all_at_thread_exit(&condition,lock.release());
}
_LIBCPP_END_NAMESPACE_STD
namespace std {
[[noreturn]] void terminate() noexcept{sacx_user_exit(158,"std::terminate: unjoined thread or invalid C++ lifecycle");}
}
