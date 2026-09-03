#pragma once

#include <stdint.h>

/*
 * Driver-neutral kernel rendering contract.
 *
 * This deliberately sits beside KGFX rather than replacing it.  KGFX remains
 * the always-available CPU scene rasterizer and is the only present path for
 * now.  A caller asks this dispatcher whether an accelerator can consume a
 * small, kernel-owned composition primitive; if not, it receives an explicit
 * fallback result and continues through the existing CPU path.
 *
 * No type here exposes a CP packet, GPU virtual address, physical address, or
 * vendor register.  A backend (Adreno today, another GPU later) owns those
 * details behind gpu_render_backend_ops.
 */

#define GPU_RENDER_ABI_VERSION 1u
#define GPU_RENDER_MAX_UNIFORM_VEC4S 64u
#define GPU_RENDER_MAX_TEXTURES 8u
#define GPU_RENDER_MAX_SURFACES 8u
/* Matches the isolated per-surface aperture reserved by gpu_core. Keeping a
 * surface inside one fixed slot means a backend can map it once without
 * creating a general-purpose IOVA allocator. */
#define GPU_RENDER_MAX_SURFACE_BYTES (32u * 1024u * 1024u)

#define GPU_RENDER_CAP_COLOR_TARGET       (1u << 0)
#define GPU_RENDER_CAP_AUTO_TRIANGLES     (1u << 1)
#define GPU_RENDER_CAP_UNIFORM_VEC4       (1u << 2)
#define GPU_RENDER_CAP_TEXTURE_2D         (1u << 3)
#define GPU_RENDER_CAP_ALPHA_BLEND        (1u << 4)

typedef enum gpu_render_pixel_format
{
    GPU_RENDER_FORMAT_INVALID = 0,
    GPU_RENDER_FORMAT_ARGB8888 = 1,
    GPU_RENDER_FORMAT_XRGB8888 = 2,
    GPU_RENDER_FORMAT_BGRX8888 = 3,
} gpu_render_pixel_format;

typedef enum gpu_render_sample_mode
{
    GPU_RENDER_SAMPLE_NEAREST = 0,
    GPU_RENDER_SAMPLE_BILINEAR = 1,
} gpu_render_sample_mode;

typedef enum gpu_render_pipeline_kind
{
    /* Kernel-selected signed shader pair; the backend never accepts a shader
     * address or name from a caller. */
    GPU_RENDER_PIPELINE_COMPOSITOR_SOLID = 1,
    GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED = 2,
} gpu_render_pipeline_kind;

/* Opaque identity of a CPU-fallback-capable kernel render surface.  It is
 * not an address, and a stale identity is rejected by the private backend
 * bridge.  A zero-initialized handle deliberately means "external CPU view"
 * so existing callers can be migrated to surfaces gradually. */
typedef struct gpu_render_surface_handle
{
    uint16_t slot;
    uint16_t generation;
} gpu_render_surface_handle;

#define GPU_RENDER_EXTERNAL_SURFACE ((gpu_render_surface_handle){0u, 0u})

typedef struct gpu_render_surface_desc
{
    uint32_t width;
    uint32_t height;
    gpu_render_pixel_format format;
} gpu_render_surface_desc;

typedef struct gpu_render_target
{
    /* CPU mapping is supplied only by trusted kernel code so it can retain a
     * CPU fallback. The selected backend translates it into its private GPU
     * allocation/scanout mapping itself. */
    gpu_render_surface_handle surface;
    void *cpu_pixels;
    /* Exact CPU-visible allocation size.  This lets the generic layer reject
     * a malformed stride/height before a backend ever derives a GPU mapping.
     * It is not a GPU address and it is never passed through to a driver. */
    uint64_t cpu_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    gpu_render_pixel_format format;
} gpu_render_target;

typedef struct gpu_render_texture
{
    /* Optional opaque surface identity. A zero handle describes an external
     * CPU texture, which a backend must copy into a private surface before
     * sampling; a nonzero one names kernel-owned storage directly. */
    gpu_render_surface_handle surface;
    const void *cpu_pixels;
    uint64_t cpu_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    gpu_render_pixel_format format;
    gpu_render_sample_mode sample_mode;
} gpu_render_texture;

typedef struct gpu_render_submission
{
    gpu_render_pipeline_kind pipeline;
    gpu_render_target target;
    /* First-profile geometry is always an auto-indexed triangle. */
    uint32_t vertex_count;
    /* Arrays contain count consecutive vec4 values (four floats each).
     * The generic layer only validates their bounds; a backend copies their
     * values to a private constant allocation and never DMA-reads caller
     * pointers directly. */
    const float *vertex_uniform_vec4s;
    uint32_t vertex_uniform_count;
    const float *fragment_uniform_vec4s;
    uint32_t fragment_uniform_count;
    const gpu_render_texture *textures;
    uint32_t texture_count;
} gpu_render_submission;

typedef struct gpu_render_backend_info
{
    const char *name;
    uint32_t abi_version;
    uint32_t capabilities;
} gpu_render_backend_info;

typedef int (*gpu_render_backend_submit_fn)(
    const gpu_render_submission *submission, uint64_t *out_fence);

typedef struct gpu_render_backend_ops
{
    gpu_render_backend_info info;
    gpu_render_backend_submit_fn submit;
} gpu_render_backend_ops;

typedef enum gpu_render_dispatch_result
{
    GPU_RENDER_DISPATCH_SUBMITTED = 0,
    /* Normal result: no accelerator is registered or this frame needs a
     * feature the backend does not yet support. KGFX should rasterize it. */
    GPU_RENDER_DISPATCH_CPU_FALLBACK = 1,
} gpu_render_dispatch_result;

/* A small, driver-neutral readiness report for the compositor and shell.
 * `accelerator_ready` means a backend actually accepted registration.  It
 * deliberately says nothing about vendor state or raw hardware addresses. */
typedef struct gpu_render_status
{
    uint32_t abi_version;
    uint32_t capabilities;
    uint8_t accelerator_ready;
} gpu_render_status;

void gpu_render_init(void);
/* Surface storage is always CPU-mapped and zeroed so a caller can preserve a
 * byte-for-byte CPU rendering fallback.  Releasing a surface requires the
 * caller to have waited for every fence returned for it. */
int gpu_render_surface_create(const gpu_render_surface_desc *desc,
                              gpu_render_surface_handle *out_surface);
int gpu_render_surface_target(gpu_render_surface_handle surface,
                              gpu_render_target *out_target);
int gpu_render_surface_texture(gpu_render_surface_handle surface,
                               gpu_render_sample_mode sample_mode,
                               gpu_render_texture *out_texture);
int gpu_render_surface_release(gpu_render_surface_handle surface);
int gpu_render_register_backend(const gpu_render_backend_ops *ops);
void gpu_render_unregister_backend(void);
const gpu_render_backend_info *gpu_render_backend(void);
void gpu_render_query_status(gpu_render_status *out_status);

/* Validates the driver-neutral primitive before dispatch.  A malformed
 * kernel request is negative; a valid request without a usable accelerator
 * returns GPU_RENDER_DISPATCH_CPU_FALLBACK. */
int gpu_render_submit(const gpu_render_submission *submission,
                      uint64_t *out_fence);
