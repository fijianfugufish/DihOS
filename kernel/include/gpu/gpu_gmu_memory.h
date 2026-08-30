#pragma once

#include <stdint.h>
#include "gpu/gpu_gmu_image.h"
#include "gpu/gpu_iommu.h"

/* Gen7 GMU firmware has fixed external-memory addresses.  These are GMU
 * IOVAs, not CPU physical addresses, and are intentionally kept out of the
 * generic application allocation range. */
#define GPU_GMU_ICACHE_IOVA  0x0000000000004000ull
#define GPU_GMU_ICACHE_BYTES (0x01000000u - 0x00004000u)
#define GPU_GMU_DUMMY_IOVA   0x0000000060000000ull
#define GPU_GMU_DUMMY_BYTES  0x00001000u
#define GPU_GMU_DEBUG_IOVA   0x0000000060400000ull
#define GPU_GMU_DEBUG_BYTES  0x00007000u
/* HFI and the GMU log are host-shared, uncached allocations.  They must live
 * in the GMU's 0x60000000 user aperture, after its fixed dummy page; the
 * 0x00004000..0x01000000 aperture is reserved for privileged external code. */
#define GPU_GMU_HFI_IOVA     0x0000000060001000ull
#define GPU_GMU_HFI_BYTES    0x00004000u
/* A7xx GENERAL_8 encodes the log IOVA in a 32-bit field. */
#define GPU_GMU_LOG_IOVA     0x0000000060005000ull
#define GPU_GMU_LOG_BYTES    0x00004000u

typedef struct gpu_gmu_memory
{
    gpu_buffer icache;
    gpu_buffer dummy;
    gpu_buffer debug;
    gpu_buffer hfi;
    gpu_buffer log;
    uint32_t external_segments_loaded;
    uint32_t hfi_queue_count;
    uint32_t hfi_next_sequence;
} gpu_gmu_memory;

/* Allocates and fills CPU-owned GMU resources only. */
int gpu_gmu_memory_stage(const gpu_gmu_image *image, gpu_gmu_memory *out);
/* Adds every staged resource to the already-prepared GPU IOMMU domain. */
int gpu_gmu_memory_map(gpu_iommu_domain *domain, gpu_gmu_memory *memory);
void gpu_gmu_memory_release(gpu_gmu_memory *memory);
