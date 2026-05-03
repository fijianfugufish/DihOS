#ifndef KERNEL_ASM_ASM_H
#define KERNEL_ASM_ASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void asm_wait(void);
    void asm_relax(void);
    void asm_compiler_barrier(void);
    void asm_mmio_barrier(void);
    void asm_dma_clean_range(const void *ptr, uint64_t len);
    void asm_dma_invalidate_range(const void *ptr, uint64_t len);
    void asm_sync_executable_range(const void *ptr, uint64_t len);
    void asm_aa64_install_exception_vectors(void);
    void asm_aa64_set_probe_trace(int enabled);
    int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value);
    int asm_aa64_try_write32(uint64_t addr, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
