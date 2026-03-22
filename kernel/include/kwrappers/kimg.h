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

// Load uncompressed BMP (24-bit or 32-bit). Returns 0 on success.
int kimg_load_bmp(kimg *out, const char *path);

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
