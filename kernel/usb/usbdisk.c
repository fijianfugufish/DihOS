// usbdisk.c — USB MSC → blockdev adapter (freestanding)
#include <stdint.h>
#include "bootinfo.h"
#include "usb/blockdev.h"
#include "usb/usbh.h"
#include "asm/asm.h"

extern void usbh_dbg_dot(int n, uint32_t rgb);
#define D_WHITE 0xFFFFFFu
#define D_YELL 0xFFFF00u
#define D_GRN 0x00FF00u
#define D_RED 0xFF0000u
#define D_CYAN 0x00FFFFu
#define D_BLUE 0x00A0FFu
#define D_BLK 0x000000u

static void dbg_hexbyte(uint32_t dot_base, uint8_t v)
{
    static const uint32_t pal[16] = {
        0x000000u, // 0
        0xFFFFFFu, // 1
        0xFFFF00u, // 2
        0x00FF00u, // 3
        0x00FFFFu, // 4
        0x0000FFu, // 5
        0xFF0000u, // 6
        0xFF00FFu, // 7
        0xFFA500u, // 8
        0x808080u, // 9
        0x400000u, // A
        0x004000u, // B
        0x000040u, // C
        0x404000u, // D
        0x004040u, // E
        0x400040u  // F
    };

    usbh_dbg_dot(dot_base + 0, pal[(v >> 4) & 0xF]);
    usbh_dbg_dot(dot_base + 1, pal[(v >> 0) & 0xF]);
}

// low-DMA allocator (must exist in your pmem)
extern void *pmem_alloc_pages_lowdma(uint64_t pages);

// ---- BOT / Bulk debug (print these in kernel overlay) ----
volatile uint8_t g_scsi_last_sk = 0xFF;
volatile uint8_t g_scsi_last_asc = 0xFF;
volatile uint8_t g_scsi_last_ascq = 0xFF;
volatile uint32_t g_scsi_last_lba = 0;

volatile uint32_t g_bot_last_stage = 0; // where we failed (190/191/192/193 etc)
volatile uint32_t g_bot_last_dir = 0;   // 0=OUT, 1=IN
volatile uint32_t g_bot_last_req = 0;   // bytes requested
volatile uint32_t g_bot_last_got = 0;   // bytes got in last call
volatile int g_bot_last_r = 0;          // return code from usbh_bulk_*_got

volatile uint32_t g_bot_last_tag = 0;
volatile uint32_t g_bot_last_lba = 0;
volatile uint32_t g_bot_last_cnt = 0;
volatile uint32_t g_bot_last_xfer = 0; // bytes expected for DATA stage
volatile uint32_t g_bot_last_csw_sig = 0;
volatile uint32_t g_bot_last_csw_tag = 0;
volatile uint32_t g_bot_last_csw_res = 0;
volatile uint32_t g_bot_last_csw_status = 0;

static inline void bot_note_bulk(uint32_t stage, uint32_t dir, uint32_t req, uint32_t got, int r)
{
    g_bot_last_stage = stage;
    g_bot_last_dir = dir;
    g_bot_last_req = req;
    g_bot_last_got = got;
    g_bot_last_r = r;
}

static uint8_t *g_bounce = 0;
static uint32_t g_bounce_sz = 0;

// ====== tiny freestanding utils ======
static void kmemset(void *dst, int v, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t b = (uint8_t)v;
    for (uint32_t i = 0; i < n; i++)
        d[i] = b;
}
static void kmemcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

// DMA-safe allocator (must exist elsewhere in the kernel)
extern void *alloc_dma(uint32_t pages);

static inline void dcache_clean_range(const void *ptr, uint32_t len)
{
    asm_dma_clean_range(ptr, len);
}

static inline void dcache_invalidate_range(const void *ptr, uint32_t len)
{
    asm_dma_invalidate_range(ptr, len);
}

// DMA-enforced bounce buffer allocator.
// Always uses alloc_dma() so the xHCI controller can actually DMA into it.
static int ensure_bounce(uint32_t need)
{
    // Round up slightly to avoid tiny edge cases
    if (need < 64)
        need = 64;

    // Already have enough?
    if (g_bounce && g_bounce_sz >= need)
        return 0;

    // Allocate whole pages via DMA-safe allocator
    uint32_t pages = (need + 4095u) >> 12;

    void *p = alloc_dma(pages);
    if (!p)
        return -1;

    g_bounce = (uint8_t *)p;
    g_bounce_sz = pages << 12;

    // Clear so later DMA changes are obvious
    kmemset(g_bounce, 0, g_bounce_sz);

    return 0;
}

static uint32_t be32(uint32_t x)
{
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static uint16_t be16(uint16_t x)
{
    return (uint16_t)(((x & 0x00FFu) << 8) | ((x & 0xFF00u) >> 8));
}

// ====== BOT (Bulk-Only Transport) ======
#pragma pack(push, 1)
typedef struct
{
    uint32_t dCBWSignature; // 'USBC' = 0x43425355
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t bmCBWFlags; // bit7: 1=IN, 0=OUT
    uint8_t bCBWLUN;
    uint8_t bCBWCBLength; // 1..16
    uint8_t CBWCB[16];    // SCSI CDB
} CBW;

typedef struct
{
    uint32_t dCSWSignature; // 'USBS' = 0x53425355
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t bCSWStatus; // 0=Passed,1=Failed,2=PhaseError
} CSW;
#pragma pack(pop)

#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u

// ====== SCSI opcodes ======
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_INQUIRY 0x12
#define SCSI_READ_CAPACITY_10 0x25
#define SCSI_READ_10 0x28
#define SCSI_WRITE_10 0x2A

// ====== MSC context ======
typedef struct
{
    usbh_dev_t dev;    // endpoints/addr filled by enumeration
    uint32_t tag;      // increments per CBW
    uint32_t blk_size; // usually 512
    uint32_t blk_count;
    uint8_t lun; // 0 for most sticks

    uint64_t lba_offset; // <--- NEW: partition start LBA for FAT volume
} msc_ctx_t;

static msc_ctx_t g_msc = {0};

uint32_t usbdisk_get_lba_offset_lo(void)
{
    return (uint32_t)g_msc.lba_offset;
}

#pragma pack(push, 1)
typedef struct
{
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sectors;
} mbr_part_t;

typedef struct
{
    uint8_t boot[446];
    mbr_part_t part[4];
    uint16_t sig; // 0xAA55 in memory on little-endian
} mbr_t;
#pragma pack(pop)

static uint32_t detect_mbr_fat_partition_lba0(const uint8_t sec0[512])
{
    const mbr_t *m = (const mbr_t *)sec0;

    // not an MBR? treat as superfloppy (filesystem at LBA0)
    if (m->sig != 0xAA55)
        return 0;

    // If sector0 is actually a VBR (FAT boot sector), also return 0.
    // (Very rough check: FAT VBR usually starts with EB or E9)
    if (sec0[0] == 0xEB || sec0[0] == 0xE9)
        return 0;

    // Pick first FAT-like partition entry
    // FAT32 types: 0x0B (CHS), 0x0C (LBA)
    // FAT16 types: 0x06, 0x0E
    for (int i = 0; i < 4; i++)
    {
        uint8_t t = m->part[i].type;
        if (t == 0x0B || t == 0x0C || t == 0x06 || t == 0x0E)
        {
            if (m->part[i].lba_first != 0 && m->part[i].sectors != 0)
                return m->part[i].lba_first;
        }
    }

    // Unknown partition layout -> leave at 0 (FatFs will fail, but at least we won't break reads)
    return 0;
}

// ====== Low-level BOT helpers using usbh_bulk_in/out ======
static inline uint32_t rd32le_u(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Your file defines this later. Forward-declare so we can call it here.
extern int usbh_msc_bot_recover(usbh_dev_t *d); // must exist in your USBH layer

extern int usbh_bulk_out_got(usbh_dev_t *d, const void *buf, uint32_t len, uint32_t *got);
extern int usbh_bulk_in_got(usbh_dev_t *d, void *buf, uint32_t len, uint32_t *got);

static void bot_reset_recovery(msc_ctx_t *m)
{
    // Proper BOT recovery: Mass Storage Reset + Clear HALT on bulk in/out
    // Implemented inside USBH where control_xfer + endpoint addrs exist.
    (void)usbh_msc_bot_recover(&m->dev);
}

static inline int bot_fail(msc_ctx_t *m, int dot, uint32_t rgb)
{
    usbh_dbg_dot(dot, rgb);
    usbh_msc_bot_recover(&m->dev);
    return -1;
}

// -------- CBW (31 bytes OUT) --------
static int bot_send_cbw(msc_ctx_t *m, const void *cbw31)
{
    if (ensure_bounce(31) != 0)
        return bot_fail(m, 190, D_RED);

    kmemcpy(g_bounce, cbw31, 31);
    dcache_clean_range(g_bounce, 31);

    uint32_t got = 0;
    int r = usbh_bulk_out_got(&m->dev, g_bounce, 31, &got);
    if (r != 0 || got != 31)
        return bot_fail(m, 191, D_RED);

    return 0;
}

// -------- DATA IN (exact len) into bounce --------
static int bot_read_data(msc_ctx_t *m, uint32_t len)
{
    if (len == 0)
        return 0;
    if (ensure_bounce(len) != 0)
        return bot_fail(m, 192, D_RED);

    dcache_invalidate_range(g_bounce, len);

    uint32_t total = 0;
    while (total < len)
    {
        uint32_t got = 0;
        int r = usbh_bulk_in_got(&m->dev, (uint8_t *)g_bounce + total, len - total, &got);
        bot_note_bulk(192, 1, len - total, got, r);

        if (r != 0)
            return bot_fail(m, 192, D_RED);
        if (got == 0)
            return bot_fail(m, 192, D_RED);

        dcache_invalidate_range((uint8_t *)g_bounce + total, got);
        total += got;
    }

    return 0;
}

// -------- DATA OUT (exact len) from bounce --------
static int bot_write_data(msc_ctx_t *m, uint32_t len)
{
    if (len == 0)
        return 0;
    dcache_clean_range(g_bounce, len);

    uint32_t total = 0;
    while (total < len)
    {
        uint32_t got = 0;
        int r = usbh_bulk_out_got(&m->dev, (uint8_t *)g_bounce + total, len - total, &got);
        bot_note_bulk(195, 0, len - total, got, r);

        if (r != 0)
            return bot_fail(m, 195, D_RED);
        if (got == 0)
            return bot_fail(m, 195, D_RED);

        total += got;
    }

    return 0;
}

// -------- CSW (13 bytes IN) strict --------
static int bot_read_csw(msc_ctx_t *m, uint8_t csw_out[13])
{
    if (ensure_bounce(13) != 0)
        return bot_fail(m, 193, D_RED);

    dcache_invalidate_range(g_bounce, 13);

    uint32_t total = 0;
    while (total < 13)
    {
        uint32_t got = 0;
        int r = usbh_bulk_in_got(&m->dev, (uint8_t *)g_bounce + total, 13 - total, &got);
        bot_note_bulk(193, 1, 13 - total, got, r);

        if (r != 0)
            return bot_fail(m, 193, D_RED);
        if (got == 0)
            return bot_fail(m, 193, D_RED);

        dcache_invalidate_range((uint8_t *)g_bounce + total, got);
        total += got;
    }

    kmemcpy(csw_out, g_bounce, 13);

    // Parse CSW fields into globals so your kernel prints them
    g_bot_last_csw_sig = rd32le_u(&csw_out[0]);
    g_bot_last_csw_tag = rd32le_u(&csw_out[4]);
    g_bot_last_csw_res = rd32le_u(&csw_out[8]);
    g_bot_last_csw_status = csw_out[12];

    if (g_bot_last_csw_sig != CSW_SIG)
        return bot_fail(m, 194, D_RED);
    return 0;
}

// Returns 0=pass, 1=failed (check sense), -1=transport/phase error
static int bot_exec(msc_ctx_t *m, const uint8_t *cdb, uint8_t cdb_len,
                    int dir_in, void *payload, uint32_t xfer_len)
{
    usbh_dbg_dot(140, D_GRN);

    if (!m || !cdb || cdb_len == 0)
        return bot_fail(m, 189, D_RED);

    // ---- Build CBW (31 bytes) ----
    uint8_t cbw[31];
    for (int i = 0; i < 31; i++)
        cbw[i] = 0;

    uint32_t tag = (++m->tag ? m->tag : ++m->tag);

    g_bot_last_tag = tag;
    g_bot_last_xfer = xfer_len;

    cbw[0] = (uint8_t)(CBW_SIG >> 0);
    cbw[1] = (uint8_t)(CBW_SIG >> 8);
    cbw[2] = (uint8_t)(CBW_SIG >> 16);
    cbw[3] = (uint8_t)(CBW_SIG >> 24);

    cbw[4] = (uint8_t)(tag >> 0);
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16);
    cbw[7] = (uint8_t)(tag >> 24);

    cbw[8] = (uint8_t)(xfer_len >> 0);
    cbw[9] = (uint8_t)(xfer_len >> 8);
    cbw[10] = (uint8_t)(xfer_len >> 16);
    cbw[11] = (uint8_t)(xfer_len >> 24);

    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = (uint8_t)m->lun;

    uint8_t cl = (cdb_len > 16) ? 16 : cdb_len;
    cbw[14] = cl;
    for (uint8_t i = 0; i < cl; i++)
        cbw[15 + i] = cdb[i];

    if (cdb_len >= 10 && (cdb[0] == SCSI_READ_10 || cdb[0] == SCSI_WRITE_10))
    {
        uint32_t lba =
            ((uint32_t)cdb[2] << 24) |
            ((uint32_t)cdb[3] << 16) |
            ((uint32_t)cdb[4] << 8) |
            (uint32_t)cdb[5];

        uint32_t cnt =
            ((uint32_t)cdb[7] << 8) |
            (uint32_t)cdb[8];

        g_bot_last_lba = lba;
        g_bot_last_cnt = cnt;
    }
    else
    {
        g_bot_last_lba = 0;
        g_bot_last_cnt = 0;
    }

    // ---- CBW OUT ----
    usbh_dbg_dot(141, D_YELL);
    if (bot_send_cbw(m, cbw) != 0)
        return -1;
    usbh_dbg_dot(142, D_GRN);

    // ---- DATA phase (strict exact) ----
    if (xfer_len)
    {
        usbh_dbg_dot(143, D_YELL);

        if (dir_in)
        {
            if (bot_read_data(m, xfer_len) != 0)
                return -1;

            // preview (optional)
            // dbg_hexbyte(320, g_bounce[0]); ...

            kmemcpy(payload, g_bounce, xfer_len);
        }
        else
        {
            if (ensure_bounce(xfer_len) != 0)
                return bot_fail(m, 192, D_RED);

            kmemcpy(g_bounce, payload, xfer_len);
            if (bot_write_data(m, xfer_len) != 0)
                return -1;
        }

        usbh_dbg_dot(144, D_GRN);
    }

    // ---- CSW IN (strict) ----
    uint8_t csw[13];
    usbh_dbg_dot(145, D_YELL);
    if (bot_read_csw(m, csw) != 0)
        return -1;
    usbh_dbg_dot(146, D_GRN);

    uint32_t sig = rd32le_u(&csw[0]);
    uint32_t rtag = rd32le_u(&csw[4]);
    uint8_t st = csw[12];

    if (sig != CSW_SIG)
        return bot_fail(m, 194, D_RED);

    if (rtag != tag)
        return bot_fail(m, 195, D_RED);

    if (st == 0x00)
        return 0;
    if (st == 0x01)
        return 1;
    return bot_fail(m, 196, D_RED);
}

// ====== SCSI helpers ======
static int scsi_request_sense(msc_ctx_t *m, uint8_t *buf, uint32_t len)
{
    uint8_t cdb[6] = {SCSI_REQUEST_SENSE, 0, 0, 0, (uint8_t)len, 0};
    return bot_exec(m, cdb, 6, /*IN*/ 1, buf, len) == 0 ? 0 : -1;
}
static int scsi_test_unit_ready(msc_ctx_t *m)
{
    usbh_dbg_dot(153, 0xFFFF00u); // SCSI TEST UNIT READY
    uint8_t cdb[6] = {SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    int r = bot_exec(m, cdb, 6, /*no data*/ 1, 0, 0);

    if (r == 0)
    {
        usbh_dbg_dot(154, 0x00FF00u); // TUR ok
        return 0;
    }

    if (r == 1)
    {
        // CHECK CONDITION -> ask for sense once
        usbh_dbg_dot(155, 0xFFFF00u); // REQUEST SENSE
        uint8_t sense[18];
        (void)scsi_request_sense(m, sense, sizeof(sense));
        usbh_dbg_dot(156, 0xFF0000u); // TUR not ready
        return -1;
    }

    usbh_dbg_dot(156, 0xFF0000u); // TUR transport failure
    return -1;
}

static int scsi_inquiry(msc_ctx_t *m, uint8_t *buf, uint32_t len)
{
    usbh_dbg_dot(150, 0xFFFF00u); // SCSI INQUIRY
    uint8_t cdb[6] = {SCSI_INQUIRY, 0, 0, 0, (uint8_t)len, 0};
    int r = bot_exec(m, cdb, 6, /*IN*/ 1, buf, len);
    usbh_dbg_dot((r == 0) ? 151 : 152, (r == 0) ? 0x00FF00u : 0xFF0000u);
    return (r == 0) ? 0 : -1;
}

static int scsi_read_capacity_10(msc_ctx_t *m, uint32_t *blk_count, uint32_t *blk_size)
{
    uint8_t cdb[10] = {SCSI_READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t resp[8];
    int r = bot_exec(m, cdb, 10, /*IN*/ 1, resp, 8);
    if (r != 0)
        return -1;
    uint32_t last_lba = (resp[0] << 24) | (resp[1] << 16) | (resp[2] << 8) | resp[3];
    uint32_t bsz = (resp[4] << 24) | (resp[5] << 16) | (resp[6] << 8) | resp[7];
    if (blk_count)
        *blk_count = last_lba + 1u;
    if (blk_size)
        *blk_size = bsz;
    return 0;
}

static int scsi_read10(msc_ctx_t *m, uint32_t lba, uint16_t cnt, void *buf)
{
    usbh_dbg_dot(300, 0xFFFFFFu); // START scsi_read10 (white)

    uint8_t cdb[10] = {0};
    cdb[0] = SCSI_READ_10;

    // LBA big-endian
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba >> 0);

    // Transfer length big-endian
    cdb[7] = (uint8_t)(cnt >> 8);
    cdb[8] = (uint8_t)(cnt >> 0);

    usbh_dbg_dot(301, 0xFFFF00u); // CDB built

    uint32_t bytes = (uint32_t)cnt * m->blk_size;

    usbh_dbg_dot(302, 0x0000FFu); // calling bot_exec (blue)

    int r = bot_exec(m, cdb, 10, /*IN*/ 1, buf, bytes);

    if (r == 0)
    {
        usbh_dbg_dot(303, 0x00FF00u); // READ10 success
        return 0;
    }

    if (r == 1)
    {
        usbh_dbg_dot(304, 0xFFFF00u); // REQUEST SENSE required

        uint8_t sense[18];
        for (int i = 0; i < 18; i++)
            sense[i] = 0;

        int sr = scsi_request_sense(m, sense, sizeof(sense));

        if (sr == 0)
        {
            usbh_dbg_dot(305, 0x00FF00u); // REQUEST SENSE ok

            uint8_t sk = sense[2] & 0x0F;
            uint8_t asc = sense[12];
            uint8_t ascq = sense[13];

            g_scsi_last_sk = sk;
            g_scsi_last_asc = asc;
            g_scsi_last_ascq = ascq;
            g_scsi_last_lba = lba;

            // Sense Key (hex) → dots 310–311
            dbg_hexbyte(310, sk);
            // ASC → dots 312–313
            dbg_hexbyte(312, asc);
            // ASCQ → dots 314–315
            dbg_hexbyte(314, ascq);

            usbh_dbg_dot(316, 0x00FFFFu); // sense printed
        }
        else
        {
            usbh_dbg_dot(306, 0xFF0000u); // REQUEST SENSE failed
        }

        return -1;
    }

    // ---- NEW: one retry after transport/phase failure (STALL/timeout etc) ----
    // bot_fail() already called usbh_msc_bot_recover(), so endpoints should be un-halted now.
    // We try REQUEST SENSE (if it works, we fill your globals), then retry the READ once.
    {
        usbh_dbg_dot(308, 0xFF00FFu); // hard-fail retry path entered (magenta)

        // Try to read sense (may succeed now that recover ran)
        uint8_t sense[18];
        for (int i = 0; i < 18; i++)
            sense[i] = 0;

        int sr = scsi_request_sense(m, sense, sizeof(sense));
        if (sr == 0)
        {
            uint8_t sk = sense[2] & 0x0F;
            uint8_t asc = sense[12];
            uint8_t ascq = sense[13];

            g_scsi_last_sk = sk;
            g_scsi_last_asc = asc;
            g_scsi_last_ascq = ascq;
            g_scsi_last_lba = lba;

            usbh_dbg_dot(309, 0x00FFFFu); // sense captured on hard-fail path (cyan)
        }
        else
        {
            usbh_dbg_dot(309, 0xFF0000u); // sense failed on hard-fail path (red)
        }

        // Retry the same READ10 once
        usbh_dbg_dot(310, 0xFFFF00u); // retrying bot_exec (yellow)
        int r2 = bot_exec(m, cdb, 10, /*IN*/ 1, buf, bytes);

        if (r2 == 0)
        {
            usbh_dbg_dot(311, 0x00FF00u); // retry success (green)
            return 0;
        }

        // If retry returns CHECK CONDITION, you already have the normal sense path above
        if (r2 == 1)
        {
            usbh_dbg_dot(312, 0xFFFF00u); // retry got CHECK CONDITION (yellow)
            uint8_t sense2[18];
            for (int i = 0; i < 18; i++)
                sense2[i] = 0;

            int sr2 = scsi_request_sense(m, sense2, sizeof(sense2));
            if (sr2 == 0)
            {
                g_scsi_last_sk = sense2[2] & 0x0F;
                g_scsi_last_asc = sense2[12];
                g_scsi_last_ascq = sense2[13];
                g_scsi_last_lba = lba;
                usbh_dbg_dot(313, 0x00FFFFu); // sense captured after retry (cyan)
            }
            else
            {
                usbh_dbg_dot(313, 0xFF0000u); // sense failed after retry (red)
            }
        }

        usbh_dbg_dot(314, 0xFF0000u); // retry failed (red)
    }

    usbh_dbg_dot(307, 0xFF0000u); // bot_exec hard failure
    return -1;
}

static int scsi_write10(msc_ctx_t *m, uint32_t lba, uint16_t cnt, const void *buf)
{
    usbh_dbg_dot(320, 0xFFFFFFu); // START scsi_write10 (white)

    uint8_t cdb[10] = {0};
    cdb[0] = SCSI_WRITE_10;

    // LBA big-endian
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba >> 0);

    // Transfer length big-endian
    cdb[7] = (uint8_t)(cnt >> 8);
    cdb[8] = (uint8_t)(cnt >> 0);

    usbh_dbg_dot(321, 0xFFFF00u); // CDB built

    uint32_t bytes = (uint32_t)cnt * m->blk_size;

    usbh_dbg_dot(322, 0x0000FFu); // calling bot_exec (blue)

    int r = bot_exec(m, cdb, 10, /*OUT*/ 0, (void *)buf, bytes);

    if (r == 0)
    {
        usbh_dbg_dot(323, 0x00FF00u); // WRITE10 success
        return 0;
    }

    if (r == 1)
    {
        usbh_dbg_dot(324, 0xFFFF00u); // REQUEST SENSE required

        uint8_t sense[18];
        for (int i = 0; i < 18; i++)
            sense[i] = 0;

        int sr = scsi_request_sense(m, sense, sizeof(sense));

        if (sr == 0)
        {
            usbh_dbg_dot(325, 0x00FF00u); // REQUEST SENSE ok

            uint8_t sk = sense[2] & 0x0F;
            uint8_t asc = sense[12];
            uint8_t ascq = sense[13];

            g_scsi_last_sk = sk;
            g_scsi_last_asc = asc;
            g_scsi_last_ascq = ascq;
            g_scsi_last_lba = lba;

            // Use a different dot range from READ10 so they don't collide.
            dbg_hexbyte(330, sk);   // 330–331
            dbg_hexbyte(332, asc);  // 332–333
            dbg_hexbyte(334, ascq); // 334–335

            usbh_dbg_dot(336, 0x00FFFFu); // sense printed
        }
        else
        {
            usbh_dbg_dot(326, 0xFF0000u); // REQUEST SENSE failed
        }

        return -1;
    }

    usbh_dbg_dot(327, 0xFF0000u); // bot_exec hard failure
    return -1;
}

// ====== blockdev shims ======
// Fast path: big chunks.
// Robust path: on error -> BOT recovery -> retry same chunk once -> then halve.
// Final: when chunk==1, try a few recoveries, then fail.

static int msc_read_sectors(void *ctx, uint64_t lba, uint32_t cnt, void *buf)
{
    msc_ctx_t *m = (msc_ctx_t *)ctx;
    if (!m || !buf || cnt == 0)
        return -1;

    lba += m->lba_offset;

    uint8_t *p = (uint8_t *)buf;

    uint32_t chunk = 128; // 128*512 = 64KB per command (fast, still sane)

    while (cnt)
    {
        if (chunk > cnt)
            chunk = cnt;

        int r = scsi_read10(m, (uint32_t)lba, (uint16_t)chunk, p);

        if (r != 0)
        {
            bot_reset_recovery(m);
            r = scsi_read10(m, (uint32_t)lba, (uint16_t)chunk, p);
        }

        if (r == 0)
        {
            uint32_t bytes = chunk * m->blk_size;
            p += bytes;
            lba += chunk;
            cnt -= chunk;
            continue;
        }

        if (chunk > 1)
        {
            chunk >>= 1;
            continue;
        }

        // chunk == 1: do a few recoveries then give up
        for (int attempt = 0; attempt < 3; attempt++)
        {
            bot_reset_recovery(m);
            if (scsi_read10(m, (uint32_t)lba, 1, p) == 0)
                goto one_ok_r;
        }

        return -1;

    one_ok_r:
        p += m->blk_size;
        lba += 1;
        cnt -= 1;
        chunk = 16; // gently re-expand after a single-sector rescue
    }

    return 0;
}

static int msc_write_sectors(void *ctx, uint64_t lba, uint32_t cnt, const void *buf)
{
    msc_ctx_t *m = (msc_ctx_t *)ctx;
    if (!m || !buf || cnt == 0)
        return -1;

    lba += m->lba_offset;

    const uint8_t *p = (const uint8_t *)buf;

    uint32_t chunk = 128;

    while (cnt)
    {
        if (chunk > cnt)
            chunk = cnt;

        int r = scsi_write10(m, (uint32_t)lba, (uint16_t)chunk, p);

        if (r != 0)
        {
            bot_reset_recovery(m);
            r = scsi_write10(m, (uint32_t)lba, (uint16_t)chunk, p);
        }

        if (r == 0)
        {
            uint32_t bytes = chunk * m->blk_size;
            p += bytes;
            lba += chunk;
            cnt -= chunk;
            continue;
        }

        if (chunk > 1)
        {
            chunk >>= 1;
            continue;
        }

        for (int attempt = 0; attempt < 3; attempt++)
        {
            bot_reset_recovery(m);
            if (scsi_write10(m, (uint32_t)lba, 1, p) == 0)
                goto one_ok_w;
        }

        return -1;

    one_ok_w:
        p += m->blk_size;
        lba += 1;
        cnt -= 1;
        chunk = 16;
    }

    return 0;
}

// ====== exported block device (FatFs binds to this via kfile_bind_blockdev) ======
blockdev_t g_usb_bd = {
    .ctx = &g_msc,
    .sector_size = 512, // corrected after READ CAPACITY
    .read = msc_read_sectors,
    .write = msc_write_sectors};

// ====== make blockdev from already-enumerated MSC device ======
int usbdisk_make_blockdev_from_msc(usbh_dev_t *dev)
{
    usbh_dbg_dot(120, 0x00FF00u); // entered usbdisk_make_blockdev_from_msc

    if (!dev)
    {
        usbh_dbg_dot(199, 0xFF0000u); // dev == NULL
        return -1;
    }
    else
    {
        usbh_dbg_dot(199, 0x00FF00u);
    }

    kmemset(&g_msc, 0, sizeof(g_msc));
    g_msc.dev = *dev;
    g_msc.tag = 0xABCDEF01u;
    g_msc.lun = 0;

    // Ensure DMA-safe memory exists for BOT (CBW/CSW + payload)
    if (ensure_bounce(512) != 0)
    {
        usbh_dbg_dot(200, 0xFF0000u); // bounce(512) failed
        return -1;
    }
    else
    {
        usbh_dbg_dot(200, 0x00FF00u);
    }

    // INQUIRY (optional; some sticks fail it but still work)
    usbh_dbg_dot(121, 0xFFFF00u); // INQUIRY start
    uint8_t inq[36];
    int inq_rc = scsi_inquiry(&g_msc, inq, sizeof(inq));
    if (inq_rc == 0)
        usbh_dbg_dot(122, 0x00FF00u); // INQUIRY ok
    else
        usbh_dbg_dot(122, 0xFF0000u); // INQUIRY failed (continue)

    // TEST UNIT READY (limit retries)
    usbh_dbg_dot(123, 0xFFFF00u); // TUR start
    int ready = -1;
    for (int i = 0; i < 3; i++)
    {
        usbh_dbg_dot(130 + (uint32_t)i, 0xFFFF00u); // TUR attempt i
        if (scsi_test_unit_ready(&g_msc) == 0)
        {
            ready = 0;
            usbh_dbg_dot(133, 0x00FF00u); // TUR ok
            break;
        }
    }
    if (ready != 0)
        usbh_dbg_dot(134, 0xFF0000u); // TUR not ready

    // READ CAPACITY (real gate)
    usbh_dbg_dot(125, 0xFFFF00u); // READ CAPACITY start
    uint32_t cnt = 0, bsz = 0;
    if (scsi_read_capacity_10(&g_msc, &cnt, &bsz) != 0)
    {
        usbh_dbg_dot(201, 0xFF0000u); // READ CAPACITY command failed
        return -1;
    }
    else
    {
        usbh_dbg_dot(201, 0x00FF00u);
    }

    if (bsz == 0 || cnt == 0)
    {
        usbh_dbg_dot(202, 0xFF0000u); // READ CAPACITY returned 0s
        return -1;
    }
    else
    {
        usbh_dbg_dot(202, 0x00FF00u);
    }

    usbh_dbg_dot(126, 0x00FF00u); // READ CAPACITY ok

    g_msc.blk_size = bsz;
    g_msc.blk_count = cnt;

    // Detect partition offset (MBR FAT partition) so FatFs mounts correctly
    g_msc.lba_offset = 0;

    // Read sector 0 into g_bounce and parse MBR
    if (ensure_bounce(512) != 0)
        return -1;

    if (scsi_read10(&g_msc, 0, 1, g_bounce) == 0)
        g_msc.lba_offset = (uint64_t)detect_mbr_fat_partition_lba0(g_bounce);

    // Smoke test: read LBA0
    usbh_dbg_dot(127, 0xFFFF00u); // READ10 LBA0 start
    if (ensure_bounce(g_msc.blk_size) != 0)
    {
        usbh_dbg_dot(203, 0xFF0000u); // bounce(blk_size) failed
        return -1;
    }
    else
    {
        usbh_dbg_dot(203, 0x00FF00u);
    }

    if (scsi_read10(&g_msc, 0, 1, g_bounce) != 0)
    {
        usbh_dbg_dot(204, 0xFF0000u); // READ10(LBA0) failed
        return -1;
    }
    else
    {
        usbh_dbg_dot(204, 0x00FF00u);
    }

    usbh_dbg_dot(128, 0x00FF00u); // READ10 LBA0 ok

    g_usb_bd.sector_size = g_msc.blk_size;

    usbh_dbg_dot(129, 0x00FF00u); // make_blockdev success
    return 0;
}

// ====== full bind + enumerate helper you call from kernel ======
extern volatile uint32_t *g_fb32; // optional breadcrumb (see below)

static void crumb(uint32_t px)
{
    if (!g_fb32)
        return;
    g_fb32[px] = 0x00FFFFFF; // white pixel
    asm_dma_clean_range((const void *)(uintptr_t)&g_fb32[px], sizeof(g_fb32[px]));
}

static int usbdisk_try_bind_and_enumerate(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{

    usbh_dbg_dot(40, D_WHITE); // entered bind+enumerate
    usbh_dev_t dev = (usbh_dev_t){0};

    usbh_dbg_dot(41, D_YELL); // about to call usbh_init
    if (usbh_init(xhci_mmio_hint, acpi_rsdp_hint) != 0)
    {
        usbh_dbg_dot(42, D_RED); // usbh_init failed (or crashed before this)
        return -1;
    }
    usbh_dbg_dot(42, D_GRN); // usbh_init OK

    usbh_dbg_dot(43, D_YELL); // about to enumerate first MSC
    if (usbh_enumerate_first_msc(&dev) != 0)
    {
        usbh_dbg_dot(44, D_RED); // enumerate failed
        return -2;
    }
    usbh_dbg_dot(44, D_GRN); // enumerate OK

    usbh_dbg_dot(45, D_YELL); // about to bind blockdev
    if (usbdisk_make_blockdev_from_msc(&dev) != 0)
    {
        usbh_dbg_dot(46, D_RED); // bind failed
        return -3;
    }
    usbh_dbg_dot(46, D_GRN); // bind OK

    return 0;
}

int usbdisk_bind_and_enumerate(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{
    return usbdisk_try_bind_and_enumerate(xhci_mmio_hint, acpi_rsdp_hint);
}

int usbdisk_bind_and_enumerate_multi(const uint64_t *xhci_mmio_hints,
                                     uint32_t hint_count,
                                     uint64_t acpi_rsdp_hint)
{
    int last_rc = -1;

    if (hint_count > BOOTINFO_XHCI_MMIO_MAX)
        hint_count = BOOTINFO_XHCI_MMIO_MAX;

    for (uint32_t i = 0; xhci_mmio_hints && i < hint_count; ++i)
    {
        uint64_t mmio = xhci_mmio_hints[i] & ~0xFULL;
        int duplicate = 0;

        if (!mmio)
            continue;

        for (uint32_t j = 0; j < i; ++j)
        {
            if ((xhci_mmio_hints[j] & ~0xFULL) == mmio)
            {
                duplicate = 1;
                break;
            }
        }

        if (duplicate)
            continue;

        last_rc = usbdisk_try_bind_and_enumerate(mmio, acpi_rsdp_hint);
        if (last_rc == 0)
            return 0;
    }

    if (!hint_count && acpi_rsdp_hint)
        return usbdisk_try_bind_and_enumerate(0, acpi_rsdp_hint);

    return last_rc;
}
