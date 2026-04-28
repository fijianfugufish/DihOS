#include <stdint.h>
#include <stddef.h>
#include "kwrappers/kimg.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"

#define KIMG_FILE_READ_CHUNK_BYTES (64u * 1024u)
#define KIMG_STBI_TEMP_BYTES (32u * 1024u * 1024u)

typedef struct
{
    uint8_t *base;
    uint32_t cap;
    uint32_t used;
} kimg_stbi_arena_t;

static kimg_stbi_arena_t g_kimg_stbi_arena;

static int kimg_stbi_arena_prepare(void);
static void kimg_stbi_arena_reset(void);
static void *kimg_stbi_malloc(size_t size);
static void *kimg_stbi_realloc(void *ptr, size_t new_size);
static void kimg_stbi_free(void *ptr);

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) kimg_stbi_malloc(sz)
#define STBI_REALLOC(p, newsz) kimg_stbi_realloc((p), (newsz))
#define STBI_FREE(p) kimg_stbi_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

extern void usbh_dbg_dot(int n, unsigned int rgb);

volatile kimg_dbg_t g_kimg_dbg;

extern volatile uint32_t g_disk_last_lba_lo;
extern volatile uint32_t g_disk_last_count;
extern volatile int g_disk_last_rc;

uint64_t kfile_size(KFile *f);

typedef struct
{
    uint32_t payload_size;
} kimg_stbi_alloc_hdr_t;

static uint32_t kimg_align_u32(uint32_t value, uint32_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static int kimg_stbi_arena_prepare(void)
{
    if (g_kimg_stbi_arena.base)
        return 0;

    uint32_t pages = (KIMG_STBI_TEMP_BYTES + 4095u) >> 12;
    uint8_t *base = (uint8_t *)pmem_alloc_pages((uint64_t)pages);
    if (!base)
        return -1;

    g_kimg_stbi_arena.base = base;
    g_kimg_stbi_arena.cap = pages << 12;
    g_kimg_stbi_arena.used = 0u;
    return 0;
}

static void kimg_stbi_arena_reset(void)
{
    g_kimg_stbi_arena.used = 0u;
}

static void *kimg_stbi_malloc(size_t size)
{
    uint32_t need = 0u;
    uint32_t start = 0u;
    kimg_stbi_alloc_hdr_t *hdr = 0;

    if (size == 0u || size > 0x7FFFFFFFu)
        return 0;
    if (kimg_stbi_arena_prepare() != 0)
        return 0;

    need = kimg_align_u32((uint32_t)size + (uint32_t)sizeof(kimg_stbi_alloc_hdr_t), 8u);
    start = kimg_align_u32(g_kimg_stbi_arena.used, 8u);
    if (start > g_kimg_stbi_arena.cap || need > g_kimg_stbi_arena.cap - start)
        return 0;

    hdr = (kimg_stbi_alloc_hdr_t *)(g_kimg_stbi_arena.base + start);
    hdr->payload_size = (uint32_t)size;
    g_kimg_stbi_arena.used = start + need;
    return (void *)(hdr + 1);
}

static void *kimg_stbi_realloc(void *ptr, size_t new_size)
{
    void *np = 0;
    uint32_t old_size = 0u;
    uint8_t *dst = 0;
    const uint8_t *src = 0;
    uint32_t copy_n = 0u;

    if (!ptr)
        return kimg_stbi_malloc(new_size);
    if (new_size == 0u)
        return 0;

    np = kimg_stbi_malloc(new_size);
    if (!np)
        return 0;

    old_size = ((kimg_stbi_alloc_hdr_t *)ptr - 1)->payload_size;
    copy_n = old_size < (uint32_t)new_size ? old_size : (uint32_t)new_size;
    dst = (uint8_t *)np;
    src = (const uint8_t *)ptr;
    memcpy(dst, src, copy_n);

    return np;
}

static void kimg_stbi_free(void *ptr)
{
    (void)ptr;
}

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
Pixel output format:
- Many little-endian framebuffers want bytes in memory: B,G,R,A (BGRA).
  Writing a 32-bit value 0xAARRGGBB produces bytes BB GG RR AA in memory => BGRA.
- If your kgfx expects RGBA bytes in memory, use KIMG_OUT_RGBA.
*/
#define KIMG_OUT_BGRA 1
// #define KIMG_OUT_RGBA 1

static inline uint32_t pack_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
#if defined(KIMG_OUT_RGBA)
    // RGBA bytes in memory => 0xAABBGGRR on little-endian
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
#else
    // BGRA bytes in memory => 0xAARRGGBB on little-endian
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
#endif
}

static inline uint8_t kimg_apply_bmp_alpha_flags(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t flags)
{
    if ((flags & KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) &&
        r == 0xFFu && g == 0x00u && b == 0xFFu)
    {
        return 0u;
    }

    return a;
}

// Loads BMP (24/32 bpp, BI_RGB only) into 32-bit pixels in pmem.
// out->px points to pixels, out->w/out->h set.
// Returns 0 on success.
int kimg_load_bmp_flags(kimg *out, const char *path, uint32_t flags)
{
    g_kimg_dbg.err = 0;
    g_kimg_dbg.yfile = 0;
    g_kimg_dbg.want = 0;
    g_kimg_dbg.got = 0;
    g_kimg_dbg.row_stride = 0;
    g_kimg_dbg.w = 0;
    g_kimg_dbg.h = 0;
    g_kimg_dbg.bpp = 0;
    g_kimg_dbg.off_bits = 0;
    g_kimg_dbg.fpos_lo = 0;
    g_kimg_dbg.lba_lo = 0;

    if (!out || !path)
        return -1;

    out->px = NULL;
    out->w = 0;
    out->h = 0;

    KFile f;
    if (kfile_open(&f, path, KFILE_READ) != 0)
        return -1;

    // Read BMP file header (14) + first 40 bytes of DIB
    uint8_t hdr[14 + 40];
    uint32_t br = 0;

    if (kfile_read(&f, hdr, (uint32_t)sizeof(hdr), &br) != 0 || br != (uint32_t)sizeof(hdr))
    {
        kfile_close(&f);
        return -1;
    }

    // 'BM'
    if (hdr[0] != 'B' || hdr[1] != 'M')
    {
        kfile_close(&f);
        return -1;
    }

    uint32_t off_bits = rd32le(&hdr[10]);

    uint32_t dib_sz = rd32le(&hdr[14]);
    if (dib_sz < 40)
    {
        kfile_close(&f);
        return -1;
    }

    int32_t w_s = (int32_t)rd32le(&hdr[18]);
    int32_t h_s = (int32_t)rd32le(&hdr[22]);
    uint16_t planes = rd16le(&hdr[26]);
    uint16_t bpp = rd16le(&hdr[28]);
    uint32_t comp = rd32le(&hdr[30]);

    if (w_s == 0 || h_s == 0 || planes != 1)
    {
        kfile_close(&f);
        return -1;
    }

    // only BI_RGB supported
    if (comp != 0)
    {
        kfile_close(&f);
        return -1;
    }

    if (!(bpp == 24 || bpp == 32))
    {
        kfile_close(&f);
        return -1;
    }

    uint32_t abs_w = (w_s < 0) ? (uint32_t)(-w_s) : (uint32_t)w_s;
    uint32_t abs_h = (h_s < 0) ? (uint32_t)(-h_s) : (uint32_t)h_s;

    // sanity limits
    if (abs_w > 8192u || abs_h > 8192u)
    {
        kfile_close(&f);
        return -1;
    }

    uint32_t bytes_per_px = (uint32_t)(bpp >> 3); // 3 or 4
    uint32_t row_raw = abs_w * bytes_per_px;
    uint32_t row_file = (row_raw + 3u) & ~3u; // standard padded stride

    // Choose row_stride using ACTUAL file size (FatFs)
    uint64_t file_sz = kfile_size(&f);
    if (file_sz < (uint64_t)off_bits)
    {
        usbh_dbg_dot(522, 0xFF0000u);
        kfile_close(&f);
        return -1;
    }

    uint64_t data_bytes = file_sz - (uint64_t)off_bits;
    uint64_t expect_padded = (uint64_t)abs_h * (uint64_t)row_file;
    uint64_t expect_tight = (uint64_t)abs_h * (uint64_t)row_raw;

    uint32_t row_stride = row_file;

    if (data_bytes == expect_tight)
        row_stride = row_raw;
    else if (data_bytes == expect_padded)
        row_stride = row_file;
    else
    {
        if (abs_h != 0 && (data_bytes % (uint64_t)abs_h) == 0)
        {
            uint64_t s = data_bytes / (uint64_t)abs_h;
            if (s >= (uint64_t)row_raw && s <= (uint64_t)row_file)
                row_stride = (uint32_t)s;
        }

        // If the file cannot possibly contain enough pixels even in tight form, fail early.
        if (data_bytes < expect_tight)
        {
            usbh_dbg_dot(526, 0xFF0000u);
            kfile_close(&f);
            return -1;
        }
    }

    g_kimg_dbg.w = abs_w;
    g_kimg_dbg.h = abs_h;
    g_kimg_dbg.bpp = bpp;
    g_kimg_dbg.off_bits = off_bits;
    g_kimg_dbg.row_stride = row_stride;

    // Allocate destination ARGB/BGRA buffer
    uint64_t px_count = (uint64_t)abs_w * (uint64_t)abs_h;
    uint64_t px_bytes = px_count * 4ull;
    if (px_count == 0ull || px_bytes > (256ull * 1024ull * 1024ull))
    {
        kfile_close(&f);
        return -1;
    }

    uint32_t px_pages = (uint32_t)((px_bytes + 4095ull) >> 12);
    uint32_t *dst = (uint32_t *)pmem_alloc_pages_lowdma(px_pages);
    if (!dst)
    {
        kfile_close(&f);
        return -1;
    }

    // Stream-skip to off_bits (no seek)
    if (off_bits > (uint32_t)sizeof(hdr))
    {
        uint32_t to_skip = off_bits - (uint32_t)sizeof(hdr);

        uint8_t discard[256];
        while (to_skip)
        {
            uint32_t n = (to_skip > (uint32_t)sizeof(discard)) ? (uint32_t)sizeof(discard) : to_skip;
            uint32_t got = 0;

            int rc = kfile_read(&f, discard, n, &got);
            if (got == 0)
            {
                usbh_dbg_dot(524, 0xFF0000u);
                kfile_close(&f);
                return -1;
            }

            // handle partial reads robustly
            if (got < n)
            {
                to_skip -= got;
            }
            else
            {
                to_skip -= n;
            }
        }
    }
    else if (off_bits < (uint32_t)sizeof(hdr))
    {
        usbh_dbg_dot(523, 0xFF0000u);
        kfile_close(&f);
        return -1;
    }

    // Read the entire pixel payload sequentially (ktext-style)
    uint64_t pixel_bytes = (uint64_t)row_stride * (uint64_t)abs_h;
    if (pixel_bytes == 0ull)
    {
        kfile_close(&f);
        return -1;
    }

    uint32_t pix_pages = (uint32_t)((pixel_bytes + 4095ull) >> 12);
    uint8_t *pixbuf = (uint8_t *)pmem_alloc_pages_lowdma(pix_pages);
    if (!pixbuf)
    {
        kfile_close(&f);
        return -1;
    }

    uint64_t remaining = pixel_bytes;
    uint8_t *p = pixbuf;

    // heartbeat control: flash every ~64KB (cheap)
    uint64_t next_beat = (remaining > 65536ull) ? (remaining - 65536ull) : 0ull;
    unsigned beat_phase = 0;

    uint32_t dbg_row = 0;
    uint64_t dbg_next_row_off = (uint64_t)row_stride; // first row boundary in bytes

    while (remaining)
    {
        uint32_t want = (remaining > (uint64_t)KIMG_FILE_READ_CHUNK_BYTES) ? KIMG_FILE_READ_CHUNK_BYTES : (uint32_t)remaining;
        uint32_t got = 0;

        // Update "row" estimate based on how many bytes we've already consumed.
        // No division: just bump row when we cross row_stride boundaries.
        uint64_t done = (uint64_t)(p - pixbuf);
        while (done >= dbg_next_row_off && dbg_row + 1u < abs_h)
        {
            dbg_row++;
            dbg_next_row_off += (uint64_t)row_stride;
        }

        g_kimg_dbg.yfile = dbg_row;
        g_kimg_dbg.want = want;
        g_kimg_dbg.fpos_lo = (uint32_t)f_tell(&f.fil);

        int rc = kfile_read(&f, p, want, &got);

        // Only fail if we made *no progress*
        if (got == 0)
        {
            g_kimg_dbg.got = got;
            g_kimg_dbg.lba_lo = g_disk_last_lba_lo;
            g_kimg_dbg.err = -525; // keep your code

            usbh_dbg_dot(525, 0xFF0000u);
            kfile_close(&f);
            return -1;
        }

        // Optional: mark “we got bytes but rc signalled trouble”
        if (rc != 0)
        {
            usbh_dbg_dot(527, 0xFFFF00u); // yellow warning, not fatal
        }

        // Record the REAL got after the read
        g_kimg_dbg.got = got;

        p += got;
        remaining -= got;

        if (remaining <= next_beat)
        {
            unsigned int col = (beat_phase & 1u) ? 0x00FFFFu : 0xFFFF00u;
            usbh_dbg_dot(520, col);
            beat_phase++;

            next_beat = (remaining > 65536ull) ? (remaining - 65536ull) : 0ull;
        }
    }

    // Decode from pixbuf into dst
    for (uint32_t y = 0; y < abs_h; ++y)
    {
        uint32_t ydst = (h_s > 0) ? (abs_h - 1u - y) : y;

        uint8_t *src = pixbuf + (uint64_t)y * (uint64_t)row_stride;
        uint32_t *drow = dst + (uint64_t)ydst * (uint64_t)abs_w;

        if (bpp == 24)
        {
            for (uint32_t x = 0; x < abs_w; ++x)
            {
                uint8_t b = src[x * 3u + 0u];
                uint8_t g = src[x * 3u + 1u];
                uint8_t r = src[x * 3u + 2u];
                uint8_t a = kimg_apply_bmp_alpha_flags(r, g, b, 0xFFu, flags);
                drow[x] = pack_pixel(r, g, b, a);
            }
        }
        else
        {
            for (uint32_t x = 0; x < abs_w; ++x)
            {
                uint8_t b = src[x * 4u + 0u];
                uint8_t g = src[x * 4u + 1u];
                uint8_t r = src[x * 4u + 2u];
                uint8_t a = kimg_apply_bmp_alpha_flags(r, g, b, src[x * 4u + 3u], flags);
                drow[x] = pack_pixel(r, g, b, a);
            }
        }
    }

    usbh_dbg_dot(521, 0xFF00FFu);
    kfile_close(&f);

    out->px = dst;
    out->w = abs_w;
    out->h = abs_h;
    return 0;
}

int kimg_load_bmp(kimg *out, const char *path)
{
    return kimg_load_bmp_flags(out, path, 0);
}

typedef enum
{
    KIMG_FMT_UNKNOWN = 0,
    KIMG_FMT_BMP,
    KIMG_FMT_PNG,
    KIMG_FMT_JPEG
} kimg_file_format;

static kimg_file_format kimg_detect_format_bytes(const uint8_t *p, uint32_t n)
{
    if (p && n >= 2u && p[0] == 'B' && p[1] == 'M')
        return KIMG_FMT_BMP;

    if (p && n >= 8u &&
        p[0] == 0x89u && p[1] == 'P' && p[2] == 'N' && p[3] == 'G' &&
        p[4] == 0x0Du && p[5] == 0x0Au && p[6] == 0x1Au && p[7] == 0x0Au)
    {
        return KIMG_FMT_PNG;
    }

    if (p && n >= 3u && p[0] == 0xFFu && p[1] == 0xD8u && p[2] == 0xFFu)
        return KIMG_FMT_JPEG;

    return KIMG_FMT_UNKNOWN;
}

static kimg_file_format kimg_detect_file_format(const char *path)
{
    KFile f;
    uint8_t hdr[12];
    uint32_t got = 0u;

    if (!path)
        return KIMG_FMT_UNKNOWN;

    if (kfile_open(&f, path, KFILE_READ) != 0)
        return KIMG_FMT_UNKNOWN;

    (void)kfile_read(&f, hdr, (uint32_t)sizeof(hdr), &got);
    kfile_close(&f);

    return kimg_detect_format_bytes(hdr, got);
}

static int kimg_read_file_to_pmem(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    KFile f;
    uint64_t size64 = 0ull;
    uint32_t size = 0u;
    uint32_t total = 0u;
    uint8_t *buf = 0;

    if (!path || !out_buf || !out_size)
        return -1;

    *out_buf = 0;
    *out_size = 0u;

    if (kfile_open(&f, path, KFILE_READ) != 0)
        return -1;

    size64 = kfile_size(&f);
    if (size64 == 0ull || size64 > 0x7FFFFFFFull)
    {
        kfile_close(&f);
        return -1;
    }

    size = (uint32_t)size64;
    buf = (uint8_t *)pmem_alloc_pages((size64 + 4095ull) >> 12);
    if (!buf)
    {
        kfile_close(&f);
        return -1;
    }

    while (total < size)
    {
        uint32_t want = size - total;
        uint32_t got = 0u;

        if (want > KIMG_FILE_READ_CHUNK_BYTES)
            want = KIMG_FILE_READ_CHUNK_BYTES;

        if (kfile_read(&f, buf + total, want, &got) != 0 || got == 0u)
        {
            kfile_close(&f);
            pmem_free_pages(buf, (size64 + 4095ull) >> 12);
            return -1;
        }

        total += got;
    }

    kfile_close(&f);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int kimg_load_stbi_rgba_flags(kimg *out, const char *path, uint32_t flags)
{
    uint8_t *file_buf = 0;
    uint32_t file_size = 0u;
    stbi_uc *decoded = 0;
    uint32_t *dst = 0;
    int w = 0;
    int h = 0;
    int comp = 0;

    if (!out || !path)
        return -1;

    out->px = 0;
    out->w = 0u;
    out->h = 0u;

    if (kimg_stbi_arena_prepare() != 0)
        return -1;
    kimg_stbi_arena_reset();

    if (kimg_read_file_to_pmem(path, &file_buf, &file_size) != 0)
    {
        kimg_stbi_arena_reset();
        return -1;
    }

    decoded = stbi_load_from_memory(file_buf, (int)file_size, &w, &h, &comp, 4);
    pmem_free_pages(file_buf, ((uint64_t)file_size + 4095ull) >> 12);

    if (!decoded || w <= 0 || h <= 0)
    {
        kimg_stbi_arena_reset();
        return -1;
    }

    if ((uint32_t)w > 8192u || (uint32_t)h > 8192u)
    {
        kimg_stbi_arena_reset();
        return -1;
    }

    uint64_t px_count = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    uint64_t px_bytes = px_count * 4ull;
    if (px_count == 0ull || px_bytes > (256ull * 1024ull * 1024ull))
    {
        kimg_stbi_arena_reset();
        return -1;
    }

    uint32_t px_pages = (uint32_t)((px_bytes + 4095ull) >> 12);
    dst = (uint32_t *)pmem_alloc_pages(px_pages);
    if (!dst)
    {
        kimg_stbi_arena_reset();
        return -1;
    }

    for (uint64_t i = 0; i < px_count; ++i)
    {
        uint8_t r = decoded[i * 4ull + 0ull];
        uint8_t g = decoded[i * 4ull + 1ull];
        uint8_t b = decoded[i * 4ull + 2ull];
        uint8_t a = decoded[i * 4ull + 3ull];
        a = kimg_apply_bmp_alpha_flags(r, g, b, a, flags);
        dst[i] = pack_pixel(r, g, b, a);
    }

    out->px = dst;
    out->w = (uint32_t)w;
    out->h = (uint32_t)h;

    kimg_stbi_arena_reset();
    return 0;
}

int kimg_load_png(kimg *out, const char *path)
{
    if (kimg_detect_file_format(path) != KIMG_FMT_PNG)
        return -1;
    return kimg_load_stbi_rgba_flags(out, path, 0u);
}

int kimg_load_jpg(kimg *out, const char *path)
{
    if (kimg_detect_file_format(path) != KIMG_FMT_JPEG)
        return -1;
    return kimg_load_stbi_rgba_flags(out, path, 0u);
}

int kimg_load_jpeg(kimg *out, const char *path)
{
    return kimg_load_jpg(out, path);
}

int kimg_load(kimg *out, const char *path)
{
    switch (kimg_detect_file_format(path))
    {
    case KIMG_FMT_BMP:
        return kimg_load_bmp(out, path);
    case KIMG_FMT_PNG:
        return kimg_load_stbi_rgba_flags(out, path, 0u);
    case KIMG_FMT_JPEG:
        return kimg_load_stbi_rgba_flags(out, path, 0u);
    default:
        return -1;
    }
}
