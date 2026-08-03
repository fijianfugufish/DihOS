#pragma once
#include <stdint.h>
#include "kwrappers/kfile.h"
#include "kwrappers/kui_types.h" // kcolor
#include "memory/pmem.h"

typedef struct kimg
{
    uint32_t w;
    uint32_t h;

    // Stored as ARGB: (a<<24)|(r<<16)|(g<<8)|b
    uint32_t *px;
} kimg;

#define KIMG_BMP_FLAG_MAGENTA_TRANSPARENT 0x00000001u

// Load uncompressed BMP (24-bit or 32-bit). Returns 0 on success.
int kimg_load_bmp(kimg *out, const char *path);
int kimg_load_bmp_flags(kimg *out, const char *path, uint32_t flags);

// Load PNG and JPEG/JPG. Returns 0 on success.
int kimg_load_png(kimg *out, const char *path);
int kimg_load_jpg(kimg *out, const char *path);
int kimg_load_jpeg(kimg *out, const char *path);

// Auto-detect by file signature and dispatch to BMP/PNG/JPEG loader.
int kimg_load(kimg *out, const char *path);

// Reserve decoder scratch memory early, before later DMA/ring allocations make
// large contiguous allocations harder.
int kimg_prepare_decoder(void);

enum
{
    KIMG_FORMAT_PNG = 1u,
    KIMG_FORMAT_JPEG = 2u,
    KIMG_FORMAT_BMP = 3u,
};

enum
{
    KIMG_SAVE_FLAG_NONE = 0u,
    KIMG_SAVE_FLAG_NO_BUSY = 1u << 0,
};

// Save ARGB pixels to PNG, baseline JPEG, or 32-bit BMP.
int kimg_save(const kimg *img, const char *path, uint32_t format, uint32_t quality);
int kimg_save_ex(const kimg *img, const char *path, uint32_t format, uint32_t quality, uint32_t flags);
int kimg_encode_alloc(const kimg *img, uint32_t format, uint32_t quality,
                      uint8_t **out_data, uint32_t *out_size, uint64_t *out_pages);
uint32_t kimg_encode_bound(const kimg *img, uint32_t format);
int kimg_encode_to_buffer(const kimg *img, uint32_t format, uint32_t quality,
                          uint8_t *out_data, uint32_t out_capacity, uint32_t *out_size);
void kimg_encode_free(uint8_t *data, uint64_t pages);
int kimg_save_encoded(const char *path, const uint8_t *data, uint32_t size, uint32_t flags);

// Draw at (x,y). global_alpha multiplies per-pixel alpha (0..255).
void kimg_draw(const kimg *img, int x, int y, uint8_t global_alpha);

// Optional helper if you ever add pmem_free_pages later.
static inline void kimg_zero(kimg *img)
{
    if (!img)
        return;
    img->w = img->h = 0;
    img->px = 0;
}

// -- DEBUG --

typedef struct kimg_dbg_t
{
    int err;        // 0 ok, <0 fail
    uint32_t yfile; // row index we were reading when it failed
    uint32_t want;  // bytes requested in last read
    uint32_t got;   // bytes actually read in last read
    uint32_t row_stride;
    uint32_t w, h, bpp;
    uint32_t off_bits;
    uint32_t fpos_lo; // low 32 bits of file position at failure (best-effort)
    uint32_t lba_lo;  // low 32 bits of disk LBA at failure (if available)
} kimg_dbg_t;

extern volatile kimg_dbg_t g_kimg_dbg;
