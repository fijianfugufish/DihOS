#include <stddef.h>
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "memory/pmem.h" // for pmem_alloc_pages

static kfb FB;

static uint32_t sR = 16, sG = 8, sB = 0; // defaults
static uint32_t mR = 0x00FF0000, mG = 0x0000FF00, mB = 0x000000FF;

static inline uint32_t ctz32(uint32_t x)
{
    uint32_t n = 0;
    if (!x)
        return 0;
    while (!(x & 1u))
    {
        x >>= 1;
        ++n;
    }
    return n;
}

int kgfx_init(const boot_info *bi)
{
    FB.base = (volatile uint8_t *)(uintptr_t)bi->fb.fb_base;
    FB.width = bi->fb.width;
    FB.height = bi->fb.height;
    FB.pitch = bi->fb.pitch;
    FB.fmt = bi->fb.pixel_format;
    FB.rmask = bi->fb.rmask;
    FB.gmask = bi->fb.gmask;
    FB.bmask = bi->fb.bmask;
    // derive shifts/masks from framebuffer info (fallback to 8-bit channels)
    mR = FB.rmask ? FB.rmask : 0x00FF0000;
    mG = FB.gmask ? FB.gmask : 0x0000FF00;
    mB = FB.bmask ? FB.bmask : 0x000000FF;
    sR = ctz32(mR);
    sG = ctz32(mG);
    sB = ctz32(mB);
    return FB.base ? 0 : -1;
}

static uint32_t kstrlen(const char *s)
{
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void kmemcpy64(void *dst, const void *src, uint32_t n)
{
    uint64_t *d64 = (uint64_t *)dst;
    const uint64_t *s64 = (const uint64_t *)src;

    uint32_t q = n >> 3;
    for (uint32_t i = 0; i < q; ++i)
        d64[i] = s64[i];

    uint8_t *d8 = (uint8_t *)(d64 + q);
    const uint8_t *s8 = (const uint8_t *)(s64 + q);
    for (uint32_t i = (q << 3); i < n; ++i)
        d8[i - (q << 3)] = s8[i - (q << 3)];
}

char *kgfx_pmem_strdup(const char *s)
{
    if (!s)
        return 0;
    uint32_t n = kstrlen(s);
    uint64_t bytes = (uint64_t)n + 1;
    uint64_t pages = (bytes + 4095) >> 12;
    char *p = (char *)pmem_alloc_pages(pages);
    if (!p)
        return 0;
    kmemcpy64(p, s, n);
    p[n] = '\0';
    return p;
}

static inline void kgfx_flush_range(uintptr_t start, uintptr_t end)
{
    const uintptr_t line = 64;
    start &= ~(line - 1);
    end = (end + line - 1) & ~(line - 1);
    for (uintptr_t p = start; p < end; p += line)
        __asm__ __volatile__("dc cvac, %0" ::"r"(p) : "memory");
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
}

void kgfx_flush(void)
{
    if (!FB.base)
        return;
    uintptr_t s = (uintptr_t)FB.base;
    uintptr_t e = s + (uintptr_t)FB.pitch * FB.height;
    kgfx_flush_range(s, e);
}

static uint32_t kgfx_pack(kcolor c)
{
    if (FB.fmt == 0)
        return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
    if (FB.fmt == 1)
        return (uint32_t)c.b | ((uint32_t)c.g << 8) | ((uint32_t)c.r << 16);
    uint32_t rs = ctz32(FB.rmask ? FB.rmask : 0x00FF0000);
    uint32_t gs = ctz32(FB.gmask ? FB.gmask : 0x0000FF00);
    uint32_t bs = ctz32(FB.bmask ? FB.bmask : 0x000000FF);
    return ((uint32_t)c.r << rs) | ((uint32_t)c.g << gs) | ((uint32_t)c.b << bs);
}

void kgfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, kcolor c)
{
    if (!FB.base || x >= FB.width || y >= FB.height)
        return;
    if (x + w > FB.width)
        w = FB.width - x;
    if (y + h > FB.height)
        h = FB.height - y;
    uint32_t px = kgfx_pack(c);
    for (uint32_t yy = 0; yy < h; ++yy)
    {
        volatile uint32_t *row = (volatile uint32_t *)(FB.base + (y + yy) * FB.pitch + x * 4);
        for (uint32_t xx = 0; xx < w; ++xx)
            row[xx] = px;
    }
}

void kgfx_fill(kcolor c) { kgfx_rect(0, 0, FB.width, FB.height, c); }
const kfb *kgfx_info(void) { return &FB; }

/* =================== scene + backbuffer =================== */

#ifndef KGFX_MAX_OBJS
#define KGFX_MAX_OBJS 512
#endif

/* object pool */
static kgfx_obj g_objs[KGFX_MAX_OBJS];
static uint16_t g_obj_count = 0;

typedef struct
{
    int32_t x0, y0, x1, y1;
    uint8_t enabled;
} kgfx_clip_rect;

typedef struct
{
    int32_t origin_x;
    int32_t origin_y;
    int32_t z;
    int32_t root_z;
    uint16_t root_idx;
    kgfx_clip_rect clip;
    uint8_t ready;
    uint8_t visiting;
} kgfx_resolved_obj;

typedef struct
{
    uint16_t idx;
    int32_t z;
} kgfx_render_entry;

/* backbuffer (you already have these) */
static uint8_t *BB_ptr = NULL;
static uint32_t BB_stride = 0;
static uint64_t BB_bytes = 0;
static kgfx_clip_rect g_bb_clip = {0, 0, 0, 0, 0};

typedef struct
{
    uint8_t in_use;
    uint8_t renderable;
    kgfx_clip_rect bounds;
    uint64_t visual_hash;
} kgfx_obj_snapshot;

static kgfx_obj_snapshot g_prev_snapshot[KGFX_MAX_OBJS];
static uint16_t g_prev_snapshot_count = 0;
static kcolor g_prev_clear_color = {0, 0, 0};
static uint8_t g_have_prev_frame = 0;

/* ---------- creation ---------- */

kgfx_obj_handle kgfx_obj_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t height,
                                  int32_t z, kcolor fill, uint8_t visible)
{
    kgfx_obj_handle handle = {.idx = -1};
    if (g_obj_count >= KGFX_MAX_OBJS)
        return handle;

    kgfx_obj *o = &g_objs[g_obj_count];
    o->kind = KGFX_OBJ_RECT;
    o->z = z;
    o->visible = visible ? 1 : 0;
    o->parent_idx = -1;
    o->clip_to_parent = 1;

    o->fill = fill; // NEW
    o->alpha = 255;
    o->outline_width = 0;           // NEW (default 0px)
    o->outline = (kcolor){0, 0, 0}; // NEW (default black, unused until width>0)
    o->outline_alpha = 255;

    o->u.rect.x = x;
    o->u.rect.y = y;
    o->u.rect.w = w;
    o->u.rect.h = height;
    handle.idx = (int)g_obj_count++;
    return handle;
}

kgfx_obj_handle kgfx_obj_add_circle(int32_t cx, int32_t cy, uint32_t r,
                                    int32_t z, kcolor fill, uint8_t visible)
{
    kgfx_obj_handle handle = {.idx = -1};
    if (g_obj_count >= KGFX_MAX_OBJS)
        return handle;

    kgfx_obj *o = &g_objs[g_obj_count];
    o->kind = KGFX_OBJ_CIRCLE;
    o->z = z;
    o->visible = visible ? 1 : 0;
    o->parent_idx = -1;
    o->clip_to_parent = 1;

    o->fill = fill; // NEW
    o->alpha = 255;
    o->outline_width = 0;           // NEW
    o->outline = (kcolor){0, 0, 0}; // NEW
    o->outline_alpha = 255;

    o->u.circle.cx = cx;
    o->u.circle.cy = cy;
    o->u.circle.r = r;
    handle.idx = (int)g_obj_count++;
    return handle;
}

kgfx_obj_handle kgfx_obj_add_text(const kfont *font, const char *text,
                                  int32_t x, int32_t y, int32_t z,
                                  kcolor color, uint8_t alpha,
                                  uint32_t scale,
                                  int32_t char_spacing, int32_t line_spacing,
                                  ktext_align align, uint8_t visible)
{
    kgfx_obj_handle h = {.idx = -1};
    if (g_obj_count >= KGFX_MAX_OBJS || !font || !text || scale == 0)
        return h;

    kgfx_obj *o = &g_objs[g_obj_count];
    o->kind = KGFX_OBJ_TEXT;
    o->z = z;
    o->visible = visible ? 1 : 0;
    o->parent_idx = -1;
    o->clip_to_parent = 1;

    o->fill = color;
    o->alpha = alpha ? alpha : 255;
    o->outline_width = 0;
    o->outline = (kcolor){0, 0, 0};
    o->outline_alpha = 255;

    o->u.text.font = font;
    o->u.text.text = text; // you can pass a kgfx_pmem_strdup(...) here if you want it owned
    o->u.text.x = x;
    o->u.text.y = y;
    o->u.text.scale = scale;
    o->u.text.char_spacing = char_spacing;
    o->u.text.line_spacing = line_spacing;
    o->u.text.align = align;

    h.idx = (int)g_obj_count++;
    return h;
}

kgfx_obj_handle kgfx_obj_add_image(const uint32_t *argb,
                                   uint32_t w, uint32_t h,
                                   int32_t x, int32_t y,
                                   uint32_t stride_px)
{
    kgfx_obj_handle handle = {.idx = -1};
    if (g_obj_count >= KGFX_MAX_OBJS)
        return handle;

    if (!argb || w == 0 || h == 0)
        return handle;

    if (stride_px == 0)
        stride_px = w;

    kgfx_obj *o = &g_objs[g_obj_count];
    o->kind = KGFX_OBJ_IMAGE;
    o->visible = 1;
    o->z = 0;
    o->parent_idx = -1;
    o->clip_to_parent = 1;

    // keep these consistent with your other objects
    o->fill = (kcolor){255, 255, 255}; // unused for image, but harmless
    o->alpha = 255;
    o->outline_width = 0;
    o->outline = (kcolor){0, 0, 0};
    o->outline_alpha = 255;

    o->u.image.x = x;
    o->u.image.y = y;
    o->u.image.w = w;
    o->u.image.h = h;
    o->u.image.src_w = w;
    o->u.image.src_h = h;
    o->u.image.argb = argb;
    o->u.image.stride_px = stride_px;
    o->u.image.sample_mode = KGFX_IMAGE_SAMPLE_NEAREST;

    handle.idx = (int)g_obj_count++;
    return handle;
}

kgfx_obj *kgfx_obj_ref(kgfx_obj_handle h)
{
    if (h.idx < 0 || (uint16_t)h.idx >= g_obj_count)
        return 0;
    return &g_objs[h.idx];
}

static inline void unpack_rgb(uint32_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((p & mR) >> sR);
    *g = (uint8_t)((p & mG) >> sG);
    *b = (uint8_t)((p & mB) >> sB);
}
static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << sR) | ((uint32_t)g << sG) | ((uint32_t)b << sB);
}

static inline int bb_clip_reject_point(int32_t x, int32_t y)
{
    if (!g_bb_clip.enabled)
        return 0;
    return x < g_bb_clip.x0 || y < g_bb_clip.y0 || x >= g_bb_clip.x1 || y >= g_bb_clip.y1;
}

static int clip_intersect(kgfx_clip_rect *dst, const kgfx_clip_rect *other)
{
    if (!dst || !other || !dst->enabled || !other->enabled)
        return 1;

    if (other->x0 > dst->x0)
        dst->x0 = other->x0;
    if (other->y0 > dst->y0)
        dst->y0 = other->y0;
    if (other->x1 < dst->x1)
        dst->x1 = other->x1;
    if (other->y1 < dst->y1)
        dst->y1 = other->y1;

    return dst->x0 < dst->x1 && dst->y0 < dst->y1;
}

static int clip_equal(const kgfx_clip_rect *a, const kgfx_clip_rect *b)
{
    if (!a || !b)
        return 0;
    return a->enabled == b->enabled &&
           a->x0 == b->x0 && a->y0 == b->y0 &&
           a->x1 == b->x1 && a->y1 == b->y1;
}

static int clip_intersects(const kgfx_clip_rect *a, const kgfx_clip_rect *b)
{
    if (!a || !b || !a->enabled || !b->enabled)
        return 0;
    return a->x0 < b->x1 && a->y0 < b->y1 && b->x0 < a->x1 && b->y0 < a->y1;
}

static void clip_union(kgfx_clip_rect *dst, const kgfx_clip_rect *src)
{
    if (!dst || !src || !src->enabled)
        return;

    if (!dst->enabled)
    {
        *dst = *src;
        return;
    }

    if (src->x0 < dst->x0)
        dst->x0 = src->x0;
    if (src->y0 < dst->y0)
        dst->y0 = src->y0;
    if (src->x1 > dst->x1)
        dst->x1 = src->x1;
    if (src->y1 > dst->y1)
        dst->y1 = src->y1;
}

static int color_equal(kcolor a, kcolor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static kgfx_clip_rect fb_clip_rect(void)
{
    kgfx_clip_rect clip;
    clip.x0 = 0;
    clip.y0 = 0;
    clip.x1 = (int32_t)FB.width;
    clip.y1 = (int32_t)FB.height;
    clip.enabled = 1;
    return clip;
}

static inline void hash_mix_u8(uint64_t *h, uint8_t v)
{
    *h ^= (uint64_t)v;
    *h *= 1099511628211ull;
}

static inline void hash_mix_u16(uint64_t *h, uint16_t v)
{
    hash_mix_u8(h, (uint8_t)(v & 0xFFu));
    hash_mix_u8(h, (uint8_t)(v >> 8));
}

static inline void hash_mix_u32(uint64_t *h, uint32_t v)
{
    hash_mix_u16(h, (uint16_t)(v & 0xFFFFu));
    hash_mix_u16(h, (uint16_t)(v >> 16));
}

static inline void hash_mix_i32(uint64_t *h, int32_t v)
{
    hash_mix_u32(h, (uint32_t)v);
}

static inline void hash_mix_u64(uint64_t *h, uint64_t v)
{
    hash_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    hash_mix_u32(h, (uint32_t)(v >> 32));
}

static uint64_t hash_cstr(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i = 0;

    if (!s)
    {
        hash_mix_u8(&h, 0xFFu);
        return h;
    }

    while (s[i])
    {
        hash_mix_u8(&h, (uint8_t)s[i]);
        ++i;
    }
    hash_mix_u8(&h, 0);
    return h;
}

static void obj_local_origin(const kgfx_obj *o, int32_t *x, int32_t *y)
{
    if (!o || !x || !y)
        return;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        *x = o->u.rect.x;
        *y = o->u.rect.y;
        break;
    case KGFX_OBJ_CIRCLE:
        *x = o->u.circle.cx;
        *y = o->u.circle.cy;
        break;
    case KGFX_OBJ_TEXT:
        *x = o->u.text.x;
        *y = o->u.text.y;
        break;
    case KGFX_OBJ_IMAGE:
        *x = o->u.image.x;
        *y = o->u.image.y;
        break;
    default:
        *x = 0;
        *y = 0;
        break;
    }
}

static int obj_world_bounds_clip(const kgfx_obj *o, int32_t origin_x, int32_t origin_y, kgfx_clip_rect *clip)
{
    if (!o || !clip)
        return 0;

    clip->enabled = 1;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        clip->x0 = origin_x;
        clip->y0 = origin_y;
        clip->x1 = origin_x + (int32_t)o->u.rect.w;
        clip->y1 = origin_y + (int32_t)o->u.rect.h;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    case KGFX_OBJ_CIRCLE:
        clip->x0 = origin_x - (int32_t)o->u.circle.r;
        clip->y0 = origin_y - (int32_t)o->u.circle.r;
        clip->x1 = origin_x + (int32_t)o->u.circle.r + 1;
        clip->y1 = origin_y + (int32_t)o->u.circle.r + 1;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    case KGFX_OBJ_IMAGE:
        clip->x0 = origin_x;
        clip->y0 = origin_y;
        clip->x1 = origin_x + (int32_t)o->u.image.w;
        clip->y1 = origin_y + (int32_t)o->u.image.h;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    default:
        break;
    }

    return 0;
}

static int resolve_obj(uint16_t idx, kgfx_resolved_obj *resolved)
{
    kgfx_resolved_obj *r = 0;
    const kgfx_obj *o = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    kgfx_clip_rect parent_bounds = {0, 0, 0, 0, 0};

    if (!resolved || idx >= g_obj_count)
        return 0;

    r = &resolved[idx];
    if (r->ready)
        return 1;
    if (r->visiting)
        return 0;

    r->visiting = 1;
    o = &g_objs[idx];
    obj_local_origin(o, &local_x, &local_y);

    r->origin_x = local_x;
    r->origin_y = local_y;
    r->z = o->z;
    r->root_z = o->z;
    r->root_idx = idx;
    r->clip.enabled = 1;
    r->clip.x0 = 0;
    r->clip.y0 = 0;
    r->clip.x1 = (int32_t)FB.width;
    r->clip.y1 = (int32_t)FB.height;

    if (o->parent_idx >= 0 && (uint16_t)o->parent_idx < g_obj_count)
    {
        if (!g_objs[(uint16_t)o->parent_idx].visible)
        {
            r->visiting = 0;
            return 0;
        }

        kgfx_resolved_obj *parent = &resolved[(uint16_t)o->parent_idx];
        if (!resolve_obj((uint16_t)o->parent_idx, resolved))
        {
            r->visiting = 0;
            return 0;
        }

        r->origin_x += parent->origin_x;
        r->origin_y += parent->origin_y;
        r->z += parent->z;
        r->root_z = parent->root_z;
        r->root_idx = parent->root_idx;
        r->clip = parent->clip;

        if (o->clip_to_parent && obj_world_bounds_clip(&g_objs[(uint16_t)o->parent_idx], parent->origin_x, parent->origin_y, &parent_bounds))
        {
            if (!clip_intersect(&r->clip, &parent_bounds))
            {
                r->visiting = 0;
                return 0;
            }
        }
    }

    if (r->clip.enabled && (r->clip.x0 >= r->clip.x1 || r->clip.y0 >= r->clip.y1))
    {
        r->visiting = 0;
        return 0;
    }

    r->ready = 1;
    r->visiting = 0;
    return 1;
}

static int text_world_bounds(const kgfx_text_data *t, uint16_t outline_width, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    const char *p = 0;
    int32_t line_y = 0;
    int32_t min_x = 0;
    int32_t min_y = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;
    uint8_t have_bounds = 0;
    int32_t glyph_h = 0;

    if (!t || !t->font || !t->text || !t->scale || !x0 || !y0 || !x1 || !y1)
        return 0;

    p = t->text;
    line_y = t->y;
    glyph_h = (int32_t)ktext_scale_mul_px(t->font->h, t->scale);

    do
    {
        uint32_t line_w = ktext_measure_line_px(t->font, p, t->scale, t->char_spacing);
        int32_t line_x = t->x;

        if (t->align == KTEXT_ALIGN_CENTER)
            line_x -= (int32_t)(line_w / 2u);
        else if (t->align == KTEXT_ALIGN_RIGHT)
            line_x -= (int32_t)line_w;

        if (line_w != 0 && glyph_h > 0)
        {
            if (!have_bounds)
            {
                min_x = line_x;
                min_y = line_y;
                max_x = line_x + (int32_t)line_w;
                max_y = line_y + glyph_h;
                have_bounds = 1;
            }
            else
            {
                if (line_x < min_x)
                    min_x = line_x;
                if (line_y < min_y)
                    min_y = line_y;
                if (line_x + (int32_t)line_w > max_x)
                    max_x = line_x + (int32_t)line_w;
                if (line_y + glyph_h > max_y)
                    max_y = line_y + glyph_h;
            }
        }

        while (*p && *p != '\n')
            ++p;
        if (*p == '\n')
        {
            ++p;
            line_y += glyph_h + t->line_spacing;
        }
    } while (*p);

    if (!have_bounds)
        return 0;

    min_x -= (int32_t)outline_width;
    min_y -= (int32_t)outline_width;
    max_x += (int32_t)outline_width;
    max_y += (int32_t)outline_width;

    *x0 = min_x;
    *y0 = min_y;
    *x1 = max_x;
    *y1 = max_y;
    return min_x < max_x && min_y < max_y;
}

static int obj_draw_bounds(const kgfx_obj *o, const kgfx_resolved_obj *r, kgfx_clip_rect *bounds)
{
    int32_t pad = 0;
    kgfx_clip_rect raw = {0, 0, 0, 0, 1};
    kgfx_clip_rect screen = fb_clip_rect();
    uint8_t outline_visible = 0;

    if (!o || !r || !bounds)
        return 0;

    outline_visible = (o->outline_width != 0 && o->outline_alpha != 0) ? 1u : 0u;
    pad = outline_visible ? (int32_t)o->outline_width : 0;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        if (!o->u.rect.w || !o->u.rect.h)
            return 0;
        if (o->alpha == 0 && !outline_visible)
            return 0;
        raw.x0 = r->origin_x - pad;
        raw.y0 = r->origin_y - pad;
        raw.x1 = r->origin_x + (int32_t)o->u.rect.w + pad;
        raw.y1 = r->origin_y + (int32_t)o->u.rect.h + pad;
        break;
    case KGFX_OBJ_CIRCLE: {
        int32_t radius = 0;
        if (!o->u.circle.r)
            return 0;
        if (o->alpha == 0 && !outline_visible)
            return 0;
        radius = (int32_t)o->u.circle.r + pad;
        raw.x0 = r->origin_x - radius;
        raw.y0 = r->origin_y - radius;
        raw.x1 = r->origin_x + radius + 1;
        raw.y1 = r->origin_y + radius + 1;
        break;
    }
    case KGFX_OBJ_TEXT:
        if (o->alpha == 0 && !outline_visible)
            return 0;
        if (!text_world_bounds(&o->u.text, outline_visible ? o->outline_width : 0, &raw.x0, &raw.y0, &raw.x1, &raw.y1))
            return 0;
        raw.x0 += r->origin_x - o->u.text.x;
        raw.y0 += r->origin_y - o->u.text.y;
        raw.x1 += r->origin_x - o->u.text.x;
        raw.y1 += r->origin_y - o->u.text.y;
        break;
    case KGFX_OBJ_IMAGE:
        if (!o->u.image.argb || !o->u.image.w || !o->u.image.h)
            return 0;
        if (o->alpha == 0 && !outline_visible)
            return 0;
        raw.x0 = r->origin_x - pad;
        raw.y0 = r->origin_y - pad;
        raw.x1 = r->origin_x + (int32_t)o->u.image.w + pad;
        raw.y1 = r->origin_y + (int32_t)o->u.image.h + pad;
        break;
    default:
        return 0;
    }

    *bounds = raw;
    if (!clip_intersect(bounds, &r->clip))
        return 0;
    if (!clip_intersect(bounds, &screen))
        return 0;
    return bounds->x0 < bounds->x1 && bounds->y0 < bounds->y1;
}

static uint64_t obj_visual_hash(const kgfx_obj *o, const kgfx_resolved_obj *r)
{
    uint64_t h = 1469598103934665603ull;

    if (!o || !r)
        return 0;

    hash_mix_u32(&h, (uint32_t)o->kind);
    hash_mix_i32(&h, r->origin_x);
    hash_mix_i32(&h, r->origin_y);
    hash_mix_i32(&h, r->z);
    hash_mix_i32(&h, r->root_z);
    hash_mix_u16(&h, r->root_idx);
    hash_mix_u8(&h, r->clip.enabled);
    hash_mix_i32(&h, r->clip.x0);
    hash_mix_i32(&h, r->clip.y0);
    hash_mix_i32(&h, r->clip.x1);
    hash_mix_i32(&h, r->clip.y1);
    hash_mix_u8(&h, o->visible);
    hash_mix_u8(&h, o->clip_to_parent);
    hash_mix_u8(&h, o->fill.r);
    hash_mix_u8(&h, o->fill.g);
    hash_mix_u8(&h, o->fill.b);
    hash_mix_u8(&h, o->alpha);
    hash_mix_u16(&h, o->outline_width);
    hash_mix_u8(&h, o->outline.r);
    hash_mix_u8(&h, o->outline.g);
    hash_mix_u8(&h, o->outline.b);
    hash_mix_u8(&h, o->outline_alpha);

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        hash_mix_i32(&h, o->u.rect.x);
        hash_mix_i32(&h, o->u.rect.y);
        hash_mix_u32(&h, o->u.rect.w);
        hash_mix_u32(&h, o->u.rect.h);
        break;
    case KGFX_OBJ_CIRCLE:
        hash_mix_i32(&h, o->u.circle.cx);
        hash_mix_i32(&h, o->u.circle.cy);
        hash_mix_u32(&h, o->u.circle.r);
        break;
    case KGFX_OBJ_TEXT:
        hash_mix_u64(&h, (uint64_t)(uintptr_t)o->u.text.font);
        hash_mix_i32(&h, o->u.text.x);
        hash_mix_i32(&h, o->u.text.y);
        hash_mix_u32(&h, o->u.text.scale);
        hash_mix_i32(&h, o->u.text.char_spacing);
        hash_mix_i32(&h, o->u.text.line_spacing);
        hash_mix_u32(&h, (uint32_t)o->u.text.align);
        hash_mix_u64(&h, (uint64_t)(uintptr_t)o->u.text.text);
        hash_mix_u64(&h, hash_cstr(o->u.text.text));
        break;
    case KGFX_OBJ_IMAGE:
        hash_mix_i32(&h, o->u.image.x);
        hash_mix_i32(&h, o->u.image.y);
        hash_mix_u32(&h, o->u.image.w);
        hash_mix_u32(&h, o->u.image.h);
        hash_mix_u32(&h, o->u.image.src_w);
        hash_mix_u32(&h, o->u.image.src_h);
        hash_mix_u64(&h, (uint64_t)(uintptr_t)o->u.image.argb);
        hash_mix_u32(&h, o->u.image.stride_px);
        hash_mix_u8(&h, o->u.image.sample_mode);
        break;
    default:
        break;
    }

    return h;
}

static int snapshot_build(const kgfx_obj *o, const kgfx_resolved_obj *r, kgfx_obj_snapshot *snap)
{
    if (!snap)
        return 0;

    snap->in_use = 1;
    snap->renderable = 0;
    snap->visual_hash = 0;
    snap->bounds = (kgfx_clip_rect){0, 0, 0, 0, 0};

    if (!o || !r)
        return 0;

    if (!obj_draw_bounds(o, r, &snap->bounds))
        return 0;

    snap->renderable = 1;
    snap->visual_hash = obj_visual_hash(o, r);
    return 1;
}

static int dirty_compare_and_accumulate(const kgfx_obj_snapshot *prev, const kgfx_obj_snapshot *curr, kgfx_clip_rect *dirty)
{
    if (!dirty)
        return 0;

    if ((!prev || !prev->in_use) && (!curr || !curr->in_use))
        return 0;

    if (!prev || !prev->in_use)
    {
        if (curr->renderable)
            clip_union(dirty, &curr->bounds);
        return 1;
    }

    if (!curr || !curr->in_use)
    {
        if (prev->renderable)
            clip_union(dirty, &prev->bounds);
        return 1;
    }

    if (prev->renderable != curr->renderable)
    {
        if (prev->renderable)
            clip_union(dirty, &prev->bounds);
        if (curr->renderable)
            clip_union(dirty, &curr->bounds);
        return 1;
    }

    if (!curr->renderable)
        return 0;

    if (prev->visual_hash != curr->visual_hash || !clip_equal(&prev->bounds, &curr->bounds))
    {
        clip_union(dirty, &prev->bounds);
        clip_union(dirty, &curr->bounds);
        return 1;
    }

    return 0;
}

static void bb_rect_clip(int32_t x, int32_t y, uint32_t w, uint32_t h, kcolor c, uint8_t a);

static void bb_clear_region(const kgfx_clip_rect *clip, kcolor c)
{
    if (!clip || !clip->enabled)
        return;
    bb_rect_clip(clip->x0, clip->y0,
                 (uint32_t)(clip->x1 - clip->x0),
                 (uint32_t)(clip->y1 - clip->y0),
                 c, 255);
}

static void present_full(void)
{
    for (uint32_t y = 0; y < FB.height; ++y)
    {
        void *dst = (void *)(FB.base + y * FB.pitch);
        void *src = (void *)(BB_ptr + y * BB_stride);
        kmemcpy64(dst, src, FB.pitch);
    }
    kgfx_flush();
}

static void present_region(const kgfx_clip_rect *clip)
{
    if (!clip || !clip->enabled)
        return;

    for (int32_t y = clip->y0; y < clip->y1; ++y)
    {
        volatile uint32_t *dst = (volatile uint32_t *)(FB.base + (uint32_t)y * FB.pitch + (uint32_t)clip->x0 * 4u);
        const uint32_t *src = (const uint32_t *)(BB_ptr + (uint32_t)y * BB_stride + (uint32_t)clip->x0 * 4u);
        uint32_t count = (uint32_t)(clip->x1 - clip->x0);
        uintptr_t start = (uintptr_t)dst;
        uintptr_t end = start + (uintptr_t)count * 4u;

        for (uint32_t x = 0; x < count; ++x)
            dst[x] = src[x];

        kgfx_flush_range(start, end);
    }
}

// blend a solid src color (sr,sg,sb,sa) over an existing packed dst pixel
static inline uint32_t blend_over(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (sa == 255)
        return pack_rgb(sr, sg, sb);
    if (sa == 0)
        return dst;
    uint8_t dr, dg, db;
    unpack_rgb(dst, &dr, &dg, &db);
    uint32_t inv = (uint32_t)(255 - sa);
    uint8_t or = (uint8_t)((sr * sa + dr * inv + 127) / 255);
    uint8_t og = (uint8_t)((sg * sa + dg * inv + 127) / 255);
    uint8_t ob = (uint8_t)((sb * sa + db * inv + 127) / 255);
    return pack_rgb(or, og, ob);
}

void kgfx_put_px_blend(int x, int y, kcolor c, uint8_t a)
{
    if (a == 0)
        return;
    if (x < 0 || y < 0 || x >= (int)FB.width || y >= (int)FB.height)
        return;
    if (bb_clip_reject_point(x, y))
        return;

    // Choose target: backbuffer if allocated, else front buffer
    uint8_t *base = BB_ptr ? BB_ptr : (uint8_t *)((uintptr_t)FB.base);
    uint8_t *p = base + (uint32_t)y * (BB_ptr ? BB_stride : FB.pitch) + (uint32_t)x * 4u;
    uint32_t *pix = (uint32_t *)p;

    if (a == 255)
    {
        *pix = ((uint32_t)c.r << sR) | ((uint32_t)c.g << sG) | ((uint32_t)c.b << sB);
        return;
    }

    // Blend src over dst
    uint8_t dr = (uint8_t)((*pix & mR) >> sR);
    uint8_t dg = (uint8_t)((*pix & mG) >> sG);
    uint8_t db = (uint8_t)((*pix & mB) >> sB);

    uint32_t inv = (uint32_t)(255 - a);
    uint8_t or_ = (uint8_t)((c.r * a + dr * inv + 127) / 255);
    uint8_t og = (uint8_t)((c.g * a + dg * inv + 127) / 255);
    uint8_t ob = (uint8_t)((c.b * a + db * inv + 127) / 255);

    *pix = ((uint32_t)or_ << sR) | ((uint32_t)og << sG) | ((uint32_t)ob << sB);
}

// fast span blender for a constant src color+alpha
static inline void bb_hspan_blend(int32_t y, int32_t x0, int32_t x1, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (!BB_ptr)
        return;
    if (y < 0 || y >= (int32_t)FB.height)
        return;
    if (g_bb_clip.enabled && (y < g_bb_clip.y0 || y >= g_bb_clip.y1))
        return;
    if (x0 > x1)
    {
        int32_t t = x0;
        x0 = x1;
        x1 = t;
    }
    if (x1 < 0 || x0 >= (int32_t)FB.width)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= (int32_t)FB.width)
        x1 = (int32_t)FB.width - 1;
    if (g_bb_clip.enabled)
    {
        if (x0 < g_bb_clip.x0)
            x0 = g_bb_clip.x0;
        if (x1 >= g_bb_clip.x1)
            x1 = g_bb_clip.x1 - 1;
    }
    if (x0 > x1)
        return;

    uint32_t *row = (uint32_t *)(BB_ptr + (uint32_t)y * BB_stride + (uint32_t)x0 * 4u);
    uint32_t count = (uint32_t)(x1 - x0 + 1);

    if (sa == 255)
    {
        uint32_t px = pack_rgb(sr, sg, sb);
        for (uint32_t i = 0; i < count; ++i)
            row[i] = px;
        return;
    }
    if (sa == 0)
        return;

    for (uint32_t i = 0; i < count; ++i)
    {
        row[i] = blend_over(row[i], sr, sg, sb, sa);
    }
}

/* ---------- draw helpers (clipped) ---------- */

static void bb_clear(kcolor c)
{
    uint32_t px = kgfx_pack(c);
    for (uint32_t y = 0; y < FB.height; ++y)
    {
        uint32_t *row = (uint32_t *)(BB_ptr + y * BB_stride);
        for (uint32_t x = 0; x < FB.width; ++x)
            row[x] = px;
    }
}

static void bb_rect_clip(int32_t x, int32_t y, uint32_t w, uint32_t h, kcolor c, uint8_t a)
{
    kgfx_clip_rect rect_clip = {0, 0, 0, 0, 1};

    if (!BB_ptr || !FB.width || !FB.height)
        return;
    int32_t x0 = x, y0 = y, x1 = x + (int32_t)w, y1 = y + (int32_t)h;
    if (g_bb_clip.enabled)
    {
        rect_clip = g_bb_clip;
        if (!clip_intersect(&rect_clip, &(kgfx_clip_rect){x0, y0, x1, y1, 1}))
            return;
        x0 = rect_clip.x0;
        y0 = rect_clip.y0;
        x1 = rect_clip.x1;
        y1 = rect_clip.y1;
    }
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)FB.width)
        x1 = (int32_t)FB.width;
    if (y1 > (int32_t)FB.height)
        y1 = (int32_t)FB.height;
    if (x1 <= x0 || y1 <= y0)
        return;

    for (int32_t yy = y0; yy < y1; ++yy)
    {
        bb_hspan_blend(yy, x0, x1 - 1, c.r, c.g, c.b, a);
    }
}

// draw a horizontal span [x0, x1] at row y, fully clipped
static inline void bb_hspan_clip(int32_t y, int32_t x0, int32_t x1, uint32_t px)
{
    if (!BB_ptr)
        return;
    if (y < 0 || y >= (int32_t)FB.height)
        return;
    if (g_bb_clip.enabled && (y < g_bb_clip.y0 || y >= g_bb_clip.y1))
        return;
    if (x0 > x1)
    {
        int32_t t = x0;
        x0 = x1;
        x1 = t;
    }

    if (x1 < 0 || x0 >= (int32_t)FB.width)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= (int32_t)FB.width)
        x1 = (int32_t)FB.width - 1;
    if (g_bb_clip.enabled)
    {
        if (x0 < g_bb_clip.x0)
            x0 = g_bb_clip.x0;
        if (x1 >= g_bb_clip.x1)
            x1 = g_bb_clip.x1 - 1;
    }
    if (x0 > x1)
        return;

    uint32_t count = (uint32_t)(x1 - x0 + 1);
    uint32_t *row = (uint32_t *)(BB_ptr + (uint32_t)y * BB_stride + (uint32_t)x0 * 4u);
    for (uint32_t i = 0; i < count; ++i)
        row[i] = px;
}

static void bb_blit_argb32_clip(int32_t x, int32_t y,
                                uint32_t w, uint32_t h,
                                const uint32_t *src_argb, uint32_t src_stride_px,
                                uint8_t global_alpha)
{
    if (!BB_ptr || !src_argb || w == 0 || h == 0 || global_alpha == 0)
        return;

    if (src_stride_px < w)
        return; // prevents out-of-bounds row reads

    // Clip against screen
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + (int32_t)w;
    int32_t y1 = y + (int32_t)h;

    if (x1 <= 0 || y1 <= 0 || x0 >= (int32_t)FB.width || y0 >= (int32_t)FB.height)
        return;
    if (g_bb_clip.enabled &&
        (x1 <= g_bb_clip.x0 || y1 <= g_bb_clip.y0 || x0 >= g_bb_clip.x1 || y0 >= g_bb_clip.y1))
        return;

    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x1 > (int32_t)FB.width) ? (int32_t)FB.width : x1;
    int32_t cy1 = (y1 > (int32_t)FB.height) ? (int32_t)FB.height : y1;
    if (g_bb_clip.enabled)
    {
        if (cx0 < g_bb_clip.x0)
            cx0 = g_bb_clip.x0;
        if (cy0 < g_bb_clip.y0)
            cy0 = g_bb_clip.y0;
        if (cx1 > g_bb_clip.x1)
            cx1 = g_bb_clip.x1;
        if (cy1 > g_bb_clip.y1)
            cy1 = g_bb_clip.y1;
    }
    if (cx1 <= cx0 || cy1 <= cy0)
        return;

    uint32_t copy_w = (uint32_t)(cx1 - cx0);
    uint32_t copy_h = (uint32_t)(cy1 - cy0);

    // Offsets into source
    uint32_t sx0 = (uint32_t)(cx0 - x0);
    uint32_t sy0 = (uint32_t)(cy0 - y0);

    for (uint32_t iy = 0; iy < copy_h; ++iy)
    {
        const uint32_t *srow = src_argb + (uint64_t)(sy0 + iy) * src_stride_px + sx0;
        uint8_t *drow8 = BB_ptr + (uint32_t)(cy0 + (int32_t)iy) * BB_stride + (uint32_t)cx0 * 4u;
        uint32_t *drow = (uint32_t *)drow8;

        for (uint32_t ix = 0; ix < copy_w; ++ix)
        {
            uint32_t sp = srow[ix]; // 0xAARRGGBB
            uint8_t sa = (uint8_t)(sp >> 24);
            if (sa == 0)
                continue;

            if (global_alpha == 255 && sa == 255)
            {
                drow[ix] = sp & 0x00FFFFFFu | 0xFF000000u;
                continue;
            }

            // Multiply per-pixel alpha by object alpha
            // out_a = (sa * global_alpha) / 255 with rounding
            uint16_t aa = (uint16_t)sa * (uint16_t)global_alpha + 127;
            uint8_t a = (uint8_t)(aa / 255);
            if (a == 0)
                continue;

            uint8_t sr = (uint8_t)(sp >> 16);
            uint8_t sg = (uint8_t)(sp >> 8);
            uint8_t sb = (uint8_t)(sp);

            uint32_t dst = drow[ix];
            drow[ix] = blend_over(dst, sr, sg, sb, a);
        }
    }
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint32_t t256)
{
    return (uint8_t)(((uint32_t)a * (256u - t256) + (uint32_t)b * t256 + 128u) >> 8);
}

static inline uint32_t sample_argb32_nearest(const uint32_t *src_argb,
                                             uint32_t src_w, uint32_t src_h,
                                             uint32_t src_stride_px,
                                             uint32_t fx16, uint32_t fy16)
{
    uint32_t sx = fx16 >> 16;
    uint32_t sy = fy16 >> 16;

    if (sx >= src_w)
        sx = src_w - 1u;
    if (sy >= src_h)
        sy = src_h - 1u;

    return src_argb[(uint64_t)sy * src_stride_px + sx];
}

static inline uint32_t sample_argb32_bilinear(const uint32_t *src_argb,
                                              uint32_t src_w, uint32_t src_h,
                                              uint32_t src_stride_px,
                                              uint32_t fx16, uint32_t fy16)
{
    uint32_t sx0 = fx16 >> 16;
    uint32_t sy0 = fy16 >> 16;
    uint32_t sx1 = sx0 + 1u;
    uint32_t sy1 = sy0 + 1u;
    uint32_t tx = (fx16 >> 8) & 0xFFu;
    uint32_t ty = (fy16 >> 8) & 0xFFu;
    uint32_t p00 = 0;
    uint32_t p10 = 0;
    uint32_t p01 = 0;
    uint32_t p11 = 0;
    uint8_t a0 = 0, r0 = 0, g0 = 0, b0 = 0;
    uint8_t a1 = 0, r1 = 0, g1 = 0, b1 = 0;

    if (sx0 >= src_w)
        sx0 = src_w - 1u;
    if (sy0 >= src_h)
        sy0 = src_h - 1u;
    if (sx1 >= src_w)
        sx1 = src_w - 1u;
    if (sy1 >= src_h)
        sy1 = src_h - 1u;

    p00 = src_argb[(uint64_t)sy0 * src_stride_px + sx0];
    p10 = src_argb[(uint64_t)sy0 * src_stride_px + sx1];
    p01 = src_argb[(uint64_t)sy1 * src_stride_px + sx0];
    p11 = src_argb[(uint64_t)sy1 * src_stride_px + sx1];

    a0 = lerp_u8((uint8_t)(p00 >> 24), (uint8_t)(p10 >> 24), tx);
    r0 = lerp_u8((uint8_t)(p00 >> 16), (uint8_t)(p10 >> 16), tx);
    g0 = lerp_u8((uint8_t)(p00 >> 8), (uint8_t)(p10 >> 8), tx);
    b0 = lerp_u8((uint8_t)p00, (uint8_t)p10, tx);

    a1 = lerp_u8((uint8_t)(p01 >> 24), (uint8_t)(p11 >> 24), tx);
    r1 = lerp_u8((uint8_t)(p01 >> 16), (uint8_t)(p11 >> 16), tx);
    g1 = lerp_u8((uint8_t)(p01 >> 8), (uint8_t)(p11 >> 8), tx);
    b1 = lerp_u8((uint8_t)p01, (uint8_t)p11, tx);

    return ((uint32_t)lerp_u8(a0, a1, ty) << 24) |
           ((uint32_t)lerp_u8(r0, r1, ty) << 16) |
           ((uint32_t)lerp_u8(g0, g1, ty) << 8) |
           (uint32_t)lerp_u8(b0, b1, ty);
}

static void bb_blit_argb32_scaled_clip(int32_t x, int32_t y,
                                       uint32_t dst_w, uint32_t dst_h,
                                       const uint32_t *src_argb,
                                       uint32_t src_w, uint32_t src_h,
                                       uint32_t src_stride_px,
                                       uint8_t global_alpha,
                                       uint8_t sample_mode)
{
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    uint32_t cx0 = 0;
    uint32_t cy0 = 0;
    uint32_t copy_w = 0;
    uint32_t copy_h = 0;
    uint32_t step_x = 0;
    uint32_t step_y = 0;

    if (!BB_ptr || !src_argb || dst_w == 0 || dst_h == 0 || src_w == 0 || src_h == 0 || global_alpha == 0)
        return;

    if (src_stride_px < src_w)
        return;

    x0 = x;
    y0 = y;
    x1 = x + (int32_t)dst_w;
    y1 = y + (int32_t)dst_h;

    if (x1 <= 0 || y1 <= 0 || x0 >= (int32_t)FB.width || y0 >= (int32_t)FB.height)
        return;
    if (g_bb_clip.enabled &&
        (x1 <= g_bb_clip.x0 || y1 <= g_bb_clip.y0 || x0 >= g_bb_clip.x1 || y0 >= g_bb_clip.y1))
        return;

    cx0 = (uint32_t)((x0 < 0) ? 0 : x0);
    cy0 = (uint32_t)((y0 < 0) ? 0 : y0);
    x1 = (x1 > (int32_t)FB.width) ? (int32_t)FB.width : x1;
    y1 = (y1 > (int32_t)FB.height) ? (int32_t)FB.height : y1;
    if (g_bb_clip.enabled)
    {
        if ((int32_t)cx0 < g_bb_clip.x0)
            cx0 = (uint32_t)g_bb_clip.x0;
        if ((int32_t)cy0 < g_bb_clip.y0)
            cy0 = (uint32_t)g_bb_clip.y0;
        if (x1 > g_bb_clip.x1)
            x1 = g_bb_clip.x1;
        if (y1 > g_bb_clip.y1)
            y1 = g_bb_clip.y1;
    }
    if (x1 <= (int32_t)cx0 || y1 <= (int32_t)cy0)
        return;

    copy_w = (uint32_t)(x1 - (int32_t)cx0);
    copy_h = (uint32_t)(y1 - (int32_t)cy0);
    step_x = ((uint64_t)src_w << 16) / dst_w;
    step_y = ((uint64_t)src_h << 16) / dst_h;

    for (uint32_t iy = 0; iy < copy_h; ++iy)
    {
        uint8_t *drow8 = BB_ptr + (cy0 + iy) * BB_stride + cx0 * 4u;
        uint32_t *drow = (uint32_t *)drow8;
        uint32_t fy16 = ((uint32_t)(((int32_t)cy0 + (int32_t)iy - y) < 0 ? 0 : ((int32_t)cy0 + (int32_t)iy - y)) * step_y);

        for (uint32_t ix = 0; ix < copy_w; ++ix)
        {
            uint32_t fx16 = ((uint32_t)(((int32_t)cx0 + (int32_t)ix - x) < 0 ? 0 : ((int32_t)cx0 + (int32_t)ix - x)) * step_x);
            uint32_t sp = (sample_mode == KGFX_IMAGE_SAMPLE_BILINEAR)
                              ? sample_argb32_bilinear(src_argb, src_w, src_h, src_stride_px, fx16, fy16)
                              : sample_argb32_nearest(src_argb, src_w, src_h, src_stride_px, fx16, fy16);
            uint8_t sa = (uint8_t)(sp >> 24);

            if (sa == 0)
                continue;

            if (global_alpha == 255 && sa == 255)
            {
                drow[ix] = sp & 0x00FFFFFFu | 0xFF000000u;
                continue;
            }

            {
                uint16_t aa = (uint16_t)sa * (uint16_t)global_alpha + 127;
                uint8_t a = (uint8_t)(aa / 255);
                uint8_t sr = (uint8_t)(sp >> 16);
                uint8_t sg = (uint8_t)(sp >> 8);
                uint8_t sb = (uint8_t)(sp);

                if (a == 0)
                    continue;

                drow[ix] = blend_over(drow[ix], sr, sg, sb, a);
            }
        }
    }
}

// integer sqrt for u64 (fast, no lib)
static inline uint32_t isqrt_u64(uint64_t v)
{
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= r + bit)
        {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* bb_hspan_blend(y, x0, x1, sr,sg,sb,sa) must treat x0..x1 as INCLUSIVE. */

/* ---------- PERFECT filled circle (alpha) ---------- */
static void bb_circle_fill_clip(int32_t cx, int32_t cy, uint32_t r, kcolor c, uint8_t a)
{
    if (!BB_ptr || !FB.width || !FB.height || r == 0)
        return;

    int32_t y_min = -(int32_t)r;
    int32_t y_max = (int32_t)r;

    for (int32_t dy = y_min; dy <= y_max; ++dy)
    {
        int32_t y = cy + dy;
        if ((unsigned)y >= FB.height)
            continue;

        uint64_t yy = (uint64_t)dy * (uint64_t)dy;
        uint64_t rr = (uint64_t)r * (uint64_t)r;
        uint32_t ox = isqrt_u64(rr - (yy <= rr ? yy : rr)); // clamp for safety

        int32_t x0 = cx - (int32_t)ox;
        int32_t x1 = cx + (int32_t)ox;
        bb_hspan_blend(y, x0, x1, c.r, c.g, c.b, a); // inclusive span
    }
}

/* ------------ OUTSIDE rectangle outline (matches your call) ------------- */
static void bb_rect_outline_clip(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                 uint32_t t, kcolor col, uint8_t a)
{
    if (!BB_ptr || !FB.width || !FB.height || t == 0)
        return;

    int32_t x0 = x - (int32_t)t;
    int32_t y0 = y - (int32_t)t;
    int32_t x1 = x + (int32_t)w + (int32_t)t - 1;
    int32_t y1 = y + (int32_t)h + (int32_t)t - 1;

    /* top band */
    for (int32_t yy = y0; yy < y0 + (int32_t)t; ++yy)
        bb_hspan_blend(yy, x0, x1, col.r, col.g, col.b, a);

    /* bottom band */
    for (int32_t yy = y1 - (int32_t)t + 1; yy <= y1; ++yy)
        bb_hspan_blend(yy, x0, x1, col.r, col.g, col.b, a);

    /* sides */
    for (int32_t yy = y0 + (int32_t)t; yy <= y1 - (int32_t)t; ++yy)
    {
        bb_hspan_blend(yy, x0, x0 + (int32_t)t - 1, col.r, col.g, col.b, a); // left
        bb_hspan_blend(yy, x1 - (int32_t)t + 1, x1, col.r, col.g, col.b, a); // right
    }
}

/* ---- SOLID outside ring: fills between r .. r+t using spans (gap-free) ---- */
static void bb_circle_outline_outside_clip(int32_t cx, int32_t cy,
                                           uint32_t r, uint32_t t,
                                           kcolor col, uint8_t a)
{
    if (!BB_ptr || !FB.width || !FB.height || r == 0 || t == 0)
        return;

    const uint32_t r_out = r + t;
    const uint64_t R2o = (uint64_t)r_out * r_out;
    const uint64_t R2i = (uint64_t)r * r;

    for (int32_t dy = -(int32_t)r_out; dy <= (int32_t)r_out; ++dy)
    {
        int32_t y = cy + dy;
        if ((unsigned)y >= FB.height)
            continue;

        uint64_t yy = (uint64_t)dy * (uint64_t)dy;
        if (yy > R2o)
            continue; // outside outer circle

        // half-widths on this scanline
        int32_t ox = (int32_t)isqrt_u64(R2o - yy);
        int32_t ix = (yy < R2i) ? (int32_t)isqrt_u64(R2i - yy) : -1; // -1 means "no inner hole here"

        int32_t xLo = cx - ox;
        int32_t xRo = cx + ox;

        int drawn = 0;

        if (ix < 0)
        {
            // inner radius not present on this row → draw the whole outer span
            bb_hspan_blend(y, xLo, xRo, col.r, col.g, col.b, a);
            drawn = 1;
        }
        else
        {
            int32_t xLi = cx - ix;
            int32_t xRi = cx + ix;

            // left ring part [xLo .. xLi-1]
            if (xLi - 1 >= xLo)
            {
                bb_hspan_blend(y, xLo, xLi - 1, col.r, col.g, col.b, a);
                drawn = 1;
            }
            // right ring part [xRi+1 .. xRo]
            if (xRo >= xRi + 1)
            {
                bb_hspan_blend(y, xRi + 1, xRo, col.r, col.g, col.b, a);
                drawn = 1;
            }
        }

        // exact poles: if nothing drawn on this row, put a 1-px cap
        if (!drawn && (dy == -(int32_t)r_out || dy == (int32_t)r_out))
        {
            bb_hspan_blend(y, cx, cx, col.r, col.g, col.b, a);
        }
    }
}

/* ---------- z-sort (stable) over generic objects ---------- */

static int render_entry_cmp(const kgfx_render_entry *a, const kgfx_render_entry *b,
                            const kgfx_resolved_obj *resolved)
{
    const kgfx_resolved_obj *ra = &resolved[a->idx];
    const kgfx_resolved_obj *rb = &resolved[b->idx];

    if (ra->root_z < rb->root_z)
        return -1;
    if (ra->root_z > rb->root_z)
        return 1;

    /*
     * Keep different parent roots isolated even when they share z.
     * Child/local z may reorder only inside the same root subtree.
     */
    if (ra->root_idx < rb->root_idx)
        return -1;
    if (ra->root_idx > rb->root_idx)
        return 1;

    if (ra->z < rb->z)
        return -1;
    if (ra->z > rb->z)
        return 1;

    if (a->idx < b->idx)
        return -1;
    if (a->idx > b->idx)
        return 1;

    return 0;
}

static void sort_render_entries(kgfx_render_entry *entries, uint16_t n,
                                const kgfx_resolved_obj *resolved)
{
    if (n <= 1)
        return;

    if (n <= 32)
    {
        for (uint16_t i = 1; i < n; ++i)
        {
            kgfx_render_entry v = entries[i];
            uint16_t k = i;
            while (k > 0 && render_entry_cmp(&entries[k - 1], &v, resolved) > 0)
            {
                entries[k] = entries[k - 1];
                --k;
            }
            entries[k] = v;
        }
        return;
    }

    kgfx_render_entry tmp[KGFX_MAX_OBJS];
    for (uint16_t width = 1; width < n; width <<= 1)
    {
        uint16_t out = 0;
        for (uint16_t i = 0; i < n; i += (uint16_t)(width << 1))
        {
            uint16_t L = i;
            uint16_t M = (i + width < n) ? (uint16_t)(i + width) : n;
            uint16_t R = (i + (width << 1) < n) ? (uint16_t)(i + (width << 1)) : n;
            uint16_t a = L, b = M;
            while (a < M && b < R)
            {
                if (render_entry_cmp(&entries[a], &entries[b], resolved) <= 0)
                    tmp[out++] = entries[a++];
                else
                    tmp[out++] = entries[b++];
            }
            while (a < M)
                tmp[out++] = entries[a++];
            while (b < R)
                tmp[out++] = entries[b++];
        }
        for (uint16_t i = 0; i < n; ++i)
            entries[i] = tmp[i];
    }
}

int kgfx_obj_destroy(kgfx_obj_handle h)
{
    if (h.idx < 0 || (uint16_t)h.idx >= g_obj_count)
        return -1;

    uint16_t idx = (uint16_t)h.idx;
    uint16_t last = (uint16_t)(g_obj_count - 1);

    if (idx != last)
    {
        g_objs[idx] = g_objs[last];
    }

    for (uint16_t i = 0; i < last; ++i)
    {
        if (g_objs[i].parent_idx == (int16_t)idx)
            g_objs[i].parent_idx = -1;
        else if (g_objs[i].parent_idx == (int16_t)last && idx != last)
            g_objs[i].parent_idx = (int16_t)idx;
    }

    // optional: zero old last slot for cleaner debugging
    g_objs[last].visible = 0;
    g_objs[last].z = 0;
    g_objs[last].kind = 0;

    g_obj_count--;
    return 0;
}

/* ---------- scene init & render ---------- */

int kgfx_scene_init(void)
{
    if (!FB.base || !FB.pitch || !FB.width || !FB.height)
        return -1;
    uint64_t need = (uint64_t)FB.pitch * FB.height;
    uint64_t pages = (need + 4095) >> 12;
    void *p = pmem_alloc_pages(pages);
    if (!p)
        return -1;
    BB_ptr = (uint8_t *)p;
    BB_stride = FB.pitch;
    BB_bytes = pages * 4096ull;
    g_obj_count = 0;
    g_prev_snapshot_count = 0;
    g_prev_clear_color = (kcolor){0, 0, 0};
    g_have_prev_frame = 0;
    g_bb_clip = (kgfx_clip_rect){0, 0, 0, 0, 0};
    for (uint16_t i = 0; i < KGFX_MAX_OBJS; ++i)
        g_prev_snapshot[i] = (kgfx_obj_snapshot){0};
    return 0;
}

void kgfx_render_all(kcolor clear_color)
{
    kgfx_resolved_obj resolved[KGFX_MAX_OBJS] = {0};
    kgfx_render_entry entries[KGFX_MAX_OBJS];
    kgfx_obj_snapshot current[KGFX_MAX_OBJS] = {0};
    kgfx_clip_rect dirty = {0, 0, 0, 0, 0};
    kgfx_clip_rect full = fb_clip_rect();
    uint8_t full_redraw = 0;

    if (!BB_ptr)
        return;

    // 1) collect visible objects and build visual snapshots
    uint16_t n = 0;
    for (uint16_t i = 0; i < g_obj_count; ++i)
    {
        current[i].in_use = 1;

        if (!g_objs[i].visible)
            continue;
        if (!resolve_obj(i, resolved))
            continue;

        if (snapshot_build(&g_objs[i], &resolved[i], &current[i]))
        {
            entries[n].idx = i;
            entries[n].z = resolved[i].z;
            ++n;
        }
    }

    // 2) stable z-sort
    sort_render_entries(entries, n, resolved);

    // 3) decide which pixels need to be refreshed
    if (!g_have_prev_frame || !color_equal(g_prev_clear_color, clear_color))
    {
        dirty = full;
        full_redraw = 1;
    }
    else
    {
        uint16_t max_count = (g_obj_count > g_prev_snapshot_count) ? g_obj_count : g_prev_snapshot_count;
        for (uint16_t i = 0; i < max_count; ++i)
        {
            kgfx_obj_snapshot *prev = (i < g_prev_snapshot_count) ? &g_prev_snapshot[i] : 0;
            kgfx_obj_snapshot *curr = (i < g_obj_count) ? &current[i] : 0;
            dirty_compare_and_accumulate(prev, curr, &dirty);
        }
    }

    if (!dirty.enabled)
    {
        for (uint16_t i = 0; i < g_obj_count; ++i)
            g_prev_snapshot[i] = current[i];
        g_prev_snapshot_count = g_obj_count;
        g_prev_clear_color = clear_color;
        g_have_prev_frame = 1;
        return;
    }

    if (clip_equal(&dirty, &full))
        full_redraw = 1;

    // 4) refresh the dirty part of the backbuffer
    if (full_redraw)
    {
        g_bb_clip = (kgfx_clip_rect){0, 0, 0, 0, 0};
        bb_clear(clear_color);
    }
    else
    {
        g_bb_clip = dirty;
        bb_clear_region(&dirty, clear_color);
    }

    // 5) draw by kind, clipped to the dirty region when partial
    for (uint16_t i = 0; i < n; ++i)
    {
        kgfx_clip_rect prev_clip = g_bb_clip;
        const kgfx_obj *o = &g_objs[entries[i].idx];
        const kgfx_resolved_obj *r = &resolved[entries[i].idx];

        if (!full_redraw && !clip_intersects(&current[entries[i].idx].bounds, &dirty))
            continue;

        g_bb_clip = r->clip;
        if (prev_clip.enabled && !clip_intersect(&g_bb_clip, &prev_clip))
        {
            g_bb_clip = prev_clip;
            continue;
        }

        switch (o->kind)
        {
        case KGFX_OBJ_RECT:
            bb_rect_clip(r->origin_x, r->origin_y, o->u.rect.w, o->u.rect.h, o->fill, o->alpha);
            if (o->outline_width)
                bb_rect_outline_clip(r->origin_x, r->origin_y, o->u.rect.w, o->u.rect.h,
                                     o->outline_width, o->outline, o->outline_alpha);
            break;
        case KGFX_OBJ_CIRCLE:
            // fill first
            bb_circle_fill_clip(r->origin_x, r->origin_y, o->u.circle.r, o->fill, o->alpha);
            // outside outline second
            if (o->outline_width)
                bb_circle_outline_outside_clip(r->origin_x, r->origin_y,
                                               o->u.circle.r, o->outline_width,
                                               o->outline, o->outline_alpha);
            break;
        case KGFX_OBJ_TEXT: {
            const kgfx_text_data *t = &o->u.text;
            if (t->font && t->text && t->scale)
            {
                // text uses fill/alpha as the glyph color, and outline_* for border
                ktext_draw_str_align_outline(
                    t->font,
                    r->origin_x, r->origin_y,
                    t->text,
                    o->fill, o->alpha,
                    t->scale,
                    t->char_spacing, t->line_spacing,
                    t->align,
                    o->outline_width, o->outline, o->outline_alpha);
            }
            break;
        }
        case KGFX_OBJ_IMAGE: {
            const kgfx_image_data *im = &o->u.image;

            // draw image first
            if (im->src_w == im->w && im->src_h == im->h)
            {
                bb_blit_argb32_clip(r->origin_x, r->origin_y, im->w, im->h,
                                    im->argb, im->stride_px,
                                    o->alpha);
            }
            else
            {
                bb_blit_argb32_scaled_clip(r->origin_x, r->origin_y,
                                           im->w, im->h,
                                           im->argb,
                                           im->src_w, im->src_h,
                                           im->stride_px,
                                           o->alpha,
                                           im->sample_mode);
            }

            // optional border/outline on top (same behaviour as others)
            if (o->outline_width)
                bb_rect_outline_clip(r->origin_x, r->origin_y, (int32_t)im->w, (int32_t)im->h,
                                     o->outline_width, o->outline, o->outline_alpha);

            break;
        }
        default:
            break;
        }
        g_bb_clip = prev_clip;
    }

    // 5) present backbuffer → framebuffer
    for (uint32_t y = 0; y < FB.height; ++y)
    {
        void *dst = (void *)(FB.base + y * FB.pitch);
        void *src = (void *)(BB_ptr + y * BB_stride);
        kmemcpy64(dst, src, FB.pitch);
    }
    kgfx_flush();
}
