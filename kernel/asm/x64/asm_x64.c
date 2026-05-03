#include "asm/asm.h"
#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64)

void asm_wait(void)
{
    __asm__ __volatile__("hlt" ::: "memory");
}

void asm_relax(void)
{
    __asm__ __volatile__("pause" ::: "memory");
}

void asm_compiler_barrier(void)
{
    __asm__ __volatile__("" ::: "memory");
}

void asm_mmio_barrier(void)
{
    __asm__ __volatile__("mfence" ::: "memory");
}

void asm_dma_clean_range(const void *ptr, uint64_t len)
{
    (void)ptr;
    (void)len;
    asm_compiler_barrier();
}

void asm_dma_invalidate_range(const void *ptr, uint64_t len)
{
    (void)ptr;
    (void)len;
    asm_compiler_barrier();
}

void asm_sync_executable_range(const void *ptr, uint64_t len)
{
    (void)ptr;
    (void)len;
    asm_compiler_barrier();
}

void asm_aa64_install_exception_vectors(void)
{
}

void asm_aa64_set_probe_trace(int enabled)
{
    (void)enabled;
}

int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value)
{
    (void)addr;
    if (out_value)
        *out_value = 0u;
    return -1;
}

int asm_aa64_try_write32(uint64_t addr, uint32_t value)
{
    (void)addr;
    (void)value;
    return -1;
}

#endif
