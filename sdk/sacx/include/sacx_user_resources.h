#pragma once
#include "sacx_user_syscall.h"
#define SACX_USER_RESOURCES_VERSION 1u
#define SACX_USER_RESOURCES_CPU 1u
#define SACX_USER_RESOURCES_MEMORY 2u
/* Calling process/thread only. CPU accounting measures active guest slices
 * (including entry/return overhead) and synchronous SACX service work. Sleep,
 * parked waits and time between scheduler polls are excluded. Hypervisor steal
 * time within an active slice cannot currently be distinguished.
 * Private bytes include eagerly owned heap/guards, image, live stacks/TLS and
 * private translation tables; committed heap bytes (not a guarantee of access permissions) are a separate quantity. */
typedef struct sacx_user_resources {
    uint32_t version,bytes;
    uint64_t flags;
    uint64_t thread_user_ns,thread_kernel_ns,process_user_ns,process_kernel_ns;
    uint64_t private_bytes,heap_reserved_bytes,heap_committed_bytes,heap_limit_bytes;
} sacx_user_resources;
#if defined(__aarch64__)
static inline int sacx_user_resources_get(sacx_user_resources *out){
    return (int)sacx_user_call(SACX_USER_RESOURCES,(uintptr_t)out,sizeof(*out),0);
}
#endif
