#pragma once

/*
 * The deliberately narrow first A7xx draw encoder.
 *
 * Its input is an authenticated Mesart pipeline reflection plus a
 * driver-neutral target.  It is not a packet ABI: all register selection,
 * address placement and packet framing remain inside the kernel.
 */

#include <stdint.h>

#include "gpu/gpu_render.h"
#include "gpu/gpu_ring.h"
#include "mesart/mesart_renderer.h"

typedef struct adreno_x1_85_3d_draw
{
    const mesart_renderer_graphics_pipeline *pipeline;
    const gpu_render_target *target;
    uint64_t target_gpu_va;
    /* The first profile's geometry is a kernel-owned three-vertex vec2
     * buffer.  This address is never supplied by Mesart EL0 or MPIP. */
    uint64_t vertex_buffer_gpu_va;
    uint32_t vertex_buffer_bytes;
    uint32_t vertex_buffer_stride;
    /* Default GLSL uniforms are lowered by Mesa to application UBO 0.  These
     * addresses always come from the fixed kernel-owned uniform pool, never
     * from Mesart EL0 or a signed artifact. */
    uint64_t vertex_default_ubo_gpu_va;
    uint64_t fragment_default_ubo_gpu_va;
    uint32_t vertex_default_ubo_bytes;
    uint32_t fragment_default_ubo_bytes;
} adreno_x1_85_3d_draw;

/* Checks the exact first 3D profile before a command ring is touched.  The
 * initial profile has one kernel-owned R32G32_FLOAT attribute and one linear
 * BGRX render target (the boot GOP format recorded for X1-85). */
int adreno_x1_85_3d_validate_draw(const adreno_x1_85_3d_draw *draw);

/* Encodes the non-context X1-85/A740 values that Mesa marks as the device's
 * raw baseline. It is separated from per-draw state so the live submit path
 * can establish it once per fresh GX context. */
int adreno_x1_85_3d_emit_x1e_baseline(gpu_command_ring *ring);

/* The direct-sysmem prologue invalidates stale A7xx colour/depth/LRZ caches
 * and installs the zeroed fixed-function context. Render completion is
 * emitted separately through the backend's RB_DONE_TS fence, whose caller
 * owns the private completion allocation. */
int adreno_x1_85_3d_emit_sysmem_prologue(gpu_command_ring *ring);

/* Makes the completed direct-sysmem colour pass observable outside A7xx's
 * CCU and caches.  It must follow the last draw and precede the completion
 * fence for that submission. */
int adreno_x1_85_3d_emit_sysmem_fini(gpu_command_ring *ring);

/* Independent fixed-function write-path diagnostic: clear a private,
 * mapped 16x16 RGBA8 surface (64-byte pitch) to opaque green. */
int adreno_x1_85_emit_solid_fill_probe(gpu_command_ring *ring, uint64_t iova);

/* Selects six A7xx hardware counters used only to diagnose the first
 * direct-sysmem draw.  The counters are monotonic, so the caller snapshots
 * them immediately before and after a draw and uses their deltas.  This
 * keeps the investigation inside the GPU: VPC proves primitive assembly,
 * TSE proves raster setup, and RB proves fragment dispatch/store activity. */
int adreno_x1_85_3d_emit_stage_counter_select(gpu_command_ring *ring);

/* Emits only the signed shader, VPC linkage and sysmem colour-target state.
 * The caller must have installed the separately captured X1E context baseline
 * and must finish the batch with adreno_x1_85_emit_auto_triangle_draw(). */
int adreno_x1_85_3d_emit_draw_state(gpu_command_ring *ring,
                                    const adreno_x1_85_3d_draw *draw);
