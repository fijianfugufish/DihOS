#pragma once
#include <stdint.h>
#include "bootinfo.h"
#include "kwrappers/kui_types.h"
#include "kwrappers/colors.h"
#include "gpu/gpu_render.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* framebuffer + immediate API (unchanged) */
    typedef struct
    {
        volatile uint8_t *base;
        uint32_t width, height, pitch, fmt;
        uint32_t rmask, gmask, bmask;
    } kfb;

    int kgfx_init(const boot_info *bi);
    const kfb *kgfx_info(void);
    void kgfx_fill(kcolor c);
    void kgfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, kcolor c);
    void kgfx_flush(void);
    /* Emergency path: draw/present a screen without involving the scene graph. */
    void kgfx_panic_begin(kcolor background);
    void kgfx_panic_present(void);

    /* =================== Scene graph: multiple shapes =================== */

    typedef enum
    {
        KGFX_OBJ_RECT = 1,
        KGFX_OBJ_CIRCLE = 2,
        KGFX_OBJ_TEXT = 3,
        KGFX_OBJ_IMAGE = 4,
    } kgfx_obj_kind;

    typedef struct
    {
        int32_t x, y;
        uint32_t w, h;
    } kgfx_rect_data;

    typedef struct
    {
        int32_t cx, cy;
        uint32_t r;
    } kgfx_circle_data;

    typedef struct
    {
        const struct kfont *font;
        const char *text;
        int32_t x;
        int32_t y;
        uint32_t scale;
        int32_t char_spacing;
        int32_t line_spacing;
        ktext_align align;
    } kgfx_text_data;

    typedef struct kgfx_image_data
    {
        int32_t x;
        int32_t y;
        uint32_t w;
        uint32_t h;
        uint32_t src_w;
        uint32_t src_h;
        const uint32_t *argb;
        uint32_t stride_px;
        uint32_t content_version;
        uint8_t sample_mode;
        uint8_t opaque_hint;
    } kgfx_image_data;

    typedef enum
    {
        KGFX_IMAGE_SAMPLE_NEAREST = 0,
        KGFX_IMAGE_SAMPLE_BILINEAR = 1,
    } kgfx_image_sample_mode;

    typedef struct
    {
        kgfx_obj_kind kind;
        int32_t z;
        uint8_t visible;
        int16_t parent_idx;
        uint16_t parent_generation;
        uint8_t clip_to_parent;

        kcolor fill;
        uint8_t alpha;

        uint16_t outline_width;
        kcolor outline;
        uint8_t outline_alpha;
        int32_t rotation_deg;
        int32_t pivot_x;
        int32_t pivot_y;
        uint8_t pivot_set;

        union
        {
            kgfx_rect_data rect;
            kgfx_circle_data circle;
            kgfx_text_data text;
            kgfx_image_data image;
        } u;
    } kgfx_obj;

    typedef struct
    {
        int idx;
        uint16_t generation;
    } kgfx_obj_handle;

    int kgfx_scene_init(void);
    /* Prepare the neutral surface before a backend attaches its translation
     * domain, then enable GPU presentation only after backend admission. */
    int kgfx_gpu_backbuffer_prepare(void);
    int kgfx_gpu_backbuffer_enable(void);
    void kgfx_gpu_backbuffer_disable(void);
    int kgfx_gpu_backbuffer_attach(void);
    int kgfx_gpu_backbuffer_texture(gpu_render_texture *out_texture);
    uint8_t kgfx_gpu_frame_dirty(void);
    int kgfx_gpu_frame_damage(gpu_render_rect *out_damage);
    void kgfx_gpu_frame_submitted(void);
    void kgfx_present_backbuffer_cpu(void);

#define KGFX_MAX_COMPOSITION_LAYERS (GPU_RENDER_MAX_BATCH_DRAWS - 1u)
    typedef struct kgfx_composition_layer
    {
        gpu_render_texture texture;
        gpu_render_rect source_rect;
        gpu_render_rect destination_rect;
        int32_t z;
        uint16_t root_idx;
    } kgfx_composition_layer;

    /* The window manager supplies opaque top-level roots. KGFX retains each
     * subtree in its own neutral surface, while motion changes only the
     * destination rectangle. Surface pixels are mutated only when the GPU
     * has released the previous batch. */
    void kgfx_window_layers_prepare(const kgfx_obj_handle *roots,
                                    uint32_t count,
                                    uint8_t surface_updates_allowed);
    uint32_t kgfx_window_layers_snapshot(kgfx_composition_layer *out_layers,
                                         uint32_t capacity);
    void kgfx_window_layers_disable(void);
#define KGFX_MAX_GPU_FOREGROUND_ROOTS 4u
    void kgfx_gpu_foreground_set(const kgfx_obj_handle *roots,
                                 uint32_t count);
    void kgfx_gpu_overlay_set(kgfx_obj_handle object);
    void kgfx_gpu_overlay_present_cpu(const gpu_render_target *target,
                                      const gpu_render_rect *damage);

    kgfx_obj_handle kgfx_obj_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                      int32_t z, kcolor fill, uint8_t visible);
    kgfx_obj_handle kgfx_obj_add_circle(int32_t cx, int32_t cy, uint32_t r,
                                        int32_t z, kcolor fill, uint8_t visible);
    kgfx_obj_handle kgfx_obj_add_text(const struct kfont *font, const char *text,
                                      int32_t x, int32_t y, int32_t z,
                                      kcolor color, uint8_t alpha,
                                      uint32_t scale,
                                      int32_t char_spacing, int32_t line_spacing,
                                      ktext_align align, uint8_t visible);
    kgfx_obj_handle kgfx_obj_add_image(const uint32_t *argb,
                                       uint32_t w, uint32_t h,
                                       int32_t x, int32_t y,
                                       uint32_t stride_px);

    kgfx_obj *kgfx_obj_ref(kgfx_obj_handle h);

    static inline void kgfx_obj_set_fill(kgfx_obj_handle h, kcolor c)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->fill = c;
    }

    static inline void kgfx_obj_set_outline(kgfx_obj_handle h, uint32_t width, kcolor c)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
        {
            o->outline_width = (uint16_t)width;
            o->outline = c;
        }
    }

    static inline void kgfx_obj_set_alpha(kgfx_obj_handle h, uint8_t a)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->alpha = a;
    }

    static inline void kgfx_obj_set_outline_alpha(kgfx_obj_handle h, uint8_t a)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->outline_alpha = a;
    }

    static inline void kgfx_text_set(kgfx_obj_handle h, const char *text)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_TEXT)
            o->u.text.text = text;
    }

    static inline void kgfx_text_set_font(kgfx_obj_handle h, const struct kfont *f)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_TEXT)
            o->u.text.font = f;
    }

    static inline void kgfx_text_set_align(kgfx_obj_handle h, ktext_align a)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_TEXT)
            o->u.text.align = a;
    }

    static inline void kgfx_text_set_spacing(kgfx_obj_handle h, int cs, int ls)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_TEXT)
        {
            o->u.text.char_spacing = cs;
            o->u.text.line_spacing = ls;
        }
    }

    static inline void kgfx_text_set_scale(kgfx_obj_handle h, uint32_t s)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_TEXT)
            o->u.text.scale = s;
    }

    static inline void kgfx_image_set_size(kgfx_obj_handle h, uint32_t w, uint32_t h_px)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_IMAGE)
        {
            o->u.image.w = w;
            o->u.image.h = h_px;
        }
    }

    static inline void kgfx_image_set_scale(kgfx_obj_handle h, uint32_t scale_pct)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_IMAGE && scale_pct > 0)
        {
            o->u.image.w = (o->u.image.src_w * scale_pct) / 100u;
            o->u.image.h = (o->u.image.src_h * scale_pct) / 100u;
            if (o->u.image.w == 0)
                o->u.image.w = 1;
            if (o->u.image.h == 0)
                o->u.image.h = 1;
        }
    }

    static inline void kgfx_image_set_sample_mode(kgfx_obj_handle h, kgfx_image_sample_mode mode)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_IMAGE)
            o->u.image.sample_mode = (uint8_t)mode;
    }

    static inline void kgfx_image_touch(kgfx_obj_handle h)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_IMAGE)
            ++o->u.image.content_version;
    }

    static inline void kgfx_image_set_opaque_hint(kgfx_obj_handle h, uint8_t opaque)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o && o->kind == KGFX_OBJ_IMAGE)
            o->u.image.opaque_hint = opaque ? 1u : 0u;
    }

    static inline void kgfx_obj_set_parent(kgfx_obj_handle child, kgfx_obj_handle parent)
    {
        kgfx_obj *o = kgfx_obj_ref(child);
        if (o && kgfx_obj_ref(parent))
        {
            o->parent_idx = (int16_t)parent.idx;
            o->parent_generation = parent.generation;
        }
    }

    static inline void kgfx_obj_clear_parent(kgfx_obj_handle child)
    {
        kgfx_obj *o = kgfx_obj_ref(child);
        if (o)
        {
            o->parent_idx = -1;
            o->parent_generation = 0u;
        }
    }

    static inline void kgfx_obj_set_clip_to_parent(kgfx_obj_handle h, uint8_t enabled)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->clip_to_parent = enabled ? 1u : 0u;
    }

    static inline void kgfx_obj_set_rotation_deg(kgfx_obj_handle h, int32_t deg)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->rotation_deg = deg;
    }

    static inline void kgfx_obj_set_rotation_pivot(kgfx_obj_handle h, int32_t x, int32_t y)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
        {
            o->pivot_x = x;
            o->pivot_y = y;
            o->pivot_set = 1u;
        }
    }

    static inline void kgfx_obj_clear_rotation_pivot(kgfx_obj_handle h)
    {
        kgfx_obj *o = kgfx_obj_ref(h);
        if (o)
            o->pivot_set = 0u;
    }

    char *kgfx_pmem_strdup(const char *s);

    /* Back-compat rect wrappers (unchanged interface) */
    typedef struct
    {
        int idx;
        uint16_t generation;
    } kgfx_rect_handle;

    typedef struct
    {
        int32_t x, y;
        uint32_t w, h;
        int32_t z;
        kcolor color;
        uint8_t visible;
    } kgfx_rect_t;

    static inline kgfx_rect_handle kgfx_rect_add(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                                 int32_t z, kcolor color, uint8_t vis)
    {
        kgfx_obj_handle oh = kgfx_obj_add_rect(x, y, w, h, z, color, vis);
        kgfx_rect_handle rh;
        rh.idx = oh.idx;
        rh.generation = oh.generation;
        return rh;
    }

    static inline kgfx_rect_t *kgfx_rect_ref(kgfx_rect_handle h)
    {
        kgfx_obj_handle oh;
        oh.idx = h.idx;
        oh.generation = h.generation;

        kgfx_obj *o = kgfx_obj_ref(oh);
        if (!o || o->kind != KGFX_OBJ_RECT)
            return (kgfx_rect_t *)0;
        return (kgfx_rect_t *)&o->u.rect;
    }

    int kgfx_obj_destroy(kgfx_obj_handle h);

    void kgfx_put_px_blend(int x, int y, kcolor c, uint8_t a);
    int kgfx_target_argb_begin(uint32_t *argb, uint32_t w, uint32_t h, uint32_t stride_px);
    void kgfx_target_argb_end(void);
    int kgfx_capture_argb(uint32_t *dst, uint32_t dst_stride_px,
                          int32_t x, int32_t y, uint32_t w, uint32_t h);
    void kgfx_render_all(kcolor clear_color);
    uint64_t kgfx_rendered_frame_count(void);
    uint64_t kgfx_last_render_us(void);

#ifdef __cplusplus
}
#endif
