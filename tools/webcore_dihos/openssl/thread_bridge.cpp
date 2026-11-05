#include "pthread.h"
#include <sacx_user_sync.hpp>
#include <sacx_user_keys.h>
#include <stddef.h>
static_assert(offsetof(pthread_mutex_t,depth)==sizeof(sacx_user_mutex));
static_assert(sizeof(pthread_cond_t)==sizeof(sacx_user_condition));
static sacx_user_mutex* base(pthread_mutex_t* m){return reinterpret_cast<sacx_user_mutex*>(m);}
static sacx_user_recursive_mutex* recursive(pthread_mutex_t* m){return reinterpret_cast<sacx_user_recursive_mutex*>(m);}
static sacx_user_condition* condition(pthread_cond_t* c){return reinterpret_cast<sacx_user_condition*>(c);}
extern "C" {
int pthread_mutexattr_init(pthread_mutexattr_t* a){if(!a)return 22;a->recursive=0;return 0;}
int pthread_mutexattr_destroy(pthread_mutexattr_t* a){return a?0:22;}
int pthread_mutexattr_settype(pthread_mutexattr_t* a,int type){if(!a || type<0 || type>2)return 22;a->recursive=type==PTHREAD_MUTEX_RECURSIVE;return 0;}
int pthread_mutex_init(pthread_mutex_t* m,const pthread_mutexattr_t* a){if(!m)return 22;*m={};m->recursive=a?a->recursive:0;return 0;}
int pthread_mutex_destroy(pthread_mutex_t* m){if(!m)return 22;return __atomic_load_n(&m->word,__ATOMIC_ACQUIRE)?16:0;}
int pthread_mutex_lock(pthread_mutex_t* m){if(!m)return 22;return m->recursive?sacx_recursive_lock(recursive(m)):sacx_mutex_lock(base(m));}
int pthread_mutex_unlock(pthread_mutex_t* m){if(!m)return 22;return m->recursive?sacx_recursive_unlock(recursive(m)):sacx_mutex_unlock(base(m));}
int pthread_cond_init(pthread_cond_t* c,const void* attr){if(!c || attr)return 22;*c={};return 0;}
int pthread_cond_destroy(pthread_cond_t* c){if(!c)return 22;return __atomic_load_n(&c->waiters,__ATOMIC_ACQUIRE)?16:0;}
int pthread_cond_wait(pthread_cond_t* c,pthread_mutex_t* m){
    if(!c || !m || (m->recursive && m->depth!=1))return 22;
    if(m->recursive)m->depth=0;
    int rc=sacx_condition_wait(condition(c),base(m));
    if(m->recursive && __atomic_load_n(&m->owner,__ATOMIC_RELAXED)==sacx_sync_self())m->depth=1;
    return rc;
}
int pthread_cond_signal(pthread_cond_t* c){return c?sacx_condition_notify(condition(c),1):22;}
int pthread_cond_broadcast(pthread_cond_t* c){return c?sacx_condition_notify(condition(c),4):22;}
int pthread_once(pthread_once_t* once,void (*initialize)(void)){
    if(!once || !initialize)return 22;
    for(;;){
        uint32_t state=__atomic_load_n(&once->state,__ATOMIC_ACQUIRE);
        if(state==2)return 0;
        if(state==0 && __atomic_compare_exchange_n(&once->state,&state,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
            initialize();__atomic_store_n(&once->state,2,__ATOMIC_RELEASE);
            int rc=sacx_user_wake32(&once->state,4);return rc<0?-rc:0;
        }
        if(state>2)return 22;
        int rc=sacx_user_wait32(&once->state,1,60000);
        if(rc && rc!=-11 && rc!=-110)return -rc;
    }
}
pthread_t pthread_self(void){return sacx_user_thread_self();}
int pthread_equal(pthread_t a,pthread_t b){return a==b;}
int pthread_key_create(pthread_key_t* key,void (*cleanup)(void*)){return sacx_user_key_create(key,cleanup);}
int pthread_key_delete(pthread_key_t key){return sacx_user_key_delete(key);}
int pthread_setspecific(pthread_key_t key,const void* value){return sacx_user_key_set(key,const_cast<void*>(value));}
void* pthread_getspecific(pthread_key_t key){return sacx_user_key_get(key);}
}
