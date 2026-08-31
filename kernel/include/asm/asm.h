#ifndef KERNEL_ASM_ASM_H
#define KERNEL_ASM_ASM_H

#include <stdint.h>
struct kfont;

#ifdef __cplusplus
extern "C"
{
#endif

    void asm_wait(void);
    void asm_relax(void);
    void asm_compiler_barrier(void);
    void asm_enable_fp_simd(void);
    void asm_mmio_barrier(void);
    void asm_dma_clean_range_raw(const void *ptr, uint64_t len);
    void asm_dma_clean_range(const void *ptr, uint64_t len);
    void asm_dma_invalidate_range(const void *ptr, uint64_t len);
    void asm_sync_executable_range(const void *ptr, uint64_t len);
    void asm_aa64_install_exception_vectors(void);
    void asm_aa64_panic_renderer_init(const struct kfont *font);
    uint32_t asm_aa64_panic_active(void);
    void asm_aa64_set_probe_trace(int enabled);
    int asm_aa64_try_read32(uint64_t addr, uint32_t *out_value);
    int asm_aa64_try_write32(uint64_t addr, uint32_t value);
    int asm_aa64_try_hvc(uint32_t immediate,
                         uint64_t *x0,
                         uint64_t *x1,
                         uint64_t *x2,
                         uint64_t *x3,
                         uint64_t *out_esr);
    int asm_aa64_try_smc(uint32_t immediate,
                         uint64_t *x0,
                         uint64_t *x1,
                         uint64_t *x2,
                         uint64_t *x3,
                         uint64_t *out_esr);
    /* Guarded SMC variant for Qualcomm SCM calls that carry four arguments
     * in x2..x5.  The function returns an exception-probe result; secure
     * firmware's own status remains in x0. */
    int asm_aa64_try_smc6(uint32_t immediate,
                          uint64_t *x0,
                          uint64_t *x1,
                          uint64_t *x2,
                          uint64_t *x3,
                          uint64_t *x4,
                          uint64_t *x5,
                          uint64_t *out_esr);
    int asm_aa64_try_hv_set_vpreg(uint32_t reg,
                                  uint64_t value,
                                  uint64_t *out_status,
                                  uint64_t *out_esr);

#ifdef __cplusplus
}
#endif

#endif
