#pragma once

/*
 * Kernel-driver-only bridge for the generic render-surface pool.
 *
 * This header is deliberately separate from gpu_render.h: KGFX and other
 * high-level users can create/use a surface without learning its physical or
 * GPU-visible representation.  A compiled-in backend may request the
 * backing allocation, then map it through its own IOMMU policy.
 */

#include "gpu/gpu_memory.h"
#include "gpu/gpu_render.h"

/* Returns 0 and a backing allocation for a live kernel-owned surface; returns
 * 1 when target is an external CPU view; negative values reject stale or
 * forged surface handles.  The returned buffer remains owned by gpu_render. */
int gpu_render_backend_surface_backing(const gpu_render_target *target,
                                       gpu_buffer **out_buffer);
int gpu_render_backend_texture_backing(const gpu_render_texture *texture,
                                       gpu_buffer **out_buffer);
