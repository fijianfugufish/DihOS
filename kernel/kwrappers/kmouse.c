#include "kwrappers/kmouse.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kinput.h"

typedef struct
{
    kimg img;
    int32_t hot_x;
    int32_t hot_y;
    uint8_t loaded;
} kmouse_cursor_asset_t;

typedef struct
{
    kmouse_state state;
    kgfx_obj_handle cursor_handle;
    kmouse_cursor active_cursor;
    kmouse_cursor_asset_t cursors[KMOUSE_CURSOR_COUNT];
    uint32_t sensitivity_pct;
    uint8_t initialized;
    uint8_t has_cursor;
} kmouse_ctx_t;

static kmouse_ctx_t G;

#define KMOUSE_CURSOR_BASE_PATH "0:/OS/System/Images/Mouse/"
#define KMOUSE_CURSOR_SCALE_PCT 150u

static const char *kmouse_cursor_file_name(kmouse_cursor cursor)
{
    switch (cursor)
    {
    case KMOUSE_CURSOR_ARROW:
        return "arrow.cur";
    case KMOUSE_CURSOR_BEAM:
        return "beam.cur";
    case KMOUSE_CURSOR_WAIT:
        return "wait.cur";
    case KMOUSE_CURSOR_SIZE3:
        return "size3.cur";
    case KMOUSE_CURSOR_SIZE1:
        return "size1.cur";
    case KMOUSE_CURSOR_SIZE2:
        return "size2.cur";
    case KMOUSE_CURSOR_SIZE4:
        return "size4.cur";
    case KMOUSE_CURSOR_NO:
        return "no.cur";
    case KMOUSE_CURSOR_CROSS:
        return "cross.cur";
    case KMOUSE_CURSOR_BUSY:
        return "busy.cur";
    case KMOUSE_CURSOR_MOVE:
        return "move.cur";
    case KMOUSE_CURSOR_LINK:
        return "link.cur";
    default:
        return 0;
    }
}

static uint8_t kmouse_cursor_needs_bw_invert(kmouse_cursor cursor)
{
    switch (cursor)
    {
    case KMOUSE_CURSOR_SIZE1:
    case KMOUSE_CURSOR_SIZE2:
    case KMOUSE_CURSOR_SIZE3:
    case KMOUSE_CURSOR_SIZE4:
        return 1u;
    default:
        return 0u;
    }
}

static void kmouse_invert_bw_cursor(kmouse_cursor_asset_t *asset)
{
    uint32_t count = 0;

    if (!asset || !asset->img.px || asset->img.w == 0u || asset->img.h == 0u)
        return;

    count = asset->img.w * asset->img.h;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t px = asset->img.px[i];
        uint32_t a = px & 0xFF000000u;
        uint32_t rgb = px & 0x00FFFFFFu;

        if (a == 0u)
            continue;

        if (rgb == 0x000000u)
            asset->img.px[i] = a | 0x00FFFFFFu;
        else if (rgb == 0x00FFFFFFu)
            asset->img.px[i] = a;
    }
}

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static inline uint32_t kmouse_scale_u32(uint32_t v)
{
    uint64_t scaled = ((uint64_t)v * (uint64_t)KMOUSE_CURSOR_SCALE_PCT + 50ull) / 100ull;
    if (v != 0u && scaled == 0ull)
        scaled = 1ull;
    if (scaled > 0xFFFFFFFFull)
        scaled = 0xFFFFFFFFull;
    return (uint32_t)scaled;
}

static inline int32_t kmouse_scale_i32(int32_t v)
{
    uint32_t mag = 0;
    int32_t scaled = 0;

    if (v == 0)
        return 0;

    mag = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    scaled = (int32_t)kmouse_scale_u32(mag);
    return (v < 0) ? -scaled : scaled;
}

static inline uint16_t kmouse_rd16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t kmouse_rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline uint32_t kmouse_abs_i32(int32_t v)
{
    return (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
}

static inline uint32_t kmouse_pack_argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint32_t kmouse_bpp_rank(uint16_t bpp)
{
    switch (bpp)
    {
    case 32u:
        return 5u;
    case 24u:
        return 4u;
    case 8u:
        return 3u;
    case 4u:
        return 2u;
    case 1u:
        return 1u;
    default:
        return 0u;
    }
}

static inline uint8_t kmouse_palette_index_at(const uint8_t *src, uint32_t x, uint16_t bpp)
{
    if (bpp == 8u)
        return src[x];
    if (bpp == 4u)
    {
        uint8_t byte = src[x >> 1u];
        return (x & 1u) ? (byte & 0x0Fu) : (byte >> 4u);
    }
    if (bpp == 1u)
    {
        uint8_t byte = src[x >> 3u];
        return (byte >> (7u - (x & 7u))) & 1u;
    }
    return 0u;
}

static inline char kmouse_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int kmouse_path_has_ext(const char *path, const char *ext)
{
    uint32_t path_len = 0;
    uint32_t ext_len = 0;

    if (!path || !ext)
        return 0;

    while (path[path_len])
        ++path_len;
    while (ext[ext_len])
        ++ext_len;

    if (ext_len == 0 || path_len < ext_len)
        return 0;

    for (uint32_t i = 0; i < ext_len; ++i)
    {
        char a = kmouse_ascii_lower(path[path_len - ext_len + i]);
        char b = kmouse_ascii_lower(ext[i]);
        if (a != b)
            return 0;
    }

    return 1;
}

static void kmouse_build_cursor_path(char *dst, uint32_t cap, const char *file_name)
{
    uint32_t n = 0;
    uint32_t i = 0;
    const char *base = KMOUSE_CURSOR_BASE_PATH;

    if (!dst || cap == 0)
        return;

    while (base[n] && n + 1u < cap)
    {
        dst[n] = base[n];
        ++n;
    }

    if (!file_name)
    {
        dst[n] = 0;
        return;
    }

    while (file_name[i] && n + 1u < cap)
    {
        dst[n++] = file_name[i++];
    }

    dst[n] = 0;
}

static kmouse_cursor_asset_t *kmouse_active_asset(void)
{
    if ((uint32_t)G.active_cursor < KMOUSE_CURSOR_COUNT &&
        G.cursors[(uint32_t)G.active_cursor].loaded)
        return &G.cursors[(uint32_t)G.active_cursor];

    for (uint32_t i = 0; i < KMOUSE_CURSOR_COUNT; ++i)
    {
        if (G.cursors[i].loaded)
            return &G.cursors[i];
    }

    return 0;
}

static void kmouse_apply_active_cursor_image(void)
{
    kgfx_obj *cursor_obj = 0;
    kmouse_cursor_asset_t *asset = kmouse_active_asset();
    uint32_t draw_w = 0;
    uint32_t draw_h = 0;

    if (!asset)
        return;

    cursor_obj = kgfx_obj_ref(G.cursor_handle);
    if (!cursor_obj || cursor_obj->kind != KGFX_OBJ_IMAGE)
        return;

    cursor_obj->u.image.argb = asset->img.px;
    cursor_obj->u.image.src_w = asset->img.w;
    cursor_obj->u.image.src_h = asset->img.h;
    draw_w = kmouse_scale_u32(asset->img.w);
    draw_h = kmouse_scale_u32(asset->img.h);
    cursor_obj->u.image.w = draw_w;
    cursor_obj->u.image.h = draw_h;
    cursor_obj->u.image.stride_px = asset->img.w;
}

static int kmouse_read_all_file(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    KFile f;
    uint64_t size64 = 0;
    uint32_t size = 0;
    uint64_t pages = 0;
    uint8_t *buf = 0;
    uint32_t total = 0;

    if (!path || !out_buf || !out_size)
        return -1;

    *out_buf = 0;
    *out_size = 0;

    if (kfile_open(&f, path, KFILE_READ) != 0)
        return -1;

    size64 = kfile_size(&f);
    if (size64 == 0 || size64 > 0xFFFFFFFFull)
    {
        kfile_close(&f);
        return -1;
    }

    size = (uint32_t)size64;
    pages = ((uint64_t)size + 4095ull) >> 12;
    buf = (uint8_t *)pmem_alloc_pages(pages);
    if (!buf)
    {
        kfile_close(&f);
        return -1;
    }

    while (total < size)
    {
        uint32_t got = 0;
        uint32_t want = size - total;

        if (want > 4096u)
            want = 4096u;

        if (kfile_read(&f, buf + total, want, &got) != 0 || got == 0)
        {
            kfile_close(&f);
            return -1;
        }

        total += got;
    }

    kfile_close(&f);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int kmouse_is_png_blob(const uint8_t *p, uint32_t n)
{
    static const uint8_t sig[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    if (!p || n < 8u)
        return 0;
    for (uint32_t i = 0; i < 8u; ++i)
        if (p[i] != sig[i])
            return 0;
    return 1;
}

static int kmouse_load_cur(kimg *out, const char *path, int32_t *hot_x, int32_t *hot_y)
{
    uint8_t *blob = 0;
    uint32_t blob_size = 0;
    uint16_t count = 0;
    int32_t best_idx = -1;
    uint64_t best_score = 0;
    uint8_t *img = 0;
    uint32_t img_bytes = 0;
    uint32_t dib_size = 0;
    int32_t dib_h_signed = 0;
    uint32_t width = 0;
    uint32_t height_total = 0;
    uint32_t height = 0;
    uint16_t bpp = 0;
    uint32_t comp = 0;
    uint32_t clr_used = 0;
    uint32_t palette_entries = 0;
    uint64_t palette_bytes = 0;
    uint64_t xor_stride = 0;
    uint64_t xor_bytes = 0;
    uint64_t and_stride = 0;
    uint64_t and_bytes = 0;
    uint64_t min_needed = 0;
    uint8_t *palette = 0;
    uint8_t *xor_base = 0;
    uint8_t *and_base = 0;
    uint8_t has_and = 0;
    uint8_t has_alpha = 0;
    uint64_t px_count = 0;
    uint64_t px_bytes = 0;
    uint64_t px_pages = 0;
    uint32_t *dst = 0;
    uint32_t selected_hot_x = 0;
    uint32_t selected_hot_y = 0;
    uint8_t top_down = 0;
    const uint32_t preferred_size = 32u;

    if (!out || !path || !hot_x || !hot_y)
        return -1;

    out->px = 0;
    out->w = 0;
    out->h = 0;
    *hot_x = 0;
    *hot_y = 0;

    if (kmouse_read_all_file(path, &blob, &blob_size) != 0)
        return -1;

    if (blob_size < 6u)
        return -1;
    if (kmouse_rd16le(blob + 0) != 0u)
        return -1;
    if (kmouse_rd16le(blob + 2) != 2u)
        return -1;

    count = kmouse_rd16le(blob + 4);
    if (count == 0u)
        return -1;
    if (6u + (uint32_t)count * 16u > blob_size)
        return -1;

    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *ent = blob + 6u + i * 16u;
        uint32_t bytes_in_res = kmouse_rd32le(ent + 8);
        uint32_t image_offset = kmouse_rd32le(ent + 12);
        uint8_t *candidate_img = 0;
        uint32_t candidate_dib_size = 0;
        uint16_t candidate_bpp = 0;
        uint32_t candidate_comp = 0;
        uint32_t candidate_w = 0;
        uint32_t candidate_h_total = 0;
        uint32_t candidate_h = 0;
        uint32_t bpp_rank = 0;
        uint32_t size_penalty = 0;
        uint64_t score = 0;

        if (bytes_in_res == 0u || image_offset >= blob_size || bytes_in_res > blob_size - image_offset)
            continue;

        candidate_img = blob + image_offset;
        if (kmouse_is_png_blob(candidate_img, bytes_in_res))
            continue;
        if (bytes_in_res < 40u)
            continue;

        candidate_dib_size = kmouse_rd32le(candidate_img + 0);
        if (candidate_dib_size < 40u || candidate_dib_size > bytes_in_res)
            continue;

        candidate_bpp = kmouse_rd16le(candidate_img + 14);
        candidate_comp = kmouse_rd32le(candidate_img + 16);
        if (candidate_comp != 0u)
            continue;
        if (kmouse_bpp_rank(candidate_bpp) == 0u)
            continue;

        candidate_w = kmouse_abs_i32((int32_t)kmouse_rd32le(candidate_img + 4));
        candidate_h_total = kmouse_abs_i32((int32_t)kmouse_rd32le(candidate_img + 8));
        candidate_h = (candidate_h_total >= 2u) ? (candidate_h_total / 2u) : candidate_h_total;

        if (candidate_w == 0u || candidate_h == 0u)
            continue;

        bpp_rank = kmouse_bpp_rank(candidate_bpp);
        size_penalty = ((candidate_w > preferred_size) ? (candidate_w - preferred_size) : (preferred_size - candidate_w)) +
                       ((candidate_h > preferred_size) ? (candidate_h - preferred_size) : (preferred_size - candidate_h));
        if (size_penalty > 0xFFFFu)
            size_penalty = 0xFFFFu;
        score = ((uint64_t)bpp_rank << 56) +
                ((uint64_t)(0xFFFFu - size_penalty) << 40) +
                ((uint64_t)candidate_w * (uint64_t)candidate_h);
        if (best_idx < 0 || score > best_score)
        {
            best_idx = (int32_t)i;
            best_score = score;
        }
    }

    if (best_idx < 0)
        return -1;

    {
        const uint8_t *ent = blob + 6u + (uint32_t)best_idx * 16u;
        uint32_t bytes_in_res = kmouse_rd32le(ent + 8);
        uint32_t image_offset = kmouse_rd32le(ent + 12);

        selected_hot_x = kmouse_rd16le(ent + 4);
        selected_hot_y = kmouse_rd16le(ent + 6);

        if (image_offset >= blob_size || bytes_in_res == 0u || bytes_in_res > blob_size - image_offset)
            return -1;

        img = blob + image_offset;
        img_bytes = bytes_in_res;
    }

    if (img_bytes < 40u || kmouse_is_png_blob(img, img_bytes))
        return -1;

    dib_size = kmouse_rd32le(img + 0);
    if (dib_size < 40u || dib_size > img_bytes)
        return -1;

    width = kmouse_abs_i32((int32_t)kmouse_rd32le(img + 4));
    dib_h_signed = (int32_t)kmouse_rd32le(img + 8);
    height_total = kmouse_abs_i32(dib_h_signed);
    bpp = kmouse_rd16le(img + 14);
    comp = kmouse_rd32le(img + 16);
    clr_used = kmouse_rd32le(img + 32);

    if (width == 0u || height_total == 0u)
        return -1;
    if (comp != 0u)
        return -1;
    if (kmouse_bpp_rank(bpp) == 0u)
        return -1;

    height = (height_total >= 2u) ? (height_total / 2u) : height_total;
    if (height == 0u)
        return -1;
    top_down = (dib_h_signed < 0) ? 1u : 0u;

    if (bpp <= 8u)
    {
        palette_entries = clr_used ? clr_used : (1u << bpp);
        if (palette_entries == 0u || palette_entries > (1u << bpp))
            return -1;
    }

    palette_bytes = (uint64_t)palette_entries * 4ull;
    xor_stride = (((uint64_t)width * (uint64_t)bpp + 31ull) / 32ull) * 4ull;
    xor_bytes = xor_stride * (uint64_t)height;
    and_stride = (((uint64_t)width + 31ull) / 32ull) * 4ull;
    and_bytes = and_stride * (uint64_t)height;
    min_needed = (uint64_t)dib_size + palette_bytes + xor_bytes;

    if (min_needed > img_bytes)
        return -1;

    has_and = ((uint64_t)dib_size + palette_bytes + xor_bytes + and_bytes <= img_bytes) ? 1u : 0u;
    palette = img + dib_size;
    xor_base = palette + palette_bytes;
    and_base = has_and ? (xor_base + xor_bytes) : 0;

    px_count = (uint64_t)width * (uint64_t)height;
    px_bytes = px_count * 4ull;
    if (px_count == 0ull || px_bytes > (64ull * 1024ull * 1024ull))
        return -1;

    px_pages = (px_bytes + 4095ull) >> 12;
    dst = (uint32_t *)pmem_alloc_pages_lowdma(px_pages);
    if (!dst)
        return -1;

    for (uint32_t y = 0; y < height; ++y)
    {
        uint32_t src_y = top_down ? y : (height - 1u - y);
        const uint8_t *src = xor_base + (uint64_t)src_y * xor_stride;
        uint32_t *drow = dst + (uint64_t)y * width;

        for (uint32_t x = 0; x < width; ++x)
        {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;

            if (bpp == 32u)
            {
                b = src[x * 4u + 0u];
                g = src[x * 4u + 1u];
                r = src[x * 4u + 2u];
                a = src[x * 4u + 3u];
                if (a != 0u)
                    has_alpha = 1u;
            }
            else if (bpp == 24u)
            {
                b = src[x * 3u + 0u];
                g = src[x * 3u + 1u];
                r = src[x * 3u + 2u];
            }
            else
            {
                uint8_t idx = kmouse_palette_index_at(src, x, bpp);
                const uint8_t *pal = 0;

                if (idx >= palette_entries)
                    idx = 0;
                pal = palette + (uint32_t)idx * 4u;
                b = pal[0];
                g = pal[1];
                r = pal[2];
            }

            drow[x] = kmouse_pack_argb(r, g, b, a);
        }
    }

    if (has_and && (bpp != 32u || !has_alpha))
    {
        for (uint32_t y = 0; y < height; ++y)
        {
            uint32_t src_y = top_down ? y : (height - 1u - y);
            const uint8_t *mask = and_base + (uint64_t)src_y * and_stride;
            uint32_t *drow = dst + (uint64_t)y * width;

            for (uint32_t x = 0; x < width; ++x)
            {
                uint8_t transparent = (uint8_t)((mask[x >> 3] >> (7u - (x & 7u))) & 1u);
                if (transparent)
                    drow[x] &= 0x00FFFFFFu;
                else
                    drow[x] |= 0xFF000000u;
            }
        }
    }

    if (selected_hot_x >= width)
        selected_hot_x = width ? (width - 1u) : 0u;
    if (selected_hot_y >= height)
        selected_hot_y = height ? (height - 1u) : 0u;

    out->px = dst;
    out->w = width;
    out->h = height;
    *hot_x = (int32_t)selected_hot_x;
    *hot_y = (int32_t)selected_hot_y;
    return 0;
}

static int kmouse_load_cursor_file(kmouse_cursor_asset_t *asset, const char *path)
{
    if (!asset || !path)
        return -1;

    asset->img = (kimg){0};
    asset->hot_x = 0;
    asset->hot_y = 0;
    asset->loaded = 0;

    if (kmouse_path_has_ext(path, ".cur"))
    {
        if (kmouse_load_cur(&asset->img, path, &asset->hot_x, &asset->hot_y) != 0)
            return -1;
    }
    else
    {
        if (kimg_load_bmp_flags(&asset->img, path, KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) != 0)
            return -1;
    }

    asset->loaded = 1;
    return 0;
}

static int32_t kmouse_scale_delta(int32_t raw, uint32_t sensitivity_pct)
{
    uint32_t mag = 0;
    uint32_t factor_pct = sensitivity_pct;
    uint32_t scaled = 0;

    if (raw == 0)
        return 0;

    mag = (raw < 0) ? (uint32_t)(-raw) : (uint32_t)raw;

    if (mag >= 24u)
        factor_pct = (factor_pct * 220u) / 100u;
    else if (mag >= 12u)
        factor_pct = (factor_pct * 170u) / 100u;
    else if (mag >= 6u)
        factor_pct = (factor_pct * 130u) / 100u;

    scaled = (mag * factor_pct + 50u) / 100u;
    if (scaled == 0u)
        scaled = 1u;

    return (raw < 0) ? -(int32_t)scaled : (int32_t)scaled;
}

static void kmouse_sync_cursor(void)
{
    const kfb *fb = kgfx_info();
    kgfx_obj *cursor_obj = 0;
    kmouse_cursor_asset_t *asset = kmouse_active_asset();
    int32_t max_x = 0;
    int32_t max_y = 0;
    int32_t hot_x = 0;
    int32_t hot_y = 0;

    if (!fb)
        return;

    if (asset)
    {
        hot_x = kmouse_scale_i32(asset->hot_x);
        hot_y = kmouse_scale_i32(asset->hot_y);
    }

    if (fb->width)
        max_x = (int32_t)fb->width - 1;
    if (fb->height)
        max_y = (int32_t)fb->height - 1;

    G.state.x = clamp_i32(G.state.x, 0, max_x);
    G.state.y = clamp_i32(G.state.y, 0, max_y);

    cursor_obj = kgfx_obj_ref(G.cursor_handle);
    if (cursor_obj && cursor_obj->kind == KGFX_OBJ_IMAGE)
    {
        cursor_obj->u.image.x = G.state.x - hot_x;
        cursor_obj->u.image.y = G.state.y - hot_y;
    }
}

int kmouse_init(void)
{
    const kfb *fb = kgfx_info();
    kmouse_cursor default_cursor = KMOUSE_CURSOR_ARROW;
    kmouse_cursor first_loaded = KMOUSE_CURSOR_COUNT;
    char path_buf[192];

    G.state = (kmouse_state){0};
    G.cursor_handle.idx = -1;
    G.active_cursor = KMOUSE_CURSOR_ARROW;
    G.sensitivity_pct = 100u;
    G.initialized = 1;
    G.has_cursor = 0;

    for (uint32_t i = 0; i < KMOUSE_CURSOR_COUNT; ++i)
        G.cursors[i] = (kmouse_cursor_asset_t){0};

    if (fb)
    {
        G.state.x = (int32_t)fb->width / 2;
        G.state.y = (int32_t)fb->height / 2;
    }

    for (uint32_t i = 0; i < KMOUSE_CURSOR_COUNT; ++i)
    {
        const char *file_name = kmouse_cursor_file_name((kmouse_cursor)i);
        if (!file_name)
            continue;
        kmouse_build_cursor_path(path_buf, (uint32_t)sizeof(path_buf), file_name);
        if (kmouse_load_cursor_file(&G.cursors[i], path_buf) == 0)
        {
            if (kmouse_cursor_needs_bw_invert((kmouse_cursor)i))
                kmouse_invert_bw_cursor(&G.cursors[i]);

            if (first_loaded == KMOUSE_CURSOR_COUNT)
                first_loaded = (kmouse_cursor)i;
        }
    }

    if (first_loaded == KMOUSE_CURSOR_COUNT)
        return -1;

    if (!G.cursors[(uint32_t)default_cursor].loaded)
        default_cursor = first_loaded;
    G.active_cursor = default_cursor;

    {
        kmouse_cursor_asset_t *asset = kmouse_active_asset();
        if (!asset)
            return -1;
        G.cursor_handle = kgfx_obj_add_image(asset->img.px,
                                             asset->img.w,
                                             asset->img.h,
                                             G.state.x - asset->hot_x,
                                             G.state.y - asset->hot_y,
                                             asset->img.w);
    }

    {
        kgfx_obj *cursor_obj = kgfx_obj_ref(G.cursor_handle);
        if (!cursor_obj)
            return -1;

        cursor_obj->alpha = 255;
        cursor_obj->z = 1000;
    }

    kmouse_apply_active_cursor_image();
    G.has_cursor = 1;
    G.state.visible = 1;
    kmouse_sync_cursor();
    return 0;
}

int kmouse_set_cursor(kmouse_cursor cursor)
{
    if (!G.initialized)
        return -1;
    if ((uint32_t)cursor >= KMOUSE_CURSOR_COUNT)
        return -1;
    if (!G.cursors[(uint32_t)cursor].loaded)
        return -1;

    G.active_cursor = cursor;
    kmouse_apply_active_cursor_image();
    kmouse_sync_cursor();
    return 0;
}

int kmouse_switch_cursor(kmouse_cursor cursor)
{
    return kmouse_set_cursor(cursor);
}

kmouse_cursor kmouse_current_cursor(void)
{
    return G.active_cursor;
}

void kmouse_set_sensitivity_pct(uint32_t pct)
{
    if (pct < 10u)
        pct = 10u;
    if (pct > 400u)
        pct = 400u;
    G.sensitivity_pct = pct;
}

uint32_t kmouse_sensitivity_pct(void)
{
    return G.sensitivity_pct;
}

void kmouse_update(void)
{
    kinput_mouse_state raw = {0};

    if (!G.initialized)
        return;

    kinput_mouse_consume(&raw);

    G.state.dx = kmouse_scale_delta(raw.dx, G.sensitivity_pct);
    G.state.dy = kmouse_scale_delta(raw.dy, G.sensitivity_pct);
    G.state.wheel = raw.wheel;
    G.state.buttons = raw.buttons;

    G.state.x += G.state.dx;
    G.state.y += G.state.dy;

    kmouse_sync_cursor();
}

int32_t kmouse_x(void) { return G.state.x; }
int32_t kmouse_y(void) { return G.state.y; }
int32_t kmouse_dx(void) { return G.state.dx; }
int32_t kmouse_dy(void) { return G.state.dy; }
int32_t kmouse_wheel(void) { return G.state.wheel; }
uint8_t kmouse_buttons(void) { return G.state.buttons; }
uint8_t kmouse_visible(void) { return G.state.visible; }

void kmouse_get_state(kmouse_state *out)
{
    if (out)
        *out = G.state;
}
