#pragma once
#include <stdint.h>
#include "kwrappers/kfile.h"
#include "kwrappers/kui_types.h" // kcolor, ktext_align, struct kfont fwd
#include "memory/pmem.h"

// Inline color tag control byte (not typeable from normal keyboard input):
//   \x1F<RRGGBB>  -> set current text fill color
//   \x1F.         -> reset to the base fill color passed to draw call
#define KTEXT_INLINE_COLOR_CTRL ((char)0x1F)

#ifdef __cplusplus
extern "C"
{
#endif

    /* Full definition + typedef of kfont lives HERE */
    typedef struct kfont
    {
        const uint8_t *glyphs;
        uint32_t glyph_count;
        uint32_t bytes_per_glyph;
        uint32_t w, h;

        /* variable-width metrics */
        const uint8_t *tight_left;
        const uint8_t *tight_width;
        uint8_t space_advance;
    } kfont;

    /* --- Drawing API (uses ktext_align from kui_types.h) --- */

    // Draw with alignment: anchor_x is interpreted as
    // - left edge (LEFT), visual center (CENTER), or right edge (RIGHT).
    void ktext_draw_str_align(const kfont *f, int anchor_x, int y, const char *s,
                              kcolor col, uint8_t alpha, uint32_t scale,
                              int char_spacing, int line_spacing, ktext_align align);

    void ktext_draw_str_align_outline(
        const kfont *f,
        int anchor_x, int y,
        const char *s,
        kcolor fill, uint8_t fill_alpha,
        uint32_t scale,
        int char_spacing, int line_spacing,
        ktext_align align,
        uint32_t outline_width, kcolor outline, uint8_t outline_alpha);

    // Load PSF1/PSF2 font and precompute tight metrics.
    // out_blob points to the raw font buffer you must keep alive (pmem); out_size returns its size.
    int ktext_load_psf_file(const char *path, kfont *out, void **out_blob, uint32_t *out_size);

    // Multiline draw with scaling, alpha, char- & line-spacing.
    void ktext_draw_str_ex(const kfont *f, int x, int y, const char *s,
                           kcolor col, uint8_t alpha, uint32_t scale,
                           int char_spacing, int line_spacing);

    // Convenience wrapper (no extra spacing)
    static inline void ktext_draw_str(const kfont *f, int x, int y, const char *s,
                                      kcolor col, uint8_t alpha, uint32_t scale)
    {
        ktext_draw_str_ex(f, x, y, s, col, alpha, scale, 0, 0);
    }

    // Optional: text metrics for layout
    // Scale steps are fine-grained:
    //   scale 1 -> 1.0x, scale 2 -> 1.1x, scale 3 -> 1.2x, etc.
    uint32_t ktext_scale_mul_px(uint32_t px, uint32_t scale);
    uint32_t ktext_line_height(const kfont *f, uint32_t scale, int line_spacing);
    uint32_t ktext_measure_line_px(const kfont *f, const char *s, uint32_t scale, int char_spacing);

#ifdef __cplusplus
}
#endif
