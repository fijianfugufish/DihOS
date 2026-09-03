#include "gpu/gpu_render.h"
#include "gpu/gpu_render_backend.h"

static gpu_render_backend_ops g_backend;
static uint8_t g_backend_registered;

typedef struct gpu_render_surface_slot
{
    gpu_buffer storage;
    gpu_render_surface_desc desc;
    uint16_t generation;
    uint8_t active;
} gpu_render_surface_slot;

static gpu_render_surface_slot g_surfaces[GPU_RENDER_MAX_SURFACES];

static int surface_index(gpu_render_surface_handle surface,
                         uint32_t *out_index)
{
    gpu_render_surface_slot *slot;

    if (!out_index || !surface.generation ||
        surface.slot >= GPU_RENDER_MAX_SURFACES)
        return -1;
    slot = &g_surfaces[surface.slot];
    if (!slot->active || slot->generation != surface.generation)
        return -2;
    *out_index = surface.slot;
    return 0;
}

static int format_valid(gpu_render_pixel_format format)
{
    return format == GPU_RENDER_FORMAT_ARGB8888 ||
           format == GPU_RENDER_FORMAT_XRGB8888 ||
           format == GPU_RENDER_FORMAT_BGRX8888;
}

static int target_valid(const gpu_render_target *target)
{
    uint64_t minimum_stride;
    uint64_t minimum_bytes;

    if (!target || !target->cpu_pixels || !target->width || !target->height ||
        !format_valid(target->format))
        return 0;
    minimum_stride = (uint64_t)target->width * 4u;
    if (minimum_stride > UINT32_MAX || target->stride_bytes < minimum_stride)
        return 0;
    minimum_bytes = (uint64_t)target->stride_bytes * target->height;
    return minimum_bytes >= target->stride_bytes &&
           target->cpu_bytes >= minimum_bytes;
}

static int texture_valid(const gpu_render_texture *texture)
{
    uint64_t minimum_stride;
    uint64_t minimum_bytes;

    if (!texture || !texture->cpu_pixels || !texture->width || !texture->height ||
        !format_valid(texture->format) ||
        (texture->sample_mode != GPU_RENDER_SAMPLE_NEAREST &&
         texture->sample_mode != GPU_RENDER_SAMPLE_BILINEAR))
        return 0;
    minimum_stride = (uint64_t)texture->width * 4u;
    if (minimum_stride > UINT32_MAX || texture->stride_bytes < minimum_stride)
        return 0;
    minimum_bytes = (uint64_t)texture->stride_bytes * texture->height;
    if (minimum_bytes < texture->stride_bytes ||
        texture->cpu_bytes < minimum_bytes)
        return 0;
    if (texture->surface.generation)
    {
        uint32_t index;
        const gpu_render_surface_slot *slot;

        if (surface_index(texture->surface, &index) != 0)
            return 0;
        slot = &g_surfaces[index];
        if (slot->storage.cpu != texture->cpu_pixels ||
            slot->storage.size_bytes != texture->cpu_bytes ||
            slot->desc.width != texture->width ||
            slot->desc.height != texture->height ||
            slot->desc.format != texture->format ||
            texture->stride_bytes != texture->width * 4u)
            return 0;
    }
    return 1;
}

static int submission_valid(const gpu_render_submission *submission)
{
    if (!submission || !target_valid(&submission->target) ||
        (submission->pipeline != GPU_RENDER_PIPELINE_COMPOSITOR_SOLID &&
         submission->pipeline != GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED) ||
        submission->vertex_count != 3u ||
        submission->vertex_uniform_count > GPU_RENDER_MAX_UNIFORM_VEC4S ||
        submission->fragment_uniform_count > GPU_RENDER_MAX_UNIFORM_VEC4S ||
        submission->texture_count > GPU_RENDER_MAX_TEXTURES ||
        (submission->vertex_uniform_count && !submission->vertex_uniform_vec4s) ||
        (submission->fragment_uniform_count && !submission->fragment_uniform_vec4s) ||
        (submission->texture_count && !submission->textures))
        return 0;
    if (submission->pipeline == GPU_RENDER_PIPELINE_COMPOSITOR_SOLID &&
        submission->texture_count)
        return 0;
    if (submission->pipeline == GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED &&
        submission->texture_count != 1u)
        return 0;
    for (uint32_t i = 0u; i < submission->texture_count; ++i)
        if (!texture_valid(&submission->textures[i]))
            return 0;
    return 1;
}

void gpu_render_init(void)
{
    for (uint32_t i = 0u; i < GPU_RENDER_MAX_SURFACES; ++i)
        gpu_buffer_release(&g_surfaces[i].storage);
    for (uint32_t i = 0u; i < GPU_RENDER_MAX_SURFACES; ++i)
        g_surfaces[i] = (gpu_render_surface_slot){0};
    g_backend = (gpu_render_backend_ops){0};
    g_backend_registered = 0u;
}

int gpu_render_surface_create(const gpu_render_surface_desc *desc,
                              gpu_render_surface_handle *out_surface)
{
    uint64_t stride;
    uint64_t bytes;

    if (!desc || !out_surface || !desc->width || !desc->height ||
        !format_valid(desc->format))
        return -1;
    stride = (uint64_t)desc->width * 4u;
    bytes = stride * desc->height;
    if (stride > UINT32_MAX || !bytes || bytes < stride ||
        bytes > GPU_RENDER_MAX_SURFACE_BYTES)
        return -2;
    for (uint32_t i = 0u; i < GPU_RENDER_MAX_SURFACES; ++i)
    {
        gpu_render_surface_slot *slot = &g_surfaces[i];

        if (slot->active)
            continue;
        if (gpu_buffer_alloc(&slot->storage, bytes,
                             GPU_BUFFER_DATA | GPU_BUFFER_ZEROED) != 0)
            return -3;
        slot->generation = (uint16_t)(slot->generation + 1u);
        if (!slot->generation)
            slot->generation = 1u;
        slot->desc = *desc;
        slot->active = 1u;
        *out_surface = (gpu_render_surface_handle){(uint16_t)i,
                                                    slot->generation};
        return 0;
    }
    return -4;
}

int gpu_render_surface_target(gpu_render_surface_handle surface,
                              gpu_render_target *out_target)
{
    uint32_t index;
    gpu_render_surface_slot *slot;

    if (!out_target || surface_index(surface, &index) != 0)
        return -1;
    slot = &g_surfaces[index];
    *out_target = (gpu_render_target){
        .surface = surface,
        .cpu_pixels = slot->storage.cpu,
        .cpu_bytes = slot->storage.size_bytes,
        .width = slot->desc.width,
        .height = slot->desc.height,
        .stride_bytes = slot->desc.width * 4u,
        .format = slot->desc.format,
    };
    return 0;
}

int gpu_render_surface_texture(gpu_render_surface_handle surface,
                               gpu_render_sample_mode sample_mode,
                               gpu_render_texture *out_texture)
{
    uint32_t index;
    gpu_render_surface_slot *slot;

    if (!out_texture ||
        (sample_mode != GPU_RENDER_SAMPLE_NEAREST &&
         sample_mode != GPU_RENDER_SAMPLE_BILINEAR) ||
        surface_index(surface, &index) != 0)
        return -1;
    slot = &g_surfaces[index];
    *out_texture = (gpu_render_texture){
        .surface = surface,
        .cpu_pixels = slot->storage.cpu,
        .cpu_bytes = slot->storage.size_bytes,
        .width = slot->desc.width,
        .height = slot->desc.height,
        .stride_bytes = slot->desc.width * 4u,
        .format = slot->desc.format,
        .sample_mode = sample_mode,
    };
    return 0;
}

int gpu_render_surface_release(gpu_render_surface_handle surface)
{
    uint32_t index;

    if (surface_index(surface, &index) != 0)
        return -1;
    gpu_buffer_release(&g_surfaces[index].storage);
    g_surfaces[index].desc = (gpu_render_surface_desc){0};
    g_surfaces[index].active = 0u;
    return 0;
}

int gpu_render_backend_surface_backing(const gpu_render_target *target,
                                       gpu_buffer **out_buffer)
{
    uint32_t index;

    if (!target || !out_buffer)
        return -1;
    *out_buffer = 0;
    if (!target->surface.generation)
        return 1;
    if (surface_index(target->surface, &index) != 0)
        return -2;
    if (g_surfaces[index].storage.cpu != target->cpu_pixels ||
        g_surfaces[index].storage.size_bytes != target->cpu_bytes ||
        g_surfaces[index].desc.width != target->width ||
        g_surfaces[index].desc.height != target->height ||
        g_surfaces[index].desc.format != target->format ||
        target->stride_bytes != target->width * 4u)
        return -3;
    *out_buffer = &g_surfaces[index].storage;
    return 0;
}

int gpu_render_backend_texture_backing(const gpu_render_texture *texture,
                                       gpu_buffer **out_buffer)
{
    uint32_t index;

    if (!texture || !out_buffer)
        return -1;
    *out_buffer = 0;
    if (!texture->surface.generation)
        return 1;
    if (!texture_valid(texture) || surface_index(texture->surface, &index) != 0)
        return -2;
    *out_buffer = &g_surfaces[index].storage;
    return 0;
}

int gpu_render_register_backend(const gpu_render_backend_ops *ops)
{
    if (!ops || !ops->info.name || !ops->info.name[0] ||
        ops->info.abi_version != GPU_RENDER_ABI_VERSION || !ops->submit ||
        g_backend_registered)
        return -1;
    g_backend = *ops;
    g_backend_registered = 1u;
    return 0;
}

void gpu_render_unregister_backend(void)
{
    g_backend = (gpu_render_backend_ops){0};
    g_backend_registered = 0u;
}

const gpu_render_backend_info *gpu_render_backend(void)
{
    return g_backend_registered ? &g_backend.info : 0;
}

void gpu_render_query_status(gpu_render_status *out_status)
{
    if (!out_status)
        return;
    *out_status = (gpu_render_status){
        GPU_RENDER_ABI_VERSION,
        g_backend_registered ? g_backend.info.capabilities : 0u,
        g_backend_registered,
    };
}

int gpu_render_submit(const gpu_render_submission *submission,
                      uint64_t *out_fence)
{
    uint32_t required = GPU_RENDER_CAP_COLOR_TARGET |
                        GPU_RENDER_CAP_AUTO_TRIANGLES |
                        GPU_RENDER_CAP_UNIFORM_VEC4;

    if (out_fence)
        *out_fence = 0u;
    if (!submission_valid(submission))
        return -1;
    if (submission->pipeline == GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED)
        required |= GPU_RENDER_CAP_TEXTURE_2D;
    if (!g_backend_registered ||
        (g_backend.info.capabilities & required) != required)
        return GPU_RENDER_DISPATCH_CPU_FALLBACK;
    return g_backend.submit(submission, out_fence);
}
