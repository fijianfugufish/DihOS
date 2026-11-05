#pragma once
#include "memory/aarch64_user_vm.h"
#define DIHOS_USER_MEMORY_REGIONS 32u
#define DIHOS_USER_SPARSE_REGIONS 16u
#define DIHOS_USER_MEMORY_REGION_MAX (64u*1024u*1024u)
#define DIHOS_USER_MEMORY_STEP_MAX (64u*1024u)
#define DIHOS_USER_SPARSE_REGION_MAX (4ull*1024*1024*1024)
#define DIHOS_USER_SPARSE_ARENA_BYTES (16ull*1024*1024*1024)
struct dihos_user_sparse_chunk;
typedef struct dihos_user_sparse_region {
    uint64_t address,bytes,span_address,span_bytes;
    struct dihos_user_sparse_chunk *chunks;
    uint32_t releasing;
} dihos_user_sparse_region;
typedef struct dihos_user_region {
    void *backing;
    uint64_t address,physical,bytes,committed[DIHOS_USER_MEMORY_REGION_MAX/4096/64];
} dihos_user_region;
typedef struct dihos_user_memory {
    dihos_user_region regions[DIHOS_USER_MEMORY_REGIONS];
    dihos_user_sparse_region sparse[DIHOS_USER_SPARSE_REGIONS];
    uint64_t sparse_arena;
    uint64_t limit,charged,reserved,committed,peak;
} dihos_user_memory;
/* Legacy reservations eagerly own backing. Sparse reservations acquire data
 * and metadata pages only on commit. Both require a paused task/kernel root. */
int64_t dihos_user_memory_call(dihos_user_memory *memory,aarch64_user_vm *vm,
                               uint64_t operation,const uint64_t arguments[3]);
/* Only after discarding the inactive VM. Also frees uncommitted reservations. */
void dihos_user_memory_destroy(dihos_user_memory *memory);
