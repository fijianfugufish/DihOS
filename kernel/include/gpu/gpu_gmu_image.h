#pragma once

#include <stdint.h>
#include "gpu/gpu_firmware.h"

/* Gen7 GMU firmware is a sequence of 16-byte block headers followed by
 * payloads.  Keep parsing separate from MMIO upload so malformed operator
 * firmware cannot reach the device. */
#define GPU_GMU_MAX_SEGMENTS 32u

typedef enum gpu_gmu_segment_target
{
    GPU_GMU_SEGMENT_ITCM = 0,
    GPU_GMU_SEGMENT_DTCM,
    GPU_GMU_SEGMENT_EXTERNAL,
} gpu_gmu_segment_target;

typedef struct gpu_gmu_segment
{
    gpu_gmu_segment_target target;
    uint32_t address;
    uint32_t size_bytes;
    const uint8_t *data;
} gpu_gmu_segment;

typedef struct gpu_gmu_image
{
    gpu_gmu_segment segments[GPU_GMU_MAX_SEGMENTS];
    uint32_t segment_count;
    uint32_t itcm_bytes;
    uint32_t dtcm_bytes;
    uint32_t external_bytes;
} gpu_gmu_image;

/* Decodes, bounds-checks, and classifies a loaded Gen7 GMU blob.  It performs
 * no register access and keeps pointers valid only while firmware_set lives. */
int gpu_gmu_image_parse(const gpu_firmware_set *firmware, gpu_gmu_image *out);
