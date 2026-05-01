#include "asm/asm.h"
#include <stdint.h>

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)

#define ASM_DCACHE_LINE_SIZE 64ull

static uint32_t asm_cache_line_bytes_from_ctr_field(uint32_t field)
{
    uint32_t bytes = 4u << (field & 0xFu);
    if (bytes < 16u || bytes > 4096u)
        bytes = 64u;
    return bytes;
}

void asm_wait(void)
{
    __asm__ __volatile__("wfe" ::: "memory");
}

void asm_relax(void)
{
    __asm__ __volatile__("nop" ::: "memory");
}

void asm_compiler_barrier(void)
{
    __asm__ __volatile__("" ::: "memory");
}

void asm_mmio_barrier(void)
{
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

void asm_dma_clean_range(const void *ptr, uint64_t len)
{
    uintptr_t start;
    uintptr_t end;

    if (!ptr || len == 0u)
        return;

    start = (uintptr_t)ptr & ~(uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull);
    end = ((uintptr_t)ptr + (uintptr_t)len + (uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull)) &
          ~(uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull);

    for (uintptr_t p = start; p < end; p += ASM_DCACHE_LINE_SIZE)
        __asm__ __volatile__("dc cvac, %0" ::"r"(p) : "memory");

    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

void asm_dma_invalidate_range(const void *ptr, uint64_t len)
{
    uintptr_t start;
    uintptr_t end;

    if (!ptr || len == 0u)
        return;

    start = (uintptr_t)ptr & ~(uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull);
    end = ((uintptr_t)ptr + (uintptr_t)len + (uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull)) &
          ~(uintptr_t)(ASM_DCACHE_LINE_SIZE - 1ull);

    __asm__ __volatile__("dsb ish" ::: "memory");

    for (uintptr_t p = start; p < end; p += ASM_DCACHE_LINE_SIZE)
        __asm__ __volatile__("dc ivac, %0" ::"r"(p) : "memory");

    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

void asm_sync_executable_range(const void *ptr, uint64_t len)
{
    uint64_t ctr = 0u;
    uint32_t ic_line = 64u;
    uint32_t dc_line = 64u;
    uintptr_t start;
    uintptr_t end;
    uintptr_t p;

    if (!ptr || len == 0u)
        return;

    start = (uintptr_t)ptr;
    end = start + (uintptr_t)len;
    if (end < start)
        return;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
    ic_line = asm_cache_line_bytes_from_ctr_field((uint32_t)(ctr & 0xFu));
    dc_line = asm_cache_line_bytes_from_ctr_field((uint32_t)((ctr >> 16) & 0xFu));

    p = start & ~(uintptr_t)(dc_line - 1u);
    while (p < end)
    {
        __asm__ __volatile__("dc cvau, %0" ::"r"(p) : "memory");
        p += dc_line;
    }
    __asm__ __volatile__("dsb ish" ::: "memory");

    p = start & ~(uintptr_t)(ic_line - 1u);
    while (p < end)
    {
        __asm__ __volatile__("ic ivau, %0" ::"r"(p) : "memory");
        p += ic_line;
    }
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
}

#endif
