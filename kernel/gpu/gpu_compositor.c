#include "gpu/gpu_compositor.h"

#include "gpu/gpu_core.h"
#include "kwrappers/kgfx.h"
#include "process/dihos_process.h"
#include "terminal/terminal_api.h"

typedef struct gpu_compositor_state
{
    dihos_process_table processes;
    mesart_renderer_service service;
    mesart_renderer_graphics_pipeline solid;
    mesart_renderer_graphics_pipeline textured;
    gpu_render_target scanout_target;
    gpu_render_texture frame_source;
    uint8_t ready;
    uint8_t frame_inflight;
    uint8_t first_frame_reported;
} gpu_compositor_state;

static gpu_compositor_state G_compositor;

int gpu_compositor_prepare_target(void)
{
    /* The source KGFX surface is reserved separately before SMMU attach and
     * the UEFI framebuffer is imported by gpu_core_init().  Direct scanout
     * therefore needs no intermediate compositor surfaces. */
    return 0;
}

static int gpu_compositor_backend_submit(
    const gpu_render_submission *submission, uint64_t *out_fence)
{
    const mesart_renderer_graphics_pipeline *pipeline;

    if (!submission || !out_fence ||
        submission->pipeline != GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED ||
        submission->primitive != GPU_RENDER_PRIMITIVE_TRIANGLE_LIST ||
        submission->texture_count != 1u || submission->vertex_count != 6u)
        return GPU_RENDER_DISPATCH_CPU_FALLBACK;
    pipeline = gpu_compositor_core_pipeline(submission->pipeline);
    if (!pipeline)
        return GPU_RENDER_DISPATCH_CPU_FALLBACK;
    return gpu_core_compositor_present_kick(pipeline,
                                             &submission->textures[0],
                                             &submission->target,
                                             3u, 0u,
                                             out_fence);
}

static void gpu_compositor_fail_release(void)
{
    if (G_compositor.service.process.generation)
    {
        dihos_process_handle process = G_compositor.service.process;
        (void)dihos_process_fault(&G_compositor.processes, process);
        mesart_renderer_release(&G_compositor.service);
        (void)dihos_process_reap(&G_compositor.processes, process);
    }
    G_compositor = (gpu_compositor_state){0};
}

static void gpu_compositor_disable(void)
{
    gpu_render_unregister_backend();
    kgfx_gpu_backbuffer_disable();
    G_compositor.ready = 0u;
    G_compositor.frame_inflight = 0u;
    kgfx_present_backbuffer_cpu();
}

static int gpu_compositor_submit_frame(void)
{
    gpu_render_submission submission;
    uint64_t fence = 0u;
    int rc;

    if (!G_compositor.frame_source.cpu_pixels ||
        G_compositor.frame_source.width != G_compositor.scanout_target.width ||
        G_compositor.frame_source.height != G_compositor.scanout_target.height)
        return -1;
    submission = (gpu_render_submission){
        .pipeline = GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED,
        .primitive = GPU_RENDER_PRIMITIVE_TRIANGLE_LIST,
        .target = G_compositor.scanout_target,
        .vertex_count = 6u,
        .textures = &G_compositor.frame_source,
        .texture_count = 1u,
    };
    rc = gpu_render_submit(&submission, &fence);
    return rc == GPU_RENDER_DISPATCH_SUBMITTED ? 0 : -2;
}

int gpu_compositor_init(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || \
    defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    const gpu_scanout_target *scanout;
    int rc;

    if (G_compositor.ready)
        return 0;
    G_compositor = (gpu_compositor_state){0};
    scanout = gpu_core_scanout_target();
    if (!scanout || !scanout->buffer.cpu || !scanout->buffer.iova ||
        !scanout->width || !scanout->height ||
        scanout->pitch < scanout->width * 4u)
        return -7;
    G_compositor.scanout_target = (gpu_render_target){
        .surface = GPU_RENDER_EXTERNAL_SURFACE,
        .cpu_pixels = scanout->buffer.cpu,
        .cpu_bytes = scanout->buffer.size_bytes,
        .width = scanout->width,
        .height = scanout->height,
        .stride_bytes = scanout->pitch,
        .format = GPU_RENDER_FORMAT_BGRX8888,
    };
    dihos_process_init(&G_compositor.processes);
    rc = mesart_renderer_admit(&G_compositor.processes,
                               "0:/OS/MesaRuntime", &G_compositor.service);
    if (rc != 0)
        goto failed;
    if (dihos_process_set_running(&G_compositor.processes,
                                  G_compositor.service.process) != 0)
    {
        rc = -2;
        goto failed;
    }
    if (mesart_renderer_prepare_named_graphics_pipeline(
            &G_compositor.service, "compositor_solid",
            &(mesart_renderer_graphics_request){0},
            &G_compositor.solid) != 0)
    {
        rc = -3;
        goto failed;
    }
    if (mesart_renderer_prepare_named_graphics_pipeline(
            &G_compositor.service, "compositor_textured",
            &(mesart_renderer_graphics_request){.fragment_texture_count = 1u},
            &G_compositor.textured) != 0)
    {
        rc = -4;
        goto failed;
    }
    if (gpu_core_mesart_3d_preflight(&G_compositor.solid,
                                     &(uint32_t){0}) != 0 ||
        gpu_core_mesart_3d_preflight(&G_compositor.textured,
                                     &(uint32_t){0}) != 0)
    {
        rc = -5;
        goto failed;
    }
    if (dihos_process_exit(&G_compositor.processes,
                           G_compositor.service.process) != 0)
    {
        rc = -6;
        goto failed;
    }
    G_compositor.ready = 1u;
    if (kgfx_gpu_backbuffer_enable() == 0 &&
        gpu_render_register_backend(&(gpu_render_backend_ops){
            .info = {
                .name = "adreno-x1-85-freedreno-ir3-direct-scanout",
                .abi_version = GPU_RENDER_ABI_VERSION,
                .capabilities = GPU_RENDER_CAP_COLOR_TARGET |
                                GPU_RENDER_CAP_AUTO_TRIANGLES |
                                GPU_RENDER_CAP_RECTANGLE_LIST |
                                GPU_RENDER_CAP_UNIFORM_VEC4 |
                                GPU_RENDER_CAP_TEXTURE_2D,
            },
            .submit = gpu_compositor_backend_submit,
        }) == 0)
        terminal_success("GPU compositor: asynchronous direct-scanout shader path enabled");
    else
    {
        kgfx_gpu_backbuffer_disable();
        G_compositor.ready = 0u;
        terminal_warn("GPU compositor: core ready but KGFX stayed on CPU fallback");
    }
    terminal_success("GPU compositor: signed pipelines admitted; CPU fallback retained");
    terminal_flush_log();
    return 0;

failed:
    terminal_warn("GPU compositor unavailable; retaining KGFX CPU renderer rc=");
    terminal_print_inline_hex64((uint64_t)(uint32_t)(-rc));
    terminal_flush_log();
    gpu_compositor_fail_release();
    return rc;
#else
    return -100;
#endif
}

void gpu_compositor_update(void)
{
    int rc;

    if (!G_compositor.frame_inflight)
    {
        (void)gpu_core_mesart_3d_poll(0);
        return;
    }
    rc = gpu_core_mesart_3d_poll(0);
    if (rc > 0)
        return;
    if (rc == 0)
    {
        G_compositor.frame_inflight = 0u;
        if (!G_compositor.first_frame_reported)
        {
            terminal_success("GPU compositor: first direct-scanout shader frame presented");
            terminal_flush_log();
            G_compositor.first_frame_reported = 1u;
        }
        return;
    }
    if (rc < 0)
    {
        gpu_compositor_disable();
        terminal_error("GPU compositor direct scanout failed; KGFX CPU presentation restored");
        terminal_flush_log();
    }
}

uint8_t gpu_compositor_frame_busy(void)
{
    return G_compositor.frame_inflight || gpu_core_mesart_3d_busy();
}

void gpu_compositor_submit_kgfx_frame(void)
{
    int rc;

    if (!G_compositor.ready || G_compositor.frame_inflight ||
        !kgfx_gpu_frame_dirty() ||
        kgfx_gpu_backbuffer_texture(&G_compositor.frame_source) != 0)
        return;
    rc = gpu_compositor_submit_frame();
    if (rc == 0)
    {
        G_compositor.frame_inflight = 1u;
        kgfx_gpu_frame_submitted();
        return;
    }
    gpu_compositor_disable();
    terminal_error("GPU compositor declined direct frame; KGFX CPU presentation restored rc=");
    terminal_print_inline_hex64((uint64_t)(uint32_t)(-rc));
    terminal_flush_log();
}

uint8_t gpu_compositor_core_ready(void)
{
    return G_compositor.ready;
}

const mesart_renderer_graphics_pipeline *gpu_compositor_core_pipeline(
    gpu_render_pipeline_kind kind)
{
    if (!G_compositor.ready)
        return 0;
    if (kind == GPU_RENDER_PIPELINE_COMPOSITOR_SOLID)
        return &G_compositor.solid;
    if (kind == GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED)
        return &G_compositor.textured;
    return 0;
}
