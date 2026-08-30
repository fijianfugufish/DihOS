#pragma once

#include <stdint.h>

/* A CPU allocation is not GPU-addressable until an IOMMU domain maps it. */
typedef struct gpu_buffer
{
    void *cpu;
    uint64_t phys;
    uint64_t iova;
    uint64_t size_bytes;
    uint64_t pages;
    uint32_t flags;
} gpu_buffer;

#define GPU_BUFFER_COMMAND  (1u << 0)
#define GPU_BUFFER_DATA     (1u << 1)
#define GPU_BUFFER_ZEROED   (1u << 2)
#define GPU_BUFFER_EXTERNAL (1u << 3)

int gpu_buffer_alloc(gpu_buffer *out, uint64_t size_bytes, uint32_t flags);
/* Registers storage owned by another subsystem (for example, UEFI scanout).
 * Releasing this view never frees the backing memory. */
int gpu_buffer_import(gpu_buffer *out, void *cpu, uint64_t phys,
                      uint64_t size_bytes, uint32_t flags);
void gpu_buffer_release(gpu_buffer *buffer);

/* Cleans CPU cache lines before a future DMA submission. iova must be nonzero
 * before a KMD may give this buffer to hardware. */
void gpu_buffer_prepare_for_device(const gpu_buffer *buffer);
