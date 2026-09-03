#pragma once

#include <stdint.h>
#include "bootinfo.h"
#include "gpu/gpu_firmware.h"
#include "gpu/gpu_iommu.h"
#include "gpu/gpu_render.h"
#include "gpu/gpu_scheduler.h"
#include "mesart/mesart_renderer.h"

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
/* Shared policy queue.  Only kernel graphics code may register a compositor
 * client; the verified Mesa renderer service gets its own unprivileged client. */
gpu_scheduler *gpu_core_scheduler(void);

/* Executes the scheduler-selected sealed batch through the kernel-owned
 * Adreno backend.  The first implementation copies only a broker-validated
 * A7xx stream (inert NOPs, bounded renderer-arena writes, and ordering) into
 * a boot-mapped kernel ring, then completes its fence after CP consumption.
 * It never exposes MMIO, IOVA allocation, or a hardware fence to EL0. */
int gpu_core_execute_next_scheduled(uint64_t *out_fence);

/* Maps a kernel-allocated Mesart buffer into the renderer's fixed GPU-VA
 * aperture during service admission.  It rejects active scheduler work and
 * flushes only render CB0; this is not a general user-visible DMA mapper. */
int gpu_core_map_mesart_buffer(gpu_buffer *buffer, uint64_t gpu_va);

/* Returns one of the two fixed shader slots reserved while the DMA domain is
 * constructed, before CB0 is attached.  The slots are kernel-only: callers
 * may copy an authenticated MIR3 artifact there, but no EL0 mapping or
 * general IOVA allocation is involved.  This avoids mutating live SMMU page
 * tables when a renderer service is admitted after GPU bring-up. */
int gpu_core_mesart_shader_slot(uint32_t slot, gpu_buffer **out_storage,
                                uint64_t *out_offset,
                                uint64_t *out_gpu_va,
                                uint64_t *out_bytes);

/* Resolves a trusted render target into its private GPU mapping. It accepts
 * only the boot-imported scanout allocation or a live gpu_render surface;
 * arbitrary CPU pointers never become DMA authority. This is backend-only:
 * KGFX and EL0 never receive the returned address. */
int gpu_core_render_target_iova(const gpu_render_target *target,
                                uint64_t *out_gpu_va);

/* Builds (but deliberately does not publish) the fixed first A7xx draw for
 * an already authenticated Mesart shader pair. This is a kernel-only
 * preflight used to prove the exact state encoder before a live 3D submit is
 * enabled. `out_dwords` is diagnostic only; no GPU address is returned. */
int gpu_core_mesart_3d_preflight(
    const mesart_renderer_graphics_pipeline *pipeline, uint32_t *out_dwords);

/* Starts the fixed trusted 3D draw and returns as soon as CP has accepted the
 * sealed ring.  It is kernel-only: callers must retain the authenticated
 * pipeline and target until gpu_core_mesart_3d_poll reports its fence.
 * Returns the fence through out_fence; it never grants EL0 raw submission. */
int gpu_core_mesart_3d_kick(
    const mesart_renderer_graphics_pipeline *pipeline, uint64_t *out_fence);

/* Checks the one trusted 3D fence without spinning. Returns 0 when a fence
 * completed, 1 while no work is pending or the live fence is still running,
 * and a negative error after failing the runtime closed. */
int gpu_core_mesart_3d_poll(uint64_t *out_fence);

/* True only while CP/RB own the trusted runtime ring. The compositor uses
 * this to avoid a CPU present racing an in-flight GPU render. */
uint8_t gpu_core_mesart_3d_busy(void);

/* Synchronous diagnostic wrapper around kick/poll. It remains only for the
 * explicit shell self-test; normal compositor work must use kick/poll. */
int gpu_core_mesart_3d_submit(
    const mesart_renderer_graphics_pipeline *pipeline, uint64_t *out_fence);
