#pragma once

#include <stdint.h>
#include "gpu/gpu_gmu_image.h"
#include "gpu/gpu_gmu_memory.h"
#include "gpu/gpu_mmio.h"

typedef struct gpu_gmu_start_status
{
    uint32_t fw_init_result;
    uint32_t hfi_control_status;
    uint32_t hfi_control_attempts;
    uint32_t dtcm_breadcrumb;
    uint32_t fw_init_used_compatibility_bit;
    uint32_t fw_init_confirmed_after_poll;
} gpu_gmu_start_status;

/* Reads the reset-signature word documented at the end of DTCM.  It is the
 * final read-only validation that the GMU resource uses the expected local
 * register layout. */
int gpu_gmu_tcm_preflight(const gpu_mmio_window *gmu,
                          uint32_t *out_reset_signature);

/* Copies the ITCM and DTCM portions of a validated Gen7 GMU image through
 * the GMU's documented TCM windows.  It does not release reset, start the
 * GMU, or issue an HFI command. */
int gpu_gmu_upload_tcm(const gpu_mmio_window *gmu,
                       const gpu_gmu_image *image,
                       uint32_t *out_segments_written,
                       uint32_t *out_bytes_written);

/* Releases the cold-boot reset only after TCM upload and all shared DMA
 * buffers have been prepared and mapped.  The platform must establish the
 * RPMh votes first; this function never invents them. */
int gpu_gmu_start_cold(const gpu_mmio_window *gmu,
                       const gpu_gmu_memory *memory,
                       uint32_t reset_signature, uint32_t chip_id,
                       gpu_gmu_start_status *out_status);

/* Reasserts only the idempotent HFI queue-control latch on an already-ready
 * Gen7 firmware image.  It does not reset the GMU, change power state, or
 * touch GX. */
int gpu_gmu_gen7_start_hfi_control(const gpu_mmio_window *gmu,
                                   gpu_gmu_start_status *in_out_status);

/* Requests the non-legacy GMU's GPU_SET out-of-band lease.  While held, the
 * firmware keeps GX registers powered for the caller's hardware-init
 * sequence.  Release the lease exactly once after that sequence. */
int gpu_gmu_gen7_acquire_gpu(const gpu_mmio_window *gmu,
                             uint32_t *out_ack_status);
int gpu_gmu_gen7_release_gpu(const gpu_mmio_window *gmu);
