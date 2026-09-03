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
    /* Default GLSL uniforms are lowered by Mesa to application UBO 0.  These
     * addresses always come from the fixed kernel-owned uniform pool, never
     * from Mesart EL0 or a signed artifact. */
    uint64_t vertex_default_ubo_gpu_va;
    uint64_t fragment_default_ubo_gpu_va;
    uint32_t vertex_default_ubo_bytes;
    uint32_t fragment_default_ubo_bytes;
} adreno_x1_85_3d_draw;

/* Checks the exact first 3D profile before a command ring is touched.  The
 * initial profile is an input-free, non-discarding vertex-ID triangle with
 * one linear BGRX render target (the boot GOP format recorded for X1-85). */
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

/* Emits only the signed shader, VPC linkage and sysmem colour-target state.
 * The caller must have installed the separately captured X1E context baseline
 * and must finish the batch with adreno_x1_85_emit_auto_triangle_draw(). */
int adreno_x1_85_3d_emit_draw_state(gpu_command_ring *ring,
                                    const adreno_x1_85_3d_draw *draw);
