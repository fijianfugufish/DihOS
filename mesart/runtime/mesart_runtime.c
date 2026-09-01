#include "mesart_runtime.h"
#include "mesart_protocol.h"
#include "mesart_freedreno_pm4.h"

/* Keep the runtime self-contained.  These are intentionally small bytewise
 * definitions, sufficient for compiler-generated struct initialization and
 * later Mesa bootstrap code; no host C library enters the verified bundle. */
void *memcpy(void *destination, const void *source, uint64_t bytes)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;

    while (bytes--)
        *out++ = *in++;
    return destination;
}

void *memset(void *destination, int value, uint64_t bytes)
{
    uint8_t *out = (uint8_t *)destination;

    while (bytes--)
        *out++ = (uint8_t)value;
    return destination;
}

static int mesart_runtime_query(uint64_t *out_heap, uint64_t *out_bytes,
                                uint64_t *out_abi)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_QUERY_RUNTIME;
    register uint64_t x1 __asm__("x1") = 0u;
    register uint64_t x2 __asm__("x2") = 0u;
    register uint64_t x3 __asm__("x3") = 0u;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                         : "r"(x8)
                         : "memory");
    if (x0 != 0u || !x1 || !x2 || x3 != MESART_RUNTIME_ABI_VERSION)
        return -1;
    *out_heap = x1;
    *out_bytes = x2;
    *out_abi = x3;
    return 0;
#else
    (void)out_heap;
    (void)out_bytes;
    (void)out_abi;
    return -1;
#endif
}

static int mesart_runtime_query_gpu_arena(uint64_t *out_handle,
                                          uint64_t *out_cpu,
                                          uint64_t *out_bytes,
                                          uint64_t *out_gpu)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_QUERY_GPU_ARENA;
    register uint64_t x1 __asm__("x1") = 0u;
    register uint64_t x2 __asm__("x2") = 0u;
    register uint64_t x3 __asm__("x3") = 0u;
    register uint64_t x4 __asm__("x4") = 0u;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
                           "+r"(x4)
                         : "r"(x8)
                         : "memory");
    if (x0 != 0u || !x1 || !x2 || !x3 || !x4)
        return -1;
    *out_handle = x1;
    *out_cpu = x2;
    *out_bytes = x3;
    *out_gpu = x4;
    return 0;
#else
    (void)out_handle;
    (void)out_cpu;
    (void)out_bytes;
    (void)out_gpu;
    return -1;
#endif
}

static int mesart_runtime_query_command_buffer(uint64_t *out_handle,
                                                uint64_t *out_cpu,
                                                uint64_t *out_dwords,
                                                uint64_t *out_gpu)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_QUERY_COMMAND_BUFFER;
    register uint64_t x1 __asm__("x1") = 0u;
    register uint64_t x2 __asm__("x2") = 0u;
    register uint64_t x3 __asm__("x3") = 0u;
    register uint64_t x4 __asm__("x4") = 0u;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
                           "+r"(x4)
                         : "r"(x8)
                         : "memory");
    if (x0 != 0u || !x1 || !x2 || x3 < 4u || !x4)
        return -1;
    *out_handle = x1;
    *out_cpu = x2;
    *out_dwords = x3;
    *out_gpu = x4;
    return 0;
#else
    (void)out_handle;
    (void)out_cpu;
    (void)out_dwords;
    (void)out_gpu;
    return -1;
#endif
}

static int mesart_runtime_capture_and_queue(uint64_t handle, uint64_t dwords,
                                            uint64_t *out_fence)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = MESART_OPERATION_CAPTURE_COMMAND_BUFFER;
    register uint64_t x1 __asm__("x1") = handle;
    register uint64_t x2 __asm__("x2") = dwords;
    register uint64_t x8 __asm__("x8") = MESART_EL0_SYSCALL;

    __asm__ __volatile__("svc #0" : "+r"(x0), "+r"(x1), "+r"(x2)
                         : "r"(x8) : "memory");
    if (out_fence)
        *out_fence = 0u;
    if (x0 != 0u)
        return -1;
    x0 = MESART_OPERATION_QUEUE_CAPTURED_COMMAND;
    x1 = handle;
    x2 = 0u;
    __asm__ __volatile__("svc #0" : "+r"(x0), "+r"(x1), "+r"(x2)
                         : "r"(x8) : "memory");
    if (x0 != 0u || !x1)
        return -2;
    if (out_fence)
        *out_fence = x1;
    return 0;
#else
    (void)handle;
    (void)dwords;
    (void)out_fence;
    return -1;
#endif
}

int mesart_runtime_command_init(mesart_runtime_command_buffer *command)
{
    uint64_t handle = 0u;
    uint64_t cpu = 0u;
    uint64_t dwords = 0u;
    uint64_t gpu = 0u;

    if (!command ||
        mesart_runtime_query_command_buffer(&handle, &cpu, &dwords, &gpu) != 0)
        return -1;
    *command = (mesart_runtime_command_buffer){
        (uint32_t *)(uintptr_t)cpu, handle, dwords, gpu};
    return 0;
}

int mesart_runtime_command_submit(const mesart_runtime_command_buffer *command,
                                  uint64_t dwords, uint64_t *out_fence)
{
    if (out_fence)
        *out_fence = 0u;
    if (!command || !command->cpu || !command->handle || !command->gpu_va ||
        !dwords || dwords > command->capacity_dwords || dwords > 0xffffffffull)
        return -1;
    __asm__ __volatile__("dmb ishst" ::: "memory");
    return mesart_runtime_capture_and_queue(command->handle, dwords, out_fence);
}

int mesart_runtime_init(mesart_runtime *runtime)
{
    uint64_t heap = 0u;
    uint64_t bytes = 0u;
    uint64_t abi = 0u;

    if (!runtime || mesart_runtime_query(&heap, &bytes, &abi) != 0 ||
        abi != MESART_RUNTIME_ABI_VERSION)
        return -1;
    *runtime = (mesart_runtime){(uint8_t *)(uintptr_t)heap, bytes, 0u};
    return 0;
}

void *mesart_runtime_alloc(mesart_runtime *runtime, uint64_t bytes)
{
    uint64_t aligned;
    uint64_t end;

    if (!runtime || !runtime->heap || !bytes)
        return 0;
    aligned = (bytes + 15u) & ~15ull;
    if (aligned < bytes || runtime->next_offset > runtime->heap_bytes ||
        aligned > runtime->heap_bytes - runtime->next_offset)
        return 0;
    end = runtime->next_offset;
    runtime->next_offset += aligned;
    return runtime->heap + end;
}

int mesart_runtime_gpu_arena_init(mesart_runtime_gpu_arena *arena)
{
    uint64_t handle = 0u;
    uint64_t cpu = 0u;
    uint64_t bytes = 0u;
    uint64_t gpu = 0u;

    if (!arena || mesart_runtime_query_gpu_arena(&handle, &cpu, &bytes,
                                                  &gpu) != 0 ||
        handle > 0xffffffffull)
        return -1;
    *arena = (mesart_runtime_gpu_arena){(uint8_t *)(uintptr_t)cpu, bytes,
                                        gpu, 0u, (uint32_t)handle};
    return 0;
}

void *mesart_runtime_gpu_alloc(mesart_runtime_gpu_arena *arena,
                               uint64_t bytes, uint64_t alignment,
                               uint64_t *out_gpu_va)
{
    uint64_t offset;
    uint64_t end;

    if (out_gpu_va)
        *out_gpu_va = 0u;
    if (!arena || !arena->cpu || !arena->gpu_va || !bytes || !alignment ||
        (alignment & (alignment - 1u)))
        return 0;
    offset = (arena->next_offset + alignment - 1u) & ~(alignment - 1u);
    if (offset < arena->next_offset || offset > arena->bytes ||
        bytes > arena->bytes - offset)
        return 0;
    end = offset + bytes;
    if (end < offset)
        return 0;
    arena->next_offset = end;
    if (out_gpu_va)
        *out_gpu_va = arena->gpu_va + offset;
    return arena->cpu + offset;
}

int mesart_runtime_selftest(void)
{
    mesart_runtime runtime = {0};
    mesart_runtime_gpu_arena arena = {0};
    uint32_t *first;
    uint32_t *second;
    uint32_t *gpu_word;
    uint64_t gpu_word_va = 0u;

    if (mesart_runtime_init(&runtime) != 0 || runtime.heap_bytes < 64u)
        return -1;
    first = (uint32_t *)mesart_runtime_alloc(&runtime, 16u);
    second = (uint32_t *)mesart_runtime_alloc(&runtime, 32u);
    if (!first || !second || (uint8_t *)second - (uint8_t *)first != 16u)
        return -2;
    first[0] = 0x4d455341u; /* "MESA" */
    second[0] = 0x52554e54u; /* "RUNT" */
    if (first[0] != 0x4d455341u || second[0] != 0x52554e54u ||
        mesart_runtime_gpu_arena_init(&arena) != 0)
        return -3;
    gpu_word = (uint32_t *)mesart_runtime_gpu_alloc(&arena, sizeof(*gpu_word),
                                                     64u, &gpu_word_va);
    if (!gpu_word || !gpu_word_va || (gpu_word_va & 63u))
        return -4;
    *gpu_word = 0x4152454eu; /* "ARENA" */
    return *gpu_word == 0x4152454eu ? 0 : -5;
}
