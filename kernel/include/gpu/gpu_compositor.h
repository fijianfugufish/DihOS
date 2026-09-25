#pragma once

#include <stdint.h>

#include "gpu/gpu_render.h"
#include "mesart/mesart_renderer.h"

/* Admits the signed core shader catalogue once storage and the GPU domain are
 * ready. Failure is non-fatal: KGFX remains the complete CPU renderer. */
/* Reserve the private output before gpu_core_init attaches its SMMU domain. */
int gpu_compositor_prepare_target(void);
int gpu_compositor_init(void);
uint8_t gpu_compositor_core_ready(void);
const mesart_renderer_graphics_pipeline *gpu_compositor_core_pipeline(
    gpu_render_pipeline_kind kind);
void gpu_compositor_update(void);
uint8_t gpu_compositor_frame_busy(void);
void gpu_compositor_submit_kgfx_frame(void);
