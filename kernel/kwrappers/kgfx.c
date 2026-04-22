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

/* backbuffer (you already have these) */
static uint8_t *BB_ptr = NULL;
static uint32_t BB_stride = 0;
static uint64_t BB_bytes = 0;

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
    o->u.image.argb = argb;
    o->u.image.stride_px = stride_px;

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
    if (!BB_ptr || !FB.width || !FB.height)
        return;
    int32_t x0 = x, y0 = y, x1 = x + (int32_t)w, y1 = y + (int32_t)h;
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

    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x1 > (int32_t)FB.width) ? (int32_t)FB.width : x1;
    int32_t cy1 = (y1 > (int32_t)FB.height) ? (int32_t)FB.height : y1;

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

static void sort_by_z(uint16_t *idxs, uint16_t n)
{
    if (n <= 1)
        return;

    if (n <= 32)
    {
        for (uint16_t i = 1; i < n; ++i)
        {
            uint16_t v = idxs[i], k = i;
            while (k > 0 && g_objs[idxs[k - 1]].z > g_objs[v].z)
            {
                idxs[k] = idxs[k - 1];
                --k;
            }
            idxs[k] = v;
        }
        return;
    }

    uint16_t tmp[KGFX_MAX_OBJS];
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
                if (g_objs[idxs[a]].z <= g_objs[idxs[b]].z)
                    tmp[out++] = idxs[a++];
                else
                    tmp[out++] = idxs[b++];
            }
            while (a < M)
                tmp[out++] = idxs[a++];
            while (b < R)
                tmp[out++] = idxs[b++];
        }
        for (uint16_t i = 0; i < n; ++i)
            idxs[i] = tmp[i];
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
    return 0;
}

void kgfx_render_all(kcolor clear_color)
{
    if (!BB_ptr)
        return;

    // 1) clear backbuffer
    bb_clear(clear_color);

    // 2) collect visible objects
    uint16_t idxs[KGFX_MAX_OBJS];
    uint16_t n = 0;
    for (uint16_t i = 0; i < g_obj_count; ++i)
        if (g_objs[i].visible)
            idxs[n++] = i;

    // 3) stable z-sort
    sort_by_z(idxs, n);

    // 4) draw by kind
    for (uint16_t i = 0; i < n; ++i)
    {
        const kgfx_obj *o = &g_objs[idxs[i]];
        switch (o->kind)
        {
        case KGFX_OBJ_RECT:
            bb_rect_clip(o->u.rect.x, o->u.rect.y, o->u.rect.w, o->u.rect.h, o->fill, o->alpha);
            if (o->outline_width)
                bb_rect_outline_clip(o->u.rect.x, o->u.rect.y, o->u.rect.w, o->u.rect.h,
                                     o->outline_width, o->outline, o->outline_alpha);
            break;
        case KGFX_OBJ_CIRCLE:
            // fill first
            bb_circle_fill_clip(o->u.circle.cx, o->u.circle.cy, o->u.circle.r, o->fill, o->alpha);
            // outside outline second
            if (o->outline_width)
                bb_circle_outline_outside_clip(o->u.circle.cx, o->u.circle.cy,
                                               o->u.circle.r, o->outline_width,
                                               o->outline, o->outline_alpha);
            break;
        case KGFX_OBJ_TEXT:
            const kgfx_text_data *t = &o->u.text;
            if (t->font && t->text && t->scale)
            {
                // text uses fill/alpha as the glyph color, and outline_* for border
                ktext_draw_str_align_outline(
                    t->font,
                    t->x, t->y,
                    t->text,
                    o->fill, o->alpha,
                    t->scale,
                    t->char_spacing, t->line_spacing,
                    t->align,
                    o->outline_width, o->outline, o->outline_alpha);
            }
            break;
        case KGFX_OBJ_IMAGE:
            const kgfx_image_data *im = &o->u.image;

            // draw image first
            bb_blit_argb32_clip(im->x, im->y, im->w, im->h,
                                im->argb, im->stride_px,
                                o->alpha);

            // optional border/outline on top (same behaviour as others)
            if (o->outline_width)
                bb_rect_outline_clip(im->x, im->y, (int32_t)im->w, (int32_t)im->h,
                                     o->outline_width, o->outline, o->outline_alpha);

            break;
        default:
            break;
        }
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
