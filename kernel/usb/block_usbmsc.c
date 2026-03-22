#include "usb/blockdev.h"
#include "usb/usb_msc.h"
#include <stdint.h>
#include "kwrappers/string.h"

// Provided by pmem.c (DMA-safe memory below 4GB)
extern void *alloc_dma(uint32_t pages);

extern void usbh_dbg_dot(int n, unsigned int rgb);

static usb_msc_t g_msc;
static uint32_t g_sec = 512;
static uint64_t g_lba_base = 0;

// DMA bounce buffer (must be DMA-safe!)
static uint8_t *g_dma = 0;
static uint32_t g_dma_bytes = 0;

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static int ensure_dma_buf(uint32_t want_bytes)
{
    if (g_dma && g_dma_bytes >= want_bytes)
        return 0;

    // allocate e.g. 64KB by default, rounded to pages
    uint32_t bytes = want_bytes;
    if (bytes < (64u * 1024u))
        bytes = (64u * 1024u);

    uint32_t pages = (bytes + 4095u) / 4096u;
    g_dma = (uint8_t *)alloc_dma(pages);
    if (!g_dma)
        return -1;

    g_dma_bytes = pages * 4096u;
    return 0;
}

// Choose the FAT partition LBA from an MBR if present.
// Returns 0 for "superfloppy" (VBR at LBA0) or failure.
static uint64_t detect_fat_mbr_base(const uint8_t *lba0)
{
    // must have 55AA signature to be MBR/VBR
    if (lba0[510] != 0x55 || lba0[511] != 0xAA)
        return 0;

    // If it looks like a FAT boot sector already (EB/E9), treat as superfloppy
    if (lba0[0] == 0xEB || lba0[0] == 0xE9)
        return 0;

    const uint8_t *p = lba0 + 446;
    for (int i = 0; i < 4; i++, p += 16)
    {
        uint8_t type = p[4];
        uint32_t start = (uint32_t)p[8] |
                         ((uint32_t)p[9] << 8) |
                         ((uint32_t)p[10] << 16) |
                         ((uint32_t)p[11] << 24);

        uint32_t size = (uint32_t)p[12] |
                        ((uint32_t)p[13] << 8) |
                        ((uint32_t)p[14] << 16) |
                        ((uint32_t)p[15] << 24);

        if (!start || !size)
            continue;

        // FAT32: 0x0B/0x0C, FAT16: 0x06/0x0E
        if (type == 0x0B || type == 0x0C || type == 0x06 || type == 0x0E)
            return (uint64_t)start;
    }

    return 0;
}

static int bd_read(void *ctx, uint64_t lba, uint32_t cnt, void *buf)
{
    usbh_dbg_dot(701, 0x00FFFFu); // bd_read entered

    (void)ctx;
    if (!buf || cnt == 0)
        return -1;

    // Always use DMA-safe bounce, then memcpy into FatFs buffer
    if (ensure_dma_buf(g_sec) != 0)
        return -1;

    uint8_t *out = (uint8_t *)buf;

    while (cnt)
    {
        uint32_t max_sectors = g_dma_bytes / g_sec;
        if (max_sectors == 0)
            return -1;

        uint32_t chunk = min_u32(cnt, max_sectors);

        // Proper performance-safe cap (prevents huge READ(10)/WRITE(10) that can timeout)
        const uint32_t MAX_CMD_SECTORS = 32; // 32 * 512 = 16384 bytes (fast & safe)
        if (chunk > MAX_CMD_SECTORS)
            chunk = MAX_CMD_SECTORS;

        // Read into DMA-safe memory
        uint32_t try_chunk = chunk;
        int r;

        for (;;)
        {
            r = usb_msc_read(&g_msc, g_lba_base + lba, try_chunk, g_dma);
            if (r == 0)
                break;

            if (try_chunk == 1)
            {
                // Deterministic pinpoint: which sector always fails?
                usbh_dbg_dot(610, 0xFF0000u);                                    // fail marker
                usbh_dbg_dot(611, (uint32_t)((g_lba_base + lba) & 0x00FFFFFFu)); // LBA low 24 bits in RGB
                usbh_dbg_dot(612, (uint32_t)(r & 0x00FFFFFFu));                  // error code low bits
                return r;
            }

            try_chunk >>= 1; // 32 -> 16 -> 8 -> 4 -> 2 -> 1
        }

        // success path uses try_chunk (not chunk)
        memcpy(out, g_dma, try_chunk * g_sec);
        out += try_chunk * g_sec;
        lba += try_chunk;
        cnt -= try_chunk;
    }

    return 0;
}

static int bd_write(void *ctx, uint64_t lba, uint32_t cnt, const void *buf)
{
    (void)ctx;
    if (!buf || cnt == 0)
        return -1;

    if (ensure_dma_buf(g_sec) != 0)
        return -1;

    uint8_t *in = (const uint8_t *)buf;

    while (cnt)
    {
        uint32_t max_sectors = g_dma_bytes / g_sec;
        if (max_sectors == 0)
            return -1;

        uint32_t chunk = min_u32(cnt, max_sectors);

        // Proper performance-safe cap (prevents huge READ(10)/WRITE(10) that can timeout)
        const uint32_t MAX_CMD_SECTORS = 32; // 32 * 512 = 16384 bytes (fast & safe)
        if (chunk > MAX_CMD_SECTORS)
            chunk = MAX_CMD_SECTORS;

        // Copy from caller buffer into DMA-safe memory
        uint32_t try_chunk = chunk;
        int r;

        for (;;)
        {
            r = usb_msc_write(&g_msc, g_lba_base + lba, try_chunk, g_dma);
            if (r == 0)
                break;

            if (try_chunk == 1)
            {
                // Deterministic pinpoint: which sector always fails?
                usbh_dbg_dot(610, 0xFF0000u);                                    // fail marker
                usbh_dbg_dot(611, (uint32_t)((g_lba_base + lba) & 0x00FFFFFFu)); // LBA low 24 bits in RGB
                usbh_dbg_dot(612, (uint32_t)(r & 0x00FFFFFFu));                  // error code low bits
                return r;
            }

            try_chunk >>= 1; // 32 -> 16 -> 8 -> 4 -> 2 -> 1
        }

        // success path uses try_chunk (not chunk)
        memcpy(in, g_dma, try_chunk * g_sec);
        in += try_chunk * g_sec;
        lba += try_chunk;
        cnt -= try_chunk;
    }

    return 0;
}

int usbmsc_try_bind_global(blockdev_t *out)
{
    if (!out)
        return -1;

    if (usb_msc_probe(&g_msc))
        return -1;

    g_sec = g_msc.block_size ? g_msc.block_size : 512;

    // Prepare a reasonably sized DMA bounce buffer up-front
    if (ensure_dma_buf(64u * 1024u) != 0)
        return -1;

    // Default: no partition offset
    g_lba_base = 0;

    // Read LBA0 into DMA-safe buffer so parsing is reliable
    if (usb_msc_read(&g_msc, 0, 1, g_dma) == 0)
        g_lba_base = detect_fat_mbr_base(g_dma);

    out->ctx = 0;
    out->sector_size = g_sec;
    out->read = bd_read;
    out->write = bd_write;
    return 0;
}
