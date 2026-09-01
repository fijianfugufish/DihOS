#pragma once

#include <stdint.h>

/* Small freestanding foundation for the future verified Mesa service.  It is
 * intentionally not a POSIX or SACX ABI: the kernel admits one fixed heap,
 * and this allocator only carves that finite range. */
typedef struct mesart_runtime
{
    uint8_t *heap;
    uint64_t heap_bytes;
    uint64_t next_offset;
} mesart_runtime;

typedef struct mesart_runtime_gpu_arena
{
    uint8_t *cpu;
    uint64_t bytes;
    uint64_t gpu_va;
    uint64_t next_offset;
    uint32_t handle;
} mesart_runtime_gpu_arena;

/* The only primary command source admitted by the first runtime.  Its CPU
 * mapping is writable in EL0; capture copies it into a kernel-only sealed
 * ring before the scheduler ever sees it. */
typedef struct mesart_runtime_command_buffer
{
    uint32_t *cpu;
    uint64_t handle;
    uint64_t capacity_dwords;
    uint64_t gpu_va;
} mesart_runtime_command_buffer;

/* Obtains the heap descriptor through Mesart's EL0 broker. */
int mesart_runtime_init(mesart_runtime *runtime);

/* 16-byte-aligned monotonic allocation.  Individual frees are deliberately
 * absent; the kernel destroys the complete heap with the renderer service. */
void *mesart_runtime_alloc(mesart_runtime *runtime, uint64_t bytes);

/* Opens the single brokered GPU resource arena.  `gpu_va` is constrained to
 * the renderer's SMMU aperture; it is never a physical address. */
int mesart_runtime_gpu_arena_init(mesart_runtime_gpu_arena *arena);
void *mesart_runtime_gpu_alloc(mesart_runtime_gpu_arena *arena,
                               uint64_t bytes, uint64_t alignment,
                               uint64_t *out_gpu_va);

/* Opens and submits the fixed primary command source through the Mesart
 * capture/queue broker.  This is the future Freedreno ringbuffer flush
 * boundary; callers never submit a GPU virtual address directly. */
int mesart_runtime_command_init(mesart_runtime_command_buffer *command);
int mesart_runtime_command_submit(const mesart_runtime_command_buffer *command,
                                  uint64_t dwords, uint64_t *out_fence);

/* Link-time/runtime smoke test used only by the signed renderer checkpoint. */
int mesart_runtime_selftest(void);
