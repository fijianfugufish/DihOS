#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dihos_user_allocator_stats {
    uint64_t reserved,live_bytes,peak_bytes,allocations,pools;
} dihos_user_allocator_stats;
void *dihos_user_malloc(size_t bytes);
void *dihos_user_calloc(size_t count,size_t bytes);
void *dihos_user_realloc(void *pointer,size_t bytes);
void *dihos_user_memalign(size_t alignment,size_t bytes);
void *dihos_user_aligned_alloc(size_t alignment,size_t bytes);
void dihos_user_free(void *pointer);
/* Release idle secondary pools, or every pool when no allocations remain. */
int dihos_user_allocator_trim(void);
int dihos_user_allocator_check(void);
void dihos_user_allocator_get_stats(dihos_user_allocator_stats *out);
/* Platform adapter: user code supplies these through SACX, host tests mock it. */
int dihos_user_region_acquire(uint32_t bytes,void **out);
int dihos_user_region_release(void *pointer);
/* Allocation-free diagnostics for a failed large allocation. */
void dihos_user_allocator_failure(size_t,uint64_t,uint64_t,unsigned,unsigned);
int dihos_user_allocator_wait(uint32_t *word,uint32_t expected);
int dihos_user_allocator_wake(uint32_t *word);
__attribute__((noreturn)) void dihos_user_allocator_panic(const char *reason);
#ifdef __cplusplus
}
#endif
