#include "gpu/gpu_zap.h"
#include "gpu/gpu_qcom_scm.h"
#include "memory/mmio_map.h"
#include "asm/asm.h"
#include "terminal/terminal_api.h"

/* Protocol reference: Qualcomm PAS service 2 (INIT=1, MEM=2, AUTH=5),
 * GPU peripheral 13. Independent DihOS implementation; not a Linux port.
 * These pages are never returned to pmem: TZ may retain ownership even
 * after a failed/unfinished call. No GPU IOVA exposes the relocated image. */
static gpu_buffer metadata, image;
static int attempted, ready;

static void stage(const char *name)
{
    terminal_print(name);
    terminal_flush_log();
}
static int result(int rc, const gpu_scm_result *r)
{
    terminal_print("[K:GPU] zap PAS wrapper rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(r->esr ? "[K:GPU] zap trapped call x0 (not a valid return status)=" : "[K:GPU] zap PAS transport x0=");
    terminal_print_inline_hex64(r->transport);
    terminal_print("[K:GPU] zap PAS service x1=");
    terminal_print_inline_hex64(r->result);
    terminal_print("[K:GPU] zap PAS trap ESR=");
    terminal_print_inline_hex64(r->esr);
    terminal_flush_log();
    return rc;
}
static int uncache(uint64_t phys, uint64_t bytes)
{
    /* Call only on exclusively owned identity-mapped pages. Discard cache
     * aliases before the noncacheable mapping; never touch these as WB again. */
    asm_dma_clean_range((void *)(uintptr_t)phys,bytes);
    asm_dma_invalidate_range((void *)(uintptr_t)phys,bytes);
    return mmio_map_normal_nc_identity(phys,bytes);
}

int gpu_zap_start(const gpu_firmware_set *firmware)
{
    gpu_zap_layout layout;
    gpu_scm_result r;
    const gpu_firmware_blob *blob;
    uint64_t phys;
    if (ready) return 0;
    if (attempted) {
        stage("[K:GPU] zap previous attempt failed; retry withheld until reboot");
        return -1;
    }
    attempted=1;
    blob=gpu_firmware_find(firmware,GPU_FIRMWARE_SECURE);
    if (!blob || gpu_zap_parse(blob->buffer.cpu,blob->buffer.size_bytes,&layout)) {
        stage("[K:GPU] zap invalid or unsupported monolithic ELF32 image");
        return -2;
    }
    stage("[K:GPU] zap PAS availability");
    if (result(gpu_qcom_pas_available(&r),&r)) return -3;
    stage("[K:GPU] zap PAS GPU-13 support");
    if (result(gpu_qcom_pas_supported(13,&r),&r)) return -4;
    /* Own enough RAM for a 1-MiB-aligned relocation, never use the ELF's
     * 0x1000 p_paddr as an absolute destination or guess a reserved carveout. */
    if (gpu_buffer_alloc(&metadata,4096,GPU_BUFFER_DATA|GPU_BUFFER_ZEROED) ||
        gpu_buffer_alloc(&image,2*1024*1024,GPU_BUFFER_DATA|GPU_BUFFER_ZEROED)) return -5;
    if ((uint64_t)(uintptr_t)metadata.cpu != metadata.phys ||
        (uint64_t)(uintptr_t)image.cpu != image.phys) return -6;
    phys=(image.phys+0xfffffu)&~0xfffffull;
    if (phys < image.phys || phys-image.phys+layout.memory_bytes > image.size_bytes)
        return -6;
    if (uncache(metadata.phys,4096) || uncache(phys,layout.memory_bytes)) {
        stage("[K:GPU] zap Normal-NC shared RAM mapping failed; PAS withheld");
        return -7;
    }
    volatile uint8_t *m=(volatile uint8_t *)metadata.cpu;
    const uint8_t *src=blob->buffer.cpu;
    for (uint32_t i=0;i<layout.header_bytes;i++) m[i]=src[i];
    for (uint32_t i=0;i<layout.hash_bytes;i++) m[layout.header_bytes+i]=src[layout.hash_offset+i];
#if defined(__aarch64__)
    __asm__ __volatile__("dsb sy" ::: "memory");
#endif
    terminal_print("[K:GPU] zap owned metadata/image PA and image bytes=");
    terminal_print_inline_hex64(metadata.phys);
    terminal_print_inline_hex64(phys);
    terminal_print_inline_hex64(layout.memory_bytes);
    stage("[K:GPU] zap PAS INIT_IMAGE");
    if (result(gpu_qcom_pas_init(13,metadata.phys,&r),&r)) return -8;
    stage("[K:GPU] zap PAS MEM_SETUP");
    if (result(gpu_qcom_pas_memory(13,phys,layout.memory_bytes,&r),&r)) return -9;
    volatile uint8_t *dst=(volatile uint8_t *)(uintptr_t)phys;
    for (uint32_t i=0;i<layout.memory_bytes;i++)
        dst[i]= i<layout.code_bytes ? src[layout.code_offset+i] : 0;
    /* Shared RAM stores must reach RAM before secure firmware authenticates it. */
#if defined(__aarch64__)
    __asm__ __volatile__("dsb sy" ::: "memory");
#endif
    stage("[K:GPU] zap PAS AUTH_AND_RESET");
    if (result(gpu_qcom_pas_start(13,&r),&r)) return -10;
    ready=1;
    stage("[K:GPU] zap authenticated and started; CP handoff may now run");
    return 0;
}
