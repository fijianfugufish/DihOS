#pragma once
#include "sacx_user_threads.h"
#define SACX_USER_CONTEXT_VERSION 1u
#define SACX_USER_THREAD_STACK_BYTES (1024u*1024u)
enum { SACX_USER_THREAD_READY=1,SACX_USER_THREAD_SLEEPING=2,
       SACX_USER_THREAD_JOINING=3,SACX_USER_THREAD_WAITING=4 };
// Read-only copies of this process's saved EL0 state, never a restore interface.
typedef struct sacx_user_thread_context {
    uint32_t version,bytes,state,reserved;
    uint64_t thread,x[31],sp,pc,pstate;
    uint64_t q[64],fpcr,fpsr,tpidr,stack_begin,stack_end;
} sacx_user_thread_context;
typedef struct sacx_user_stack_bounds { uint64_t begin,end; } sacx_user_stack_bounds;
// One inspector per process. Pause is non-recursive; self-pause is rejected.
// The target remains stopped until this caller resumes it or exits. A failed
// copy does not acquire a stop. Other mutators must also be paused before GC.
static inline int sacx_user_thread_pause(sacx_user_thread target,sacx_user_thread_context *out){
    return (int)sacx_user_call(SACX_USER_THREAD_PAUSE,target,(uintptr_t)out,sizeof(*out));
}
static inline int sacx_user_thread_resume(sacx_user_thread target){
    return (int)sacx_user_call(SACX_USER_THREAD_RESUME,target,0,0);
}
static inline int sacx_user_thread_stack(sacx_user_thread target,sacx_user_stack_bounds *out){
    return (int)sacx_user_call(SACX_USER_THREAD_STACK,target,(uintptr_t)out,sizeof(*out));
}
