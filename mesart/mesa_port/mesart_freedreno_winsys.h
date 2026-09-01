#pragma once

#include <stdint.h>

#include "mesart_runtime.h"

/*
 * DihOS implementation boundary for Mesa's Freedreno KMD-facing code.
 *
 * This is intentionally shaped around a device and buffer objects because
 * those are the two objects the upstream driver needs before it can build a
 * command stream.  It is not a DRM compatibility layer: GPU addresses are
 * capability-limited VAs supplied by Mesart, never file descriptors, GEM
 * handles, physical addresses, or arbitrary mappings.
 */

#define MESART_FD_BO_COMMAND  (1u << 0)
#define MESART_FD_BO_RESOURCE (1u << 1)

typedef struct mesart_fd_device
{
    mesart_runtime runtime;
    mesart_runtime_gpu_arena gpu_arena;
    uint32_t gpu_model;
    uint32_t abi_version;
    uint64_t chip_id;
    uint8_t ready;
} mesart_fd_device;

typedef struct mesart_fd_bo
{
    void *cpu;
    uint64_t gpu_va;
    uint64_t bytes;
    uint32_t flags;
} mesart_fd_bo;

typedef struct mesart_fd_submit
{
    mesart_runtime_command_buffer command;
} mesart_fd_submit;

/* Opens the kernel-admitted renderer heap and GPU arena. */
int mesart_fd_device_init(mesart_fd_device *device);

/* Allocates a permanently pinned region from the service's bounded GPU
 * arena.  Frees are intentionally absent in this bootstrap implementation:
 * the complete arena is torn down when the signed renderer exits. */
int mesart_fd_bo_alloc(mesart_fd_device *device, uint64_t bytes,
                       uint64_t alignment, uint32_t flags,
                       mesart_fd_bo *out_bo);

/* One verified primary submission per current renderer lifetime.  The
 * command grammar remains kernel-defined; this adapter only owns the Mesa
 * side of opening and flushing the primary stream. */
int mesart_fd_submit_begin(mesart_fd_device *device,
                           mesart_fd_submit *out_submit);
int mesart_fd_submit_flush(const mesart_fd_submit *submit, uint64_t dwords,
                           uint64_t *out_fence);

/* Smoke-test the actual winsys object path in the signed EL0 bundle. */
int mesart_fd_winsys_selftest(void);

/* The first live winsys submission: write a marker through a Mesa-shaped BO
 * and primary-submit object.  The kernel still validates and owns the final
 * command stream, fence, and hardware queue. */
int mesart_fd_submit_resource_write_selftest(void);
