#pragma once
#include <stdint.h>

/*
 * Optional colour-font fallback used by ktext.  The configured asset is one
 * SFNT file (either .ttf or a face in a .ttc); glyphs are addressed by Unicode
 * code point.  No individual glyph files are required.
 */

#ifdef __cplusplus
extern "C" {
#endif

int kemoji_font_set_path(const char *path, uint32_t face_index);
int kemoji_font_has_glyph(uint32_t codepoint);
uint32_t kemoji_font_advance(uint32_t codepoint, uint32_t height_px);
void kemoji_font_draw(uint32_t codepoint, int x, int y, uint32_t height_px, uint8_t alpha);

#ifdef __cplusplus
}
#endif
