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

#ifdef __cplusplus
}
#endif

#endif
