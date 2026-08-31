#pragma once

#include <stdint.h>
#include "gpu/gpu_memory.h"

/*
 * Firmware is described by drivers, but owned by the operator.  A driver may
 * inspect and validate a staged package; it must never silently obtain blobs
 * from another OS installation or embed vendor firmware in KERNEL.ELF.
 */
typedef enum gpu_firmware_role
{
    GPU_FIRMWARE_PLATFORM_ACPI = 0,
    GPU_FIRMWARE_GMU,
    GPU_FIRMWARE_SQE,
    GPU_FIRMWARE_SECURE,
    GPU_FIRMWARE_SEQUENCE,
} gpu_firmware_role;

typedef struct gpu_firmware_file
{
    const char *name;
    gpu_firmware_role role;
    uint32_t required;
    uint32_t expected_size;
    /* Bytes stripped from the source container before staging the
     * device-visible image.  Use zero for firmware whose file begins with
     * executable/device data. */
    uint32_t payload_offset;
} gpu_firmware_file;

typedef struct gpu_firmware_manifest
{
    const char *driver_name;
    const char *directory;
    const gpu_firmware_file *files;
    uint32_t file_count;
} gpu_firmware_manifest;

typedef struct gpu_firmware_inventory
{
    uint32_t required_count;
    uint32_t present_count;
    uint32_t optional_present_count;
} gpu_firmware_inventory;

#define GPU_FIRMWARE_MAX_BLOBS 8u
typedef struct gpu_firmware_blob
{
    gpu_firmware_role role;
    uint32_t payload_offset;
    gpu_buffer buffer;
} gpu_firmware_blob;

typedef struct gpu_firmware_set
{
    gpu_firmware_blob blobs[GPU_FIRMWARE_MAX_BLOBS];
    uint32_t blob_count;
    uint64_t total_bytes;
} gpu_firmware_set;

/* Read-only existence check. Does not load, parse, map, or execute firmware. */
int gpu_firmware_scan(const gpu_firmware_manifest *manifest,
                      gpu_firmware_inventory *out);

/* Loads only complete, size-validated blobs into CPU memory.  The caller owns
 * the set and must map its buffers into an IOMMU domain before hardware use. */
int gpu_firmware_load(const gpu_firmware_manifest *manifest,
                      gpu_firmware_set *out);
void gpu_firmware_release(gpu_firmware_set *set);

/* Returns a staged blob by its semantic role.  GPU-family code must not rely
 * on the manifest's incidental file ordering when binding firmware IOVAs. */
const gpu_firmware_blob *gpu_firmware_find(const gpu_firmware_set *set,
                                           gpu_firmware_role role);

/* Returns the aligned address of a staged device-visible image.  Returns zero
 * until the payload is valid and the blob has been mapped. */
uint64_t gpu_firmware_payload_iova(const gpu_firmware_blob *blob);
