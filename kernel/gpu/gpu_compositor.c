#include "gpu/gpu_compositor.h"

#include "gpu/gpu_core.h"
#include "gpu/gpu_render_backend.h"
#include "gpu/qcom_dpu_present.h"
#include "apps/desktop_shell_api.h"
#include "apps/screenshot_service.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"
#include "process/dihos_process.h"
#include "terminal/terminal_api.h"

#define GPU_COMPOSITOR_SCANOUT_BUFFERS 2u

typedef struct gpu_compositor_state
{
    dihos_process_table processes;
    mesart_renderer_service service;
    mesart_renderer_graphics_pipeline solid;
    mesart_renderer_graphics_pipeline textured;
    gpu_render_target scanout_target;
    gpu_render_surface_handle composition_surface[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_render_target composition_target[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_render_texture composition_texture[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_render_texture frame_source;
    gpu_render_rect inflight_damage;
    gpu_render_rect pending_sync_damage;
    uint8_t ready;
    uint8_t frame_inflight;
    uint8_t frame_phase; /* 1=offscreen compose, 2=finished-frame publish */
    uint8_t first_frame_reported;
    uint8_t render_index;
    uint8_t inflight_target_index;
    uint8_t front_index;
    uint8_t display_owned;
    uint64_t submitted_counter;
    uint64_t vblank_wait_counter;
    uint64_t last_frame_us;
    uint64_t presented_frame_count;
} gpu_compositor_state;

static gpu_compositor_state G_compositor;

static uint64_t compositor_counter_now(void)
{
    uint64_t value;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

static uint64_t compositor_counter_frequency(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

int gpu_compositor_prepare_target(void)
{
    const kfb *fb = kgfx_info();
    gpu_render_surface_desc desc;

    if (G_compositor.composition_surface[0].generation &&
        G_compositor.composition_surface[1].generation)
        return 0;
    if (!fb || !fb->width || !fb->height)
        return -1;
    desc = (gpu_render_surface_desc){fb->width, fb->height,
                                     GPU_RENDER_FORMAT_BGRX8888};
    for (uint32_t i = 0u; i < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++i)
    {
        if (gpu_render_surface_create_scanout(
                &desc, &G_compositor.composition_surface[i]) != 0 ||
            gpu_render_surface_target(G_compositor.composition_surface[i],
                                      &G_compositor.composition_target[i]) != 0 ||
            gpu_render_surface_texture(G_compositor.composition_surface[i],
                                       GPU_RENDER_SAMPLE_NEAREST,
                                       &G_compositor.composition_texture[i]) != 0)
        {
            for (uint32_t j = 0u; j < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++j)
            {
                if (G_compositor.composition_surface[j].generation)
                    (void)gpu_render_surface_release(
                        G_compositor.composition_surface[j]);
                G_compositor.composition_surface[j] =
                    GPU_RENDER_EXTERNAL_SURFACE;
                G_compositor.composition_target[j] = (gpu_render_target){0};
                G_compositor.composition_texture[j] = (gpu_render_texture){0};
            }
            return -2;
        }
        /* These surfaces become textures only after a completed GPU render.
         * The small CPU-drawn foreground regions are explicitly cleaned by
         * KGFX, so per-frame full/rectangular cache walks are redundant. */
        G_compositor.composition_texture[i].device_coherent = 1u;
    }
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
                                             &submission->source_rect,
                                             &submission->destination_rect,
                                             3u, 0u,
                                             out_fence);
}

static int gpu_compositor_backend_submit_batch(
    const gpu_render_submission *submissions, uint32_t count,
    uint64_t *out_fence)
{
    gpu_core_compositor_layer layers[GPU_RENDER_MAX_BATCH_DRAWS];
    const mesart_renderer_graphics_pipeline *pipeline;

    if (!submissions || !count || count > GPU_RENDER_MAX_BATCH_DRAWS ||
        !out_fence)
        return GPU_RENDER_DISPATCH_CPU_FALLBACK;
    pipeline = gpu_compositor_core_pipeline(submissions[0].pipeline);
    if (!pipeline)
        return GPU_RENDER_DISPATCH_CPU_FALLBACK;
    for (uint32_t i = 0u; i < count; ++i)
    {
        if (submissions[i].pipeline != GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED ||
            submissions[i].primitive != GPU_RENDER_PRIMITIVE_TRIANGLE_LIST ||
            submissions[i].texture_count != 1u ||
            submissions[i].vertex_count != 6u)
            return GPU_RENDER_DISPATCH_CPU_FALLBACK;
        layers[i] = (gpu_core_compositor_layer){
            &submissions[i].textures[0],
            submissions[i].source_rect,
            submissions[i].destination_rect,
        };
    }
    return gpu_core_compositor_present_batch_kick(
        pipeline, layers, count, &submissions[0].target, out_fence);
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
    for (uint32_t i = 0u; i < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++i)
        if (G_compositor.composition_surface[i].generation)
            (void)gpu_render_surface_release(G_compositor.composition_surface[i]);
    G_compositor = (gpu_compositor_state){0};
}

static void gpu_compositor_disable(void)
{
    gpu_render_unregister_backend();
    kgfx_window_layers_disable();
    kgfx_gpu_backbuffer_disable();
    G_compositor.ready = 0u;
    G_compositor.frame_inflight = 0u;
    G_compositor.frame_phase = 0u;
    G_compositor.inflight_damage = (gpu_render_rect){0};
    G_compositor.pending_sync_damage = (gpu_render_rect){0};
    kgfx_present_backbuffer_cpu();
    qcom_dpu_present_restore_gop();
}

static int gpu_compositor_submit_frame(void)
{
    gpu_render_submission submissions[GPU_RENDER_MAX_BATCH_DRAWS];
    gpu_render_texture textures[GPU_RENDER_MAX_BATCH_DRAWS];
    kgfx_composition_layer window_layers[KGFX_MAX_COMPOSITION_LAYERS];
    uint64_t fence = 0u;
    int rc;
    gpu_render_rect damage;
    uint32_t submission_count = 0u;
    uint32_t window_count;

    if (!G_compositor.frame_source.cpu_pixels ||
        G_compositor.frame_source.width != G_compositor.scanout_target.width ||
        G_compositor.frame_source.height != G_compositor.scanout_target.height)
        return -1;
    if (kgfx_gpu_frame_damage(&damage) != 0)
        return -1;
    /* The first physical flip must start with a complete desktop.  Once DPU
     * ownership is established the two scanout surfaces are kept coherent
     * incrementally: copy the rectangle which changed on the preceding
     * frame from the displayed surface into this frame's back surface, then
     * render only the new KGFX damage below. */
    if (qcom_dpu_present_takeover_ready() && !G_compositor.display_owned)
        damage = (gpu_render_rect){0u, 0u,
            G_compositor.scanout_target.width,
            G_compositor.scanout_target.height};

    if (G_compositor.display_owned &&
        G_compositor.pending_sync_damage.width &&
        G_compositor.pending_sync_damage.height)
    {
        uint32_t slot = submission_count;
        textures[slot] = G_compositor.composition_texture[
            G_compositor.front_index];
        submissions[submission_count++] = (gpu_render_submission){
            .pipeline = GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED,
            .primitive = GPU_RENDER_PRIMITIVE_TRIANGLE_LIST,
            .target = G_compositor.composition_target[G_compositor.render_index],
            .source_rect = G_compositor.pending_sync_damage,
            .destination_rect = G_compositor.pending_sync_damage,
            .vertex_count = 6u,
            .textures = &textures[slot],
            .texture_count = 1u,
        };
    }

    {
        uint32_t slot = submission_count;
        textures[slot] = G_compositor.frame_source;
        submissions[submission_count++] = (gpu_render_submission){
            .pipeline = GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED,
            .primitive = GPU_RENDER_PRIMITIVE_TRIANGLE_LIST,
            .target = G_compositor.composition_target[G_compositor.render_index],
            .source_rect = damage,
            .destination_rect = damage,
            .vertex_count = 6u,
            .textures = &textures[slot],
            .texture_count = 1u,
        };
    }
    window_count = kgfx_window_layers_snapshot(window_layers,
                                                KGFX_MAX_COMPOSITION_LAYERS);
    for (uint32_t i = 0u; i < window_count &&
         submission_count < GPU_RENDER_MAX_BATCH_DRAWS; ++i)
    {
        gpu_render_rect dst = window_layers[i].destination_rect;
        gpu_render_rect src = window_layers[i].source_rect;
        uint32_t x0 = dst.x > damage.x ? dst.x : damage.x;
        uint32_t y0 = dst.y > damage.y ? dst.y : damage.y;
        uint32_t dst_x1 = dst.x + dst.width;
        uint32_t dst_y1 = dst.y + dst.height;
        uint32_t damage_x1 = damage.x + damage.width;
        uint32_t damage_y1 = damage.y + damage.height;
        uint32_t x1 = dst_x1 < damage_x1 ? dst_x1 : damage_x1;
        uint32_t y1 = dst_y1 < damage_y1 ? dst_y1 : damage_y1;
        uint32_t slot = submission_count;
        if (x0 >= x1 || y0 >= y1)
            continue;
        src.x += x0 - dst.x;
        src.y += y0 - dst.y;
        src.width = x1 - x0;
        src.height = y1 - y0;
        dst = (gpu_render_rect){x0, y0, x1 - x0, y1 - y0};
        textures[slot] = window_layers[i].texture;
        submissions[submission_count++] = (gpu_render_submission){
            .pipeline = GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED,
            .primitive = GPU_RENDER_PRIMITIVE_TRIANGLE_LIST,
            .target = G_compositor.composition_target[G_compositor.render_index],
            .source_rect = src,
            .destination_rect = dst,
            .vertex_count = 6u,
            .textures = &textures[slot],
            .texture_count = 1u,
        };
    }
    rc = gpu_render_submit_batch(submissions, submission_count, &fence);
    if (rc == GPU_RENDER_DISPATCH_SUBMITTED)
    {
        G_compositor.inflight_damage = damage;
        G_compositor.inflight_target_index = G_compositor.render_index;
        return 0;
    }
    if (rc == GPU_RENDER_DISPATCH_CPU_FALLBACK)
        return -2;
    return rc < 0 ? rc : -3;
}

static int gpu_compositor_publish_frame(void)
{
    gpu_render_submission submission;
    uint64_t fence = 0u;
    int rc;

    if (!G_compositor.inflight_damage.width ||
        !G_compositor.inflight_damage.height)
        return -1;
    submission = (gpu_render_submission){
        .pipeline = GPU_RENDER_PIPELINE_COMPOSITOR_TEXTURED,
        .primitive = GPU_RENDER_PRIMITIVE_TRIANGLE_LIST,
        .target = G_compositor.scanout_target,
        .source_rect = G_compositor.inflight_damage,
        .destination_rect = G_compositor.inflight_damage,
        .vertex_count = 6u,
        .textures = &G_compositor.composition_texture[
            G_compositor.inflight_target_index],
        .texture_count = 1u,
    };
    rc = gpu_render_submit(&submission, &fence);
    if (rc == GPU_RENDER_DISPATCH_SUBMITTED)
        return 0;
    if (rc == GPU_RENDER_DISPATCH_CPU_FALLBACK)
        return -2;
    return rc < 0 ? rc : -3;
}

int gpu_compositor_init(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || \
    defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    const gpu_scanout_target *scanout;
    gpu_render_surface_handle composition_surface[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_render_target composition_target[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_render_texture composition_texture[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    gpu_buffer *composition_backing[GPU_COMPOSITOR_SCANOUT_BUFFERS];
    int rc;

    if (G_compositor.ready)
        return 0;
    for (uint32_t i = 0u; i < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++i)
    {
        composition_surface[i] = G_compositor.composition_surface[i];
        composition_target[i] = G_compositor.composition_target[i];
        composition_texture[i] = G_compositor.composition_texture[i];
        composition_backing[i] = 0;
    }
    G_compositor = (gpu_compositor_state){0};
    for (uint32_t i = 0u; i < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++i)
    {
        G_compositor.composition_surface[i] = composition_surface[i];
        G_compositor.composition_target[i] = composition_target[i];
        G_compositor.composition_texture[i] = composition_texture[i];
    }
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
    for (uint32_t i = 0u; i < GPU_COMPOSITOR_SCANOUT_BUFFERS; ++i)
    {
        if (!G_compositor.composition_surface[i].generation ||
            G_compositor.composition_target[i].width != scanout->width ||
            G_compositor.composition_target[i].height != scanout->height ||
            gpu_render_backend_surface_backing(
                &G_compositor.composition_target[i],
                &composition_backing[i]) != 0 ||
            !composition_backing[i] ||
            composition_backing[i]->phys > UINT32_MAX)
            return -8;
    }
    (void)qcom_dpu_present_init(
        scanout->width, scanout->height, scanout->buffer.phys,
        scanout->pitch, composition_backing[0]->phys);
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
                .name = "adreno-x1-85-freedreno-ir3-atomic-compositor",
                .abi_version = GPU_RENDER_ABI_VERSION,
                .capabilities = GPU_RENDER_CAP_COLOR_TARGET |
                                GPU_RENDER_CAP_AUTO_TRIANGLES |
                                GPU_RENDER_CAP_RECTANGLE_LIST |
                                GPU_RENDER_CAP_UNIFORM_VEC4 |
                                GPU_RENDER_CAP_TEXTURE_2D |
                                GPU_RENDER_CAP_BATCHED_LAYERS,
            },
            .submit = gpu_compositor_backend_submit,
            .submit_batch = gpu_compositor_backend_submit_batch,
        }) == 0)
        terminal_success("GPU compositor: asynchronous offscreen/publish shader path enabled");
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

void gpu_compositor_prepare_scene(void)
{
    kgfx_obj_handle roots[KGFX_MAX_COMPOSITION_LAYERS];
    kgfx_obj_handle foreground[KGFX_MAX_GPU_FOREGROUND_ROOTS];
    uint32_t count;
    uint32_t foreground_count = 0u;

    if (!G_compositor.ready)
    {
        kgfx_window_layers_disable();
        return;
    }
    count = kwindow_collect_visible_roots(roots, KGFX_MAX_COMPOSITION_LAYERS);
    foreground_count += desktop_shell_collect_gpu_foreground_roots(
        foreground + foreground_count,
        KGFX_MAX_GPU_FOREGROUND_ROOTS - foreground_count);
    foreground_count += screenshot_service_collect_gpu_foreground_roots(
        foreground + foreground_count,
        KGFX_MAX_GPU_FOREGROUND_ROOTS - foreground_count);
    kgfx_gpu_foreground_set(foreground, foreground_count);
    kgfx_gpu_overlay_set(kmouse_cursor_object());
    kgfx_window_layers_prepare(roots, count,
        (!G_compositor.frame_inflight && !gpu_core_mesart_3d_busy()) ? 1u : 0u);
}

static void gpu_compositor_complete_frame(void)
{
    uint64_t frequency = compositor_counter_frequency();
    uint64_t elapsed = compositor_counter_now() - G_compositor.submitted_counter;

    if (frequency)
        G_compositor.last_frame_us = (elapsed * 1000000u) / frequency;
    G_compositor.frame_inflight = 0u;
    G_compositor.frame_phase = 0u;
    G_compositor.inflight_damage = (gpu_render_rect){0};
    ++G_compositor.presented_frame_count;
    if (!G_compositor.first_frame_reported)
    {
        terminal_success(G_compositor.display_owned
            ? "GPU compositor: first DPU-owned scanout frame flipped"
            : "GPU compositor: first complete offscreen frame published");
        terminal_flush_log();
        G_compositor.first_frame_reported = 1u;
    }
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
        if (G_compositor.frame_phase == 1u)
        {
            /* The complete window stack now exists away from scanout. Blend
             * translucent shell foreground and the cursor into that stable
             * image, then publish it with one GPU rectangle instead of many
             * independently visible draws. */
            gpu_buffer *backing = 0;
            gpu_render_target *finished =
                &G_compositor.composition_target[
                    G_compositor.inflight_target_index];

            kgfx_gpu_overlay_present_cpu(finished,
                                         &G_compositor.inflight_damage);
            if (qcom_dpu_present_takeover_ready() &&
                gpu_render_backend_surface_backing(finished, &backing) == 0 &&
                backing && qcom_dpu_present_flip(backing->phys) == 0)
            {
                G_compositor.display_owned = 1u;
                G_compositor.front_index =
                    G_compositor.inflight_target_index;
                G_compositor.render_index =
                    (uint8_t)(G_compositor.front_index ^ 1u);
                G_compositor.pending_sync_damage =
                    G_compositor.inflight_damage;
                gpu_compositor_complete_frame();
                return;
            }
            rc = gpu_compositor_publish_frame();
            if (rc == 0)
            {
                G_compositor.frame_phase = 2u;
                return;
            }
            gpu_compositor_disable();
            terminal_error("GPU compositor finished-frame publish declined; CPU presentation restored");
            terminal_flush_log();
            return;
        }
        if (G_compositor.frame_phase != 2u)
        {
            gpu_compositor_disable();
            terminal_error("GPU compositor completed an unknown frame phase");
            terminal_flush_log();
            return;
        }
        gpu_compositor_complete_frame();
        return;
    }
    if (rc < 0)
    {
        gpu_compositor_disable();
        terminal_error("GPU compositor offscreen/publish failed; KGFX CPU presentation restored");
        terminal_flush_log();
    }
}

uint8_t gpu_compositor_frame_busy(void)
{
    /* KGFX owns two coherent retained backbuffers. CP samples one while the
     * CPU builds the next frame in the other, so compositor activity no
     * longer blocks scene updates. Other Mesart clients still advertise
     * their own busy state separately. */
    return G_compositor.ready ? 0u : gpu_core_mesart_3d_busy();
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
        G_compositor.frame_phase = 1u;
        G_compositor.submitted_counter = compositor_counter_now();
        kgfx_gpu_frame_submitted();
        return;
    }
    gpu_compositor_disable();
    terminal_error("GPU compositor declined offscreen frame; KGFX CPU presentation restored rc=");
    terminal_print_inline_hex64((uint64_t)(uint32_t)(-rc));
    terminal_flush_log();
}

uint64_t gpu_compositor_last_frame_us(void)
{
    return G_compositor.last_frame_us;
}

uint64_t gpu_compositor_presented_frame_count(void)
{
    return G_compositor.presented_frame_count;
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
