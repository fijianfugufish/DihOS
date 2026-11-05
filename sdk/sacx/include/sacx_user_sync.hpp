#pragma once
#ifdef DIHOS_USER_SYNC_TEST
#include <stdint.h>
uint64_t sacx_sync_self();
int sacx_sync_wait(uint32_t*,uint32_t,uint32_t);
int sacx_sync_wake(uint32_t*,uint32_t);
int64_t sacx_sync_now();
#else
#include "sacx_user_threads.h"
// Library bookkeeping only: use the loader-installed TCB copy, avoiding a
// syscall/scheduler boundary inside every uncontended lock and try_lock.
// This user-writable copy grants no authority. Kernel services still identify
// the calling thread from their private context, never from this value.
static inline uint64_t sacx_sync_self(){
    return static_cast<const uint64_t*>(__builtin_thread_pointer())[1];
}
static inline int sacx_sync_wait(uint32_t *p,uint32_t value,uint32_t ms){return sacx_user_wait32(p,value,ms);}
static inline int sacx_sync_wake(uint32_t *p,uint32_t count){return sacx_user_wake32(p,count);}
static inline int64_t sacx_sync_now(){return sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);}
#endif
// Userspace synchronization; no kernel pointers or platform pthread calls.
struct sacx_user_mutex {
    uint32_t word;
    uint64_t owner;
};
struct sacx_user_recursive_mutex {sacx_user_mutex base;uint32_t depth;};
struct sacx_user_condition {uint32_t sequence,waiters;};
static inline int sacx_mutex_trylock(sacx_user_mutex *m){
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&m->word,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 16;
    __atomic_store_n(&m->owner,sacx_sync_self(),__ATOMIC_RELAXED);return 0;
}
static inline int sacx_mutex_lock(sacx_user_mutex *m){
    if(!sacx_mutex_trylock(m))return 0;
    uint64_t self=sacx_sync_self();
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)==self)return 35;
    while(__atomic_exchange_n(&m->word,2,__ATOMIC_ACQUIRE)){
        int rc=sacx_sync_wait(&m->word,2,60000);
        if(rc && rc!=-11 && rc!=-110)return -rc;
    }
    __atomic_store_n(&m->owner,self,__ATOMIC_RELAXED);return 0;
}
static inline int sacx_mutex_unlock(sacx_user_mutex *m){
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=sacx_sync_self())return 1;
    __atomic_store_n(&m->owner,0,__ATOMIC_RELAXED);
    if(__atomic_exchange_n(&m->word,0,__ATOMIC_RELEASE)==2){
        int rc=sacx_sync_wake(&m->word,1);if(rc<0)return -rc;
    }
    return 0;
}
static inline int sacx_recursive_trylock(sacx_user_recursive_mutex *m){
    if(__atomic_load_n(&m->base.owner,__ATOMIC_RELAXED)==sacx_sync_self()){
        if(m->depth==UINT32_MAX)return 11;
        ++m->depth;return 0;
    }
    int rc=sacx_mutex_trylock(&m->base);if(!rc)m->depth=1;return rc;
}
static inline int sacx_recursive_lock(sacx_user_recursive_mutex *m){
    int rc=sacx_recursive_trylock(m);if(rc!=16)return rc;
    rc=sacx_mutex_lock(&m->base);if(!rc)m->depth=1;return rc;
}
static inline int sacx_recursive_unlock(sacx_user_recursive_mutex *m){
    if(__atomic_load_n(&m->base.owner,__ATOMIC_RELAXED)!=sacx_sync_self() || !m->depth)return 1;
    if(--m->depth)return 0;return sacx_mutex_unlock(&m->base);
}
static inline int sacx_condition_notify(sacx_user_condition *condition,uint32_t count){
    if(!count || count>4)return 22;
    __atomic_fetch_add(&condition->sequence,1,__ATOMIC_RELEASE);
    int rc=sacx_sync_wake(&condition->sequence,count);return rc<0?-rc:0;
}
// deadline_ms is absolute monotonic milliseconds, or UINT64_MAX for no timeout.
// Returns with the mutex held, including timeout and syscall errors. Always
// recheck the caller's predicate: condition variables may wake spuriously.
static inline int sacx_condition_wait(sacx_user_condition *condition,sacx_user_mutex *m,uint64_t deadline_ms=UINT64_MAX){
    uint32_t expected=__atomic_load_n(&condition->sequence,__ATOMIC_ACQUIRE);
    __atomic_fetch_add(&condition->waiters,1,__ATOMIC_RELAXED);
    int rc=sacx_mutex_unlock(m);
    if(rc){__atomic_fetch_sub(&condition->waiters,1,__ATOMIC_RELAXED);return rc;}
    for(;;){
        uint64_t now=0;uint32_t slice=60000;
        if(deadline_ms!=UINT64_MAX){
            int64_t clock=sacx_sync_now();
            if(clock<0){rc=(int)-clock;break;}now=(uint64_t)clock;
            if(now>=deadline_ms){rc=110;break;}
            if(deadline_ms-now<slice)slice=(uint32_t)(deadline_ms-now);
        }
        rc=sacx_sync_wait(&condition->sequence,expected,slice);
        if(!rc || rc==-11){rc=0;break;}
        if(rc!=-110){rc=-rc;break;}
        // A 60-second kernel deadline is a poll, not a fabricated notification.
        if(__atomic_load_n(&condition->sequence,__ATOMIC_ACQUIRE)!=expected){rc=0;break;}
    }
    int lock_rc=sacx_mutex_lock(m);
    __atomic_fetch_sub(&condition->waiters,1,__ATOMIC_RELAXED);return lock_rc?lock_rc:rc;
}
