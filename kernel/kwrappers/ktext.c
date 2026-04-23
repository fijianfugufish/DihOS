#include <stdint.h>
#include <stddef.h>
#include "kwrappers/ktext.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kfile.h"

// PSF2 header
#pragma pack(push, 1)
typedef struct
{
    uint32_t magic; // 0x864AB572
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t glyph_count;
    uint32_t bytes_per_glyph;
    uint32_t height;
    uint32_t width;
} psf2_hdr;
#pragma pack(pop)

// Add near the top of ktext.c, next to psf2_hdr
#pragma pack(push, 1)
typedef struct
{
    uint16_t magic;   // 0x0436 (little-endian)
    uint8_t mode;     // bit 0: 512 glyphs; bit 1: has unicode table; bit 2: has sequences
    uint8_t charsize; // bytes per glyph (PSF1 is always 8 pixels wide)
} psf1_hdr;
#pragma pack(pop)

#define PSF1_MAGIC 0x0436
#define PSF1_MODE512 0x01
#define PSF1_MODEHASTAB 0x02

#define KTEXT_SCALE_BASE 10u

static inline uint32_t ktext_scale_to_tenths(uint32_t scale)
{
    if (scale == 0)
        return 0;
    return KTEXT_SCALE_BASE + (scale - 1u);
}

uint32_t ktext_scale_mul_px(uint32_t px, uint32_t scale)
{
    uint64_t scaled = 0;
    uint32_t tenths = ktext_scale_to_tenths(scale);

    if (px == 0 || tenths == 0)
        return 0;

    scaled = (uint64_t)px * (uint64_t)tenths;
    return (uint32_t)((scaled + (KTEXT_SCALE_BASE / 2u)) / KTEXT_SCALE_BASE);
}

/* ------------ file -> pmem -------------- */
static int read_all_file(const char *path, void **out, uint32_t *out_sz)
{
    KFile f;
    if (kfile_open(&f, path, KFILE_READ) != 0)
        return -1;

    uint64_t sz = kfile_size(&f);
    if (sz == 0 || sz > (1ull << 31))
    {
        kfile_close(&f);
        return -1;
    }

    uint64_t pages = (sz + 4095) >> 12;
    void *buf = pmem_alloc_pages(pages);
    if (!buf)
    {
        kfile_close(&f);
        return -1;
    }

    uint8_t *p = (uint8_t *)buf;
    uint32_t left = (uint32_t)sz;
    while (left)
    {
        uint32_t chunk = left > 4096 ? 4096 : left;
        uint32_t got = 0;
        if (kfile_read(&f, p, chunk, &got) != 0 || got != chunk)
        {
            kfile_close(&f);
            return -1;
        }
        p += chunk;
        left -= chunk;
    }
    kfile_close(&f);
    *out = buf;
    *out_sz = (uint32_t)sz;
    return 0;
}

/* ------------ variable-width helpers -------------- */

// Compute tight bounds (leftmost non-empty column and width) for one glyph
static void psf2_glyph_tight_bounds(const uint8_t *glyph,
                                    uint32_t w, uint32_t h, uint32_t row_bytes,
                                    uint8_t *out_left, uint8_t *out_width)
{
    uint32_t left = w; // sentinel for empty
    int32_t right = -1;

    for (uint32_t x = 0; x < w; ++x)
    {
        // check column x
        uint8_t any = 0;
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint8_t *rowbits = glyph + y * row_bytes;
            uint8_t b = rowbits[x >> 3];
            uint8_t mask = (uint8_t)(1u << (7 - (x & 7)));
            if (b & mask)
            {
                any = 1;
                break;
            }
        }
        if (any)
        {
            if (left == w)
                left = x;
            right = (int32_t)x;
        }
    }

    if (right < 0)
    {
        // empty glyph
        *out_left = (uint8_t)w;
        *out_width = 0;
    }
    else
    {
        *out_left = (uint8_t)left;
        *out_width = (uint8_t)((uint32_t)(right - (int32_t)left + 1));
    }
}

// ktext.c  (add near other static helpers)

static inline void plot(int x, int y, kcolor c, uint8_t a)
{
    kgfx_put_px_blend(x, y, c, a);
}

// Draw one glyph using tight bounds, scaled, at (x,y)
static void draw_glyph_tight_scaled(
    const kfont *f, uint32_t gi,
    int x, int y, uint32_t scale,
    kcolor col, uint8_t alpha)
{
    const uint32_t scale_tenths = ktext_scale_to_tenths(scale);

    if (!f || gi >= f->glyph_count || scale == 0)
        return;
    uint8_t left = f->tight_left[gi];
    uint8_t tw = f->tight_width[gi];
    if (tw == 0 || left >= f->w)
        return;

    const uint32_t row_bytes = (f->w + 7) >> 3;
    const uint8_t *g = f->glyphs + (uint64_t)gi * f->bytes_per_glyph;

    for (uint32_t row = 0; row < f->h; ++row)
    {
        int py0 = y + (int)(((uint64_t)row * scale_tenths) / KTEXT_SCALE_BASE);
        int py1 = y + (int)((((uint64_t)row + 1u) * scale_tenths) / KTEXT_SCALE_BASE);

        if (py1 <= py0)
            py1 = py0 + 1;

        const uint8_t *rowbits = g + row * row_bytes;
        for (uint32_t bit = 0; bit < tw; ++bit)
        {
            uint32_t xcol = (uint32_t)left + bit;
            uint8_t b = rowbits[xcol >> 3];
            uint8_t m = (uint8_t)(1u << (7 - (xcol & 7)));
            if (b & m)
            {
                int px0 = x + (int)(((uint64_t)bit * scale_tenths) / KTEXT_SCALE_BASE);
                int px1 = x + (int)((((uint64_t)bit + 1u) * scale_tenths) / KTEXT_SCALE_BASE);
                if (px1 <= px0)
                    px1 = px0 + 1;

                for (int py = py0; py < py1; ++py)
                    for (int px = px0; px < px1; ++px)
                        plot(px, py, col, alpha);
            }
        }
    }
}

// For a given radius 't', iterate a disk of offsets (dx,dy) with dx^2+dy^2 <= t^2.
// Calls 'fn(dx,dy)' for each integer offset, skipping (0,0).
static void foreach_outline_offset(uint32_t t, void (*fn)(int dx, int dy, void *), void *ctx)
{
    if (t == 0 || !fn)
        return;
    int r = (int)t;
    int r2 = r * r;
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            if (dx * dx + dy * dy <= r2)
                fn(dx, dy, ctx);
        }
    }
}

/* ------------ public load (PSF2 or PSF1) -------------- */
int ktext_load_psf_file(const char *path, kfont *out, void **out_blob, uint32_t *out_size)
{
    if (!path || !out || !out_blob || !out_size)
        return -1;

    void *blob = 0;
    uint32_t sz = 0;
    if (read_all_file(path, &blob, &sz) != 0)
        return -1;
    if (sz < 4)
        return -1;

    const uint8_t *p = (const uint8_t *)blob;

    /* Try PSF2 first */
    if (sz >= sizeof(psf2_hdr))
    {
        const psf2_hdr *h2 = (const psf2_hdr *)p;
        if (h2->magic == 0x864AB572u)
        {
            if (h2->headersize > sz)
                return -1;
            uint64_t need = (uint64_t)h2->headersize +
                            (uint64_t)h2->glyph_count * (uint64_t)h2->bytes_per_glyph;
            if (need > sz)
                return -1;
            if (h2->width == 0 || h2->height == 0)
                return -1;

            out->glyphs = p + h2->headersize;
            out->glyph_count = h2->glyph_count;
            out->bytes_per_glyph = h2->bytes_per_glyph;
            out->w = h2->width;
            out->h = h2->height;

            /* allocate tight metrics (left + width) */
            uint64_t meta_bytes = (uint64_t)out->glyph_count * 2u;
            uint64_t meta_pages = (meta_bytes + 4095) >> 12;
            uint8_t *meta = (uint8_t *)pmem_alloc_pages(meta_pages);
            if (!meta)
                return -1;

            out->tight_left = meta;
            out->tight_width = meta + out->glyph_count;

            const uint32_t row_bytes = (out->w + 7) >> 3;
            for (uint32_t gi = 0; gi < out->glyph_count; ++gi)
            {
                const uint8_t *g = out->glyphs + (uint64_t)gi * out->bytes_per_glyph;
                psf2_glyph_tight_bounds(g, out->w, out->h, row_bytes,
                                        (uint8_t *)&out->tight_left[gi],
                                        (uint8_t *)&out->tight_width[gi]);
            }

            /* space advance */
            uint8_t sp_adv = (uint8_t)(out->w / 2);
            if ((' ' < out->glyph_count) && out->tight_width[' '] != 0)
                sp_adv = out->tight_width[' '];
            out->space_advance = sp_adv;

            *out_blob = blob;
            *out_size = sz;
            return 0;
        }
    }

    /* Try PSF1 (fixed width = 8px) */
    if (sz >= sizeof(psf1_hdr))
    {
        const psf1_hdr *h1 = (const psf1_hdr *)p;
        if (h1->magic == PSF1_MAGIC)
        {
            uint32_t glyph_count = (h1->mode & PSF1_MODE512) ? 512u : 256u;
            uint32_t bpg = (uint32_t)h1->charsize; // bytes per glyph
            uint32_t headersize = (uint32_t)sizeof(psf1_hdr);

            uint64_t need = (uint64_t)headersize + (uint64_t)glyph_count * (uint64_t)bpg;
            if (need > sz)
                return -1;

            out->glyphs = p + headersize;
            out->glyph_count = glyph_count;
            out->bytes_per_glyph = bpg;
            out->w = 8;             // PSF1 is always 8 pixels wide
            out->h = (uint32_t)bpg; // height in rows

            /* allocate tight metrics */
            uint64_t meta_bytes = (uint64_t)out->glyph_count * 2u;
            uint64_t meta_pages = (meta_bytes + 4095) >> 12;
            uint8_t *meta = (uint8_t *)pmem_alloc_pages(meta_pages);
            if (!meta)
                return -1;

            out->tight_left = meta;
            out->tight_width = meta + out->glyph_count;

            const uint32_t row_bytes = 1; // 8 pixels wide → 1 byte per row
            for (uint32_t gi = 0; gi < out->glyph_count; ++gi)
            {
                const uint8_t *g = out->glyphs + (uint64_t)gi * out->bytes_per_glyph;
                psf2_glyph_tight_bounds(g, out->w, out->h, row_bytes,
                                        (uint8_t *)&out->tight_left[gi],
                                        (uint8_t *)&out->tight_width[gi]);
            }

            uint8_t sp_adv = 4; // 8/2 default for PSF1
            if ((' ' < out->glyph_count) && out->tight_width[' '] != 0)
                sp_adv = out->tight_width[' '];
            out->space_advance = sp_adv;

            *out_blob = blob;
            *out_size = sz;
            return 0;
        }
    }

    return -1; // unknown format
}

/* ------------ drawing -------------- */

uint32_t ktext_line_height(const kfont *f, uint32_t scale, int line_spacing)
{
    uint32_t glyph_h = 0;

    if (!f || scale == 0)
        return 0;
    glyph_h = ktext_scale_mul_px(f->h, scale);
    int lh = (int)glyph_h + line_spacing;
    return (lh > 0) ? (uint32_t)lh : 0;
}

uint32_t ktext_measure_line_px(const kfont *f, const char *s, uint32_t scale, int char_spacing)
{
    if (!f || !s || scale == 0)
        return 0;
    uint32_t row_bytes = (f->w + 7) >> 3;
    (void)row_bytes;

    uint32_t adv_sum = 0;
    const unsigned char *p = (const unsigned char *)s;
    int first = 1;

    while (*p && *p != '\n')
    {
        uint32_t gi = (uint32_t)(*p++);
        uint32_t adv;
        if (gi < f->glyph_count && f->tight_width[gi] != 0)
        {
            // variable width
            adv = (uint32_t)f->tight_width[gi];
        }
        else if (gi == ' ')
        {
            adv = f->space_advance;
        }
        else
        {
            // fallback to full cell width
            adv = f->w;
        }
        adv_sum += ktext_scale_mul_px(adv, scale);
        if (!first)
        {
            // apply spacing between characters (for N chars, we add N-1 spacings)
            adv_sum += (char_spacing > 0) ? (uint32_t)char_spacing : 0;
        }
        first = 0;
    }
    return adv_sum;
}

void ktext_draw_str_ex(const kfont *f, int x, int y, const char *s,
                       kcolor col, uint8_t alpha, uint32_t scale,
                       int char_spacing, int line_spacing)
{
    uint32_t font_h_scaled = 0;

    if (!f || !s || !f->glyphs || f->w == 0 || f->h == 0 || scale == 0)
        return;
    font_h_scaled = ktext_scale_mul_px(f->h, scale);
    const int start_x = x;

    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if (*p == '\n')
        {
            // newline: move to next baseline
            x = start_x;
            y += (int)font_h_scaled + line_spacing;
            continue;
        }

        uint32_t gi = (uint32_t)(*p);
        uint8_t left = f->w; // default for empty
        uint8_t tw = 0;

        if (gi < f->glyph_count)
        {
            left = f->tight_left[gi];
            tw = f->tight_width[gi];
        }

        // choose advance and draw range
        uint32_t adv;
        uint32_t draw_cols = 0;

        if (tw != 0 && left < f->w)
        {
            // normal glyph: draw only [left, left+tw)
            adv = tw;
            draw_cols = tw;
        }
        else if (gi == ' ')
        {
            // space: no draw, just advance
            adv = f->space_advance;
            draw_cols = 0;
        }
        else
        {
            // empty/unknown: use full cell width
            adv = f->w;
            draw_cols = 0; // draw nothing; you can choose to draw full box by setting = f->w;
        }

        // draw bitmap columns in the tight range
        if (draw_cols)
        {
            draw_glyph_tight_scaled(f, gi, x, y, scale, col, alpha);
        }

        // advance pen
        x += (int)ktext_scale_mul_px(adv, scale);
        // inter-character spacing (between rendered chars)
        x += char_spacing;
    }
}

void ktext_draw_str_align(const kfont *f, int anchor_x, int y, const char *s,
                          kcolor col, uint8_t alpha, uint32_t scale,
                          int char_spacing, int line_spacing, ktext_align align)
{
    uint32_t font_h_scaled = 0;

    if (!f || !s || !f->glyphs || f->w == 0 || f->h == 0 || scale == 0)
        return;
    font_h_scaled = ktext_scale_mul_px(f->h, scale);

    const unsigned char *p = (const unsigned char *)s;
    while (*p)
    {
        // 1) measure this line (up to '\n' or '\0')
        uint32_t line_w = ktext_measure_line_px(f, (const char *)p, scale, char_spacing);

        // 2) choose the starting x based on alignment
        int x;
        switch (align)
        {
        case KTEXT_ALIGN_CENTER:
            x = anchor_x - (int)(line_w / 2);
            break;
        case KTEXT_ALIGN_RIGHT:
            x = anchor_x - (int)line_w;
            break;
        default:
            x = anchor_x;
            break;
        }

        // 3) draw chars of this line (stop at '\n' or '\0')
        for (; *p && *p != '\n'; ++p)
        {
            uint32_t gi = (uint32_t)(*p);
            uint8_t left = f->w;
            uint8_t tw = 0;

            if (gi < f->glyph_count)
            {
                left = f->tight_left[gi];
                tw = f->tight_width[gi];
            }

            uint32_t adv;
            uint32_t draw_cols = 0;

            if (tw != 0 && left < f->w)
            {
                adv = tw;
                draw_cols = tw;
            }
            else if (gi == ' ')
            {
                adv = f->space_advance;
                draw_cols = 0;
            }
            else
            {
                adv = f->w;
                draw_cols = 0;
            }

            if (draw_cols)
            {
                draw_glyph_tight_scaled(f, gi, x, y, scale, col, alpha);
            }

            x += (int)ktext_scale_mul_px(adv, scale);
            x += char_spacing;
        }

        // 4) if we stopped on newline, move to next baseline
        if (*p == '\n')
        {
            ++p; // skip '\n'
            y += (int)font_h_scaled + line_spacing;
        }
    }
}

typedef struct
{
    const kfont *f;
    uint32_t gi;
    int x, y;
    uint32_t scale;
    kcolor col;
    uint8_t a;
} _glyph_draw_ctx;

static void _outline_drawer(int dx, int dy, void *vctx)
{
    _glyph_draw_ctx *c = (_glyph_draw_ctx *)vctx;
    draw_glyph_tight_scaled(c->f, c->gi, c->x + dx, c->y + dy, c->scale, c->col, c->a);
}

void ktext_draw_str_align_outline(
    const kfont *f,
    int anchor_x, int y,
    const char *s,
    kcolor fill, uint8_t fill_alpha,
    uint32_t scale,
    int char_spacing, int line_spacing,
    ktext_align align,
    uint32_t outline_width, kcolor outline, uint8_t outline_alpha)
{
    if (!f || !s || !f->glyphs || f->w == 0 || f->h == 0 || scale == 0)
        return;

    uint32_t font_h_scaled = ktext_scale_mul_px(f->h, scale);
    const unsigned char *p = (const unsigned char *)s;

    while (*p)
    {
        // 1) measure this line (stop at '\n' or '\0') for alignment
        uint32_t line_w = ktext_measure_line_px(f, (const char *)p, scale, char_spacing);

        // 2) choose starting x from alignment
        int x;
        switch (align)
        {
        case KTEXT_ALIGN_CENTER:
            x = anchor_x - (int)(line_w / 2);
            break;
        case KTEXT_ALIGN_RIGHT:
            x = anchor_x - (int)line_w;
            break;
        default:
            x = anchor_x;
            break;
        }

        // 3) draw glyphs in this line
        for (; *p && *p != '\n'; ++p)
        {
            uint32_t gi = (uint32_t)(*p);
            uint8_t left = f->w, tw = 0;

            if (gi < f->glyph_count)
            {
                left = f->tight_left[gi];
                tw = f->tight_width[gi];
            }

            // choose advance
            uint32_t adv;
            if (tw != 0 && left < f->w)
            {
                adv = tw;
            }
            else if (gi == ' ')
            {
                adv = f->space_advance;
            }
            else
            {
                adv = f->w;
            }

            // Outline pass (outside): render the glyph at a disk of offsets, then fill on top.
            if (outline_width && tw != 0 && left < f->w && outline_alpha)
            {
                _glyph_draw_ctx ctx = {f, gi, x, y, scale, outline, outline_alpha};
                foreach_outline_offset(outline_width, _outline_drawer, &ctx);
            }

            // Fill pass
            if (tw != 0 && left < f->w)
            {
                draw_glyph_tight_scaled(f, gi, x, y, scale, fill, fill_alpha);
            }
            // spaces/empty just advance

            // advance pen
            x += (int)ktext_scale_mul_px(adv, scale);
            x += char_spacing;
        }

        // 4) newline?
        if (*p == '\n')
        {
            ++p;
            y += (int)font_h_scaled + line_spacing;
        }
    }
}
