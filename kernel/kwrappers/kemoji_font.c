#include <stdint.h>
#include "kwrappers/kemoji_font.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kgfx.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"

#define KEMOJI_PATH_MAX 192u
#define KEMOJI_CACHE_MAX 96u

typedef struct { uint32_t cp, glyph; kimg image; uint8_t state; } kemoji_cache_entry;
typedef struct {
    uint8_t *data;
    uint32_t size, cmap, sbix, glyph_count, sbix_length;
    uint32_t cblc, cbdt, cblc_length, cbdt_length;
    uint8_t loaded;
} kemoji_font_state;

static char G_path[KEMOJI_PATH_MAX] = "0:/OS/System/Fonts/Emoji.ttf";
static uint32_t G_face_index = 0u;
static kemoji_font_state G_font;
static kemoji_cache_entry G_cache[KEMOJI_CACHE_MAX];
static uint8_t G_reported_load;
static uint8_t G_reported_glyph;

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int range_ok(uint32_t off, uint32_t len, uint32_t size)
{
    return off <= size && len <= size - off;
}

static int read_all(const char *path, uint8_t **out, uint32_t *out_size)
{
    KFile f;
    uint64_t size;
    uint8_t *data;
    uint32_t done = 0u;
    if (!path || !out || !out_size || kfile_open(&f, path, KFILE_READ) != 0)
        return -1;
    size = kfile_size(&f);
    if (!size || size > 0x7fffffffu) { kfile_close(&f); return -1; }
    data = (uint8_t *)pmem_alloc_pages((size + 4095u) >> 12);
    if (!data) { kfile_close(&f); return -1; }
    while (done < (uint32_t)size) {
        uint32_t got = 0u, want = (uint32_t)size - done;
        if (want > 65536u) want = 65536u;
        if (kfile_read(&f, data + done, want, &got) != 0 || got != want) { kfile_close(&f); return -1; }
        done += got;
    }
    kfile_close(&f);
    *out = data; *out_size = (uint32_t)size;
    return 0;
}

static int find_table(uint32_t face, const char tag[4], uint32_t *out_off, uint32_t *out_len)
{
    uint16_t count;
    if (!range_ok(face, 12u, G_font.size)) return -1;
    count = be16(G_font.data + face + 4u);
    if (!range_ok(face + 12u, (uint32_t)count * 16u, G_font.size)) return -1;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = G_font.data + face + 12u + i * 16u;
        uint32_t off = be32(r + 8u), len = be32(r + 12u);
        if (r[0] == (uint8_t)tag[0] && r[1] == (uint8_t)tag[1] && r[2] == (uint8_t)tag[2] && r[3] == (uint8_t)tag[3] && range_ok(off, len, G_font.size)) {
            *out_off = off; *out_len = len; return 0;
        }
    }
    return -1;
}

static int load_font(void)
{
    uint32_t face = 0u, cmap_len = 0u, maxp = 0u, maxp_len = 0u;
    if (G_font.loaded) return 0;
    if (read_all(G_path, &G_font.data, &G_font.size) != 0) {
        if (!G_reported_load) { terminal_warn("emoji font: could not read configured font file"); G_reported_load = 1u; }
        return -1;
    }
    if (G_font.size >= 12u && G_font.data[0] == 't' && G_font.data[1] == 't' && G_font.data[2] == 'c' && G_font.data[3] == 'f') {
        uint32_t count = be32(G_font.data + 8u);
        if (G_face_index >= count || !range_ok(12u, count * 4u, G_font.size)) return -1;
        face = be32(G_font.data + 12u + G_face_index * 4u);
    }
    if (find_table(face, "cmap", &G_font.cmap, &cmap_len) != 0 ||
        find_table(face, "maxp", &maxp, &maxp_len) != 0 || maxp_len < 6u)
        return -1;
    (void)find_table(face, "sbix", &G_font.sbix, &G_font.sbix_length);
    (void)find_table(face, "CBLC", &G_font.cblc, &G_font.cblc_length);
    (void)find_table(face, "CBDT", &G_font.cbdt, &G_font.cbdt_length);
    G_font.glyph_count = be16(G_font.data + maxp + 4u);
    if (!G_font.glyph_count || cmap_len < 4u ||
        (G_font.sbix_length < 12u && (G_font.cblc_length < 8u || G_font.cbdt_length < 4u))) return -1;
    G_font.loaded = 1u;
    if (!G_reported_load) { terminal_success(G_font.cblc_length ? "emoji font: cbdt/cblc ready" : "emoji font: sbix ready"); G_reported_load = 1u; }
    return 0;
}

static uint32_t glyph_for(uint32_t cp)
{
    uint16_t count;
    if (load_font() != 0) return 0u;
    count = be16(G_font.data + G_font.cmap + 2u);
    if (!range_ok(G_font.cmap + 4u, (uint32_t)count * 8u, G_font.size)) return 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *rec = G_font.data + G_font.cmap + 4u + i * 8u;
        uint32_t sub = G_font.cmap + be32(rec + 4u);
        if (!range_ok(sub, 16u, G_font.size) || be16(G_font.data + sub) != 12u) continue;
        uint32_t groups = be32(G_font.data + sub + 12u);
        if (!range_ok(sub + 16u, groups * 12u, G_font.size)) continue;
        for (uint32_t g = 0u; g < groups; ++g) {
            const uint8_t *group = G_font.data + sub + 16u + g * 12u;
            uint32_t first = be32(group), last = be32(group + 4u);
            if (cp >= first && cp <= last) return be32(group + 8u) + cp - first;
        }
    }
    return 0u;
}

static kemoji_cache_entry *cache_for(uint32_t cp)
{
    kemoji_cache_entry *free_slot = 0;
    /* The filesystem may not be mounted during early console output.  Do not
       cache that transient failure: a later text draw can then load the font. */
    if (load_font() != 0) return 0;
    for (uint32_t i = 0u; i < KEMOJI_CACHE_MAX; ++i) {
        if (G_cache[i].state && G_cache[i].cp == cp) return &G_cache[i];
        if (!G_cache[i].state && !free_slot) free_slot = &G_cache[i];
    }
    if (!free_slot) return 0;
    free_slot->cp = cp;
    free_slot->glyph = glyph_for(cp);
    free_slot->state = 1u;
    if (!free_slot->glyph || free_slot->glyph >= G_font.glyph_count) return free_slot;
    if (G_font.sbix_length >= 12u) {
        const uint8_t *table = G_font.data + G_font.sbix;
        uint32_t strikes = be32(table + 4u), chosen = 0u;
        if (!range_ok(G_font.sbix + 8u, strikes * 4u, G_font.size) || !strikes) return free_slot;
        chosen = be32(table + 8u + (strikes - 1u) * 4u); /* use the highest-resolution embedded strike. */
        if (!range_ok(G_font.sbix + chosen, 4u + (G_font.glyph_count + 1u) * 4u, G_font.size)) return free_slot;
        const uint8_t *strike = table + chosen;
        uint32_t a = be32(strike + 4u + free_slot->glyph * 4u);
        uint32_t b = be32(strike + 8u + free_slot->glyph * 4u);
        if (b <= a || !range_ok(G_font.sbix + chosen + a, b - a, G_font.size) || b - a <= 8u) return free_slot;
        const uint8_t *glyph = strike + a;
        if ((glyph[4] == 'p' && glyph[5] == 'n' && glyph[6] == 'g' && glyph[7] == ' ') ||
            (glyph[4] == 'j' && glyph[5] == 'p' && glyph[6] == 'g' && glyph[7] == ' '))
            (void)kimg_load_memory(&free_slot->image, glyph + 8u, b - a - 8u);
    } else {
        const uint8_t *cblc = G_font.data + G_font.cblc;
        uint32_t strikes = be32(cblc + 4u);
        uint32_t strike, array_off, sub_count;
        if (!strikes || !range_ok(G_font.cblc + 8u, strikes * 48u, G_font.size)) return free_slot;
        strike = G_font.cblc + 8u + (strikes - 1u) * 48u; /* highest ppem strike */
        array_off = be32(G_font.data + strike);
        sub_count = be32(G_font.data + strike + 8u);
        if (!range_ok(G_font.cblc + array_off, sub_count * 8u, G_font.size)) return free_slot;
        for (uint32_t i = 0u; i < sub_count; ++i) {
            const uint8_t *entry = G_font.data + G_font.cblc + array_off + i * 8u;
            uint32_t first = be16(entry), last = be16(entry + 2u);
            uint32_t sub = G_font.cblc + array_off + be32(entry + 4u);
            uint32_t a, b, image_off, image_format;
            const uint8_t *image;
            uint32_t png_len;
            if (free_slot->glyph < first || free_slot->glyph > last || !range_ok(sub, 12u, G_font.size)) continue;
            if (be16(G_font.data + sub) != 1u) continue; /* index subtable format 1 */
            image_format = be16(G_font.data + sub + 2u);
            image_off = be32(G_font.data + sub + 4u);
            a = be32(G_font.data + sub + 8u + (free_slot->glyph - first) * 4u);
            b = be32(G_font.data + sub + 12u + (free_slot->glyph - first) * 4u);
            /* CBLC imageDataOffset and its glyph offsets are CBDT-table-relative. */
            if (b <= a || image_off > G_font.cbdt_length || a > G_font.cbdt_length - image_off ||
                b - a > G_font.cbdt_length - image_off - a) break;
            image = G_font.data + G_font.cbdt + image_off + a;
            if (image_format != 17u || b - a < 9u) break; /* small metrics + PNG */
            png_len = be32(image + 5u);
            if (png_len > b - a - 9u || !range_ok((uint32_t)(image + 9u - G_font.data), png_len, G_font.size)) break;
            (void)kimg_load_memory(&free_slot->image, image + 9u, png_len);
            break;
        }
    }
    if (!G_reported_glyph && cp == 0x1f940u) {
        G_reported_glyph = 1u;
        if (free_slot->image.px && free_slot->image.w && free_slot->image.h)
            terminal_success("emoji font: u+1f940 decoded");
        else
            terminal_warn("emoji font: u+1f940 decode failed");
    }
    return free_slot;
}

int kemoji_font_set_path(const char *path, uint32_t face_index)
{
    uint32_t n = 0u;
    if (!path) return -1;
    while (path[n] && n + 1u < KEMOJI_PATH_MAX) { G_path[n] = path[n]; ++n; }
    if (path[n]) return -1;
    G_path[n] = 0; G_face_index = face_index; G_font = (kemoji_font_state){0};
    for (uint32_t i = 0u; i < KEMOJI_CACHE_MAX; ++i) G_cache[i] = (kemoji_cache_entry){0};
    return 0;
}

int kemoji_font_has_glyph(uint32_t codepoint)
{
    kemoji_cache_entry *entry = cache_for(codepoint);
    return entry && entry->image.px && entry->image.w && entry->image.h;
}

uint32_t kemoji_font_advance(uint32_t codepoint, uint32_t height_px)
{
    kemoji_cache_entry *entry = cache_for(codepoint);
    if (!entry || !entry->image.px || !entry->image.h) return 0u;
    uint32_t width = (uint32_t)(((uint64_t)entry->image.w * height_px + entry->image.h / 2u) / entry->image.h);
    return width ? width : 1u;
}

void kemoji_font_draw(uint32_t codepoint, int x, int y, uint32_t height_px, uint8_t alpha)
{
    kemoji_cache_entry *entry = cache_for(codepoint);
    uint32_t width;
    if (!entry || !entry->image.px || !entry->image.w || !entry->image.h || !height_px) return;
    width = kemoji_font_advance(codepoint, height_px);
    for (uint32_t dy = 0u; dy < height_px; ++dy) for (uint32_t dx = 0u; dx < width; ++dx) {
        uint32_t src = entry->image.px[(uint64_t)(dy * entry->image.h / height_px) * entry->image.w + (dx * entry->image.w / width)];
        uint8_t a = (uint8_t)(((uint32_t)(src >> 24) * alpha + 127u) / 255u);
        kcolor c = {(uint8_t)(src >> 16), (uint8_t)(src >> 8), (uint8_t)src};
        if (a) kgfx_put_px_blend(x + (int)dx, y + (int)dy, c, a);
    }
}
