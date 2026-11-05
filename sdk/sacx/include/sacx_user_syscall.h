#pragma once
#include <stdint.h>
#define SACX_USER_ABI_VERSION 1u
#define SACX_USER_SYSCALL 0x53414358u
#define SACX_USER_BUFFER_MAX 256u
#define SACX_USER_CAP_CLOCK (1ull<<2)
#define SACX_USER_CAP_LOG (1ull<<5)
#define SACX_USER_CAP_MEMORY (1ull<<6)
#define SACX_USER_CAP_THREADS (1ull<<7)
#define SACX_USER_CAP_RANDOM (1ull<<8)
#define SACX_USER_MEMORY_READ 1u
#define SACX_USER_MEMORY_WRITE 2u
enum { SACX_USER_QUERY=0, SACX_USER_YIELD=1, SACX_USER_SLEEP_MS=2,
       SACX_USER_EXIT=3, SACX_USER_MONOTONIC_MS=4, SACX_USER_LOG=5,
       SACX_USER_MEMORY_RESERVE=6,SACX_USER_MEMORY_COMMIT=7,
       SACX_USER_MEMORY_PROTECT=8,SACX_USER_MEMORY_RELEASE=9,
       SACX_USER_THREAD_CREATE=10,SACX_USER_THREAD_SELF=11,SACX_USER_THREAD_EXIT=12,
       SACX_USER_THREAD_JOIN=13,SACX_USER_WAIT32=14,SACX_USER_WAKE32=15,
       SACX_USER_MEMORY_DECOMMIT=16,SACX_USER_RANDOM=17,
       SACX_USER_THREAD_PAUSE=18,SACX_USER_THREAD_RESUME=19,SACX_USER_THREAD_STACK=20,
       SACX_USER_THREAD_DETACH=21,SACX_USER_UTC_MS=22,SACX_USER_PROCESS_SELF=23,
       SACX_USER_RESOURCES=24,SACX_USER_SPARSE_RESERVE=25,SACX_USER_SPARSE_RELEASE=26 };
typedef struct sacx_user_info {
    uint32_t version,bytes;
    uint64_t operations,capabilities;
    uint32_t max_buffer,reserved;
} sacx_user_info;
/* Version in x7, envelope in x8, operation in x0, arguments in x1..x3.
 * Returns a value or a negative error in x0. Other registers are preserved. */
#if defined(__aarch64__)
static inline int64_t sacx_user_call(uint64_t operation,uint64_t a,uint64_t b,uint64_t c){
    register uint64_t x0 __asm__("x0")=operation,x1 __asm__("x1")=a,x2 __asm__("x2")=b,x3 __asm__("x3")=c;
    register uint64_t x7 __asm__("x7")=SACX_USER_ABI_VERSION,x8 __asm__("x8")=SACX_USER_SYSCALL;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),"r"(x7),"r"(x8):"memory");return (int64_t)x0;
}
#endif
