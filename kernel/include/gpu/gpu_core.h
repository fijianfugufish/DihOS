#pragma once

#include <stdint.h>
#include "bootinfo.h"
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_iommu.h"

typedef enum gpu_device_state
{
    GPU_DEVICE_ABSENT = 0,
    GPU_DEVICE_DISCOVERED,
    GPU_DEVICE_READY,
    GPU_DEVICE_LOST,
} gpu_device_state;

typedef struct gpu_device_caps
{
    uint64_t max_gpu_va_bits;
    uint32_t flags;
} gpu_device_caps;

#define GPU_CAP_DMA_ISOLATED  (1u << 0)
#define GPU_CAP_FENCES        (1u << 1)
#define GPU_CAP_GLES          (1u << 2)

typedef struct gpu_device_info
{
    gpu_device_state state;
    gpu_device_caps caps;
    uint64_t regs_base;
    uint64_t regs_size;
    uint32_t primary_irq;
    uint32_t firmware_required;
    uint32_t firmware_present;
    uint32_t mmio_cpu_mapped;
} gpu_device_info;

typedef struct gpu_scanout_target
{
    gpu_buffer buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pixel_format;
} gpu_scanout_target;

/* Initialises discovery, resource staging and the optional UEFI scanout
 * target. It never enables a GPU. */
int gpu_core_init(const boot_info *boot);
const gpu_device_info *gpu_core_primary(void);
const gpu_firmware_set *gpu_core_firmware(void);
const gpu_iommu_topology *gpu_core_iommu_topology(void);
const gpu_scanout_target *gpu_core_scanout_target(void);
