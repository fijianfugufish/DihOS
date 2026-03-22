#include "usb/usb_msc.h"
#include <stdint.h>
#include <stddef.h>
#include "kwrappers/string.h"

// DMA-safe allocator (below 4GB)
extern void *alloc_dma(uint32_t pages);

extern void usbh_dbg_dot(int n, unsigned int rgb);

#pragma pack(push, 1)
typedef struct
{
    uint32_t sig; // 'USBC' 0x43425355
    uint32_t tag;
    uint32_t xfer_len;
    uint8_t flags; // bit7 dir: 1=IN, 0=OUT
    uint8_t lun;
    uint8_t cdb_len; // 1..16
    uint8_t cdb[16];
} CBW;

typedef struct
{
    uint32_t sig; // 'USBS' 0x53425355
    uint32_t tag;
    uint32_t residue;
    uint8_t status; // 0=Pass,1=Fail,2=PhaseErr
} CSW;
#pragma pack(pop)

static uint32_t mktag(void)
{
    static uint32_t t = 1;
    return t++;
}

static void tiny_delay(void)
{
    for (volatile uint32_t i = 0; i < 500000; ++i)
        __asm__ volatile("nop");
}

// --- DMA-safe scratch buffers ---
static CBW *g_cbw_dma = NULL;
static CSW *g_csw_dma = NULL;
static uint8_t *g_tmp_dma = NULL;
static uint32_t g_tmp_bytes = 0;

static int ensure_msc_dma(void)
{
    if (!g_cbw_dma)
        g_cbw_dma = (CBW *)alloc_dma(1); // 4KB
    if (!g_csw_dma)
        g_csw_dma = (CSW *)alloc_dma(1);
    if (!g_tmp_dma)
    {
        g_tmp_dma = (uint8_t *)alloc_dma(1);
        g_tmp_bytes = 4096;
    }
    return (g_cbw_dma && g_csw_dma && g_tmp_dma) ? 0 : -1;
}

// Low-level MSC command: assumes `data` buffer is DMA-safe if len>0

static int msc_cmd_dma(usbh_dev_t *d, const CBW *cbw_in, void *data, uint32_t len, int dir_in)
{
    // attempt 0 = normal
    // attempt 1 = after BOT recovery
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        usbh_dbg_dot(500, 0x00FFFFu);

        if (ensure_msc_dma() != 0)
        {
            usbh_dbg_dot(590, 0xFF0000u);
            return -1;
        }

        // Rebuild CBW every attempt
        *g_cbw_dma = *cbw_in;
        g_cbw_dma->sig = 0x43425355u;
        g_cbw_dma->xfer_len = len;
        g_cbw_dma->flags = dir_in ? 0x80 : 0x00;

        usbh_dbg_dot(501, 0x00FFFFu);
        if (usbh_bulk_out(d, g_cbw_dma, (uint32_t)sizeof(CBW)))
        {
            usbh_dbg_dot(591, 0xFF0000u); // CBW OUT failed

            if (attempt == 0)
            {
                usbh_dbg_dot(597, 0xFFFF00u); // recovering
                if (usbh_msc_bot_recover(d) == 0)
                    continue;
            }
            return -1;
        }
        usbh_dbg_dot(502, 0x00FF00u);

        if (len)
        {
            usbh_dbg_dot(503, 0x00FFFFu);
            int rc = dir_in ? usbh_bulk_in(d, data, len)
                            : usbh_bulk_out(d, data, len);
            if (rc)
            {
                usbh_dbg_dot(592, 0xFF0000u); // DATA stage failed

                if (attempt == 0)
                {
                    usbh_dbg_dot(598, 0xFFFF00u); // recovering after data fail
                    if (usbh_msc_bot_recover(d) == 0)
                        continue;
                }
                return -1;
            }
            usbh_dbg_dot(504, 0x00FF00u);
        }

        memset(g_csw_dma, 0, sizeof(CSW));
        usbh_dbg_dot(505, 0x00FFFFu);
        if (usbh_bulk_in(d, g_csw_dma, (uint32_t)sizeof(CSW)))
        {
            usbh_dbg_dot(593, 0xFF0000u); // CSW IN failed

            if (attempt == 0)
            {
                usbh_dbg_dot(599, 0xFFFF00u); // recovering after CSW fail
                if (usbh_msc_bot_recover(d) == 0)
                    continue;
            }
            return -1;
        }
        usbh_dbg_dot(506, 0x00FF00u);

        if (g_csw_dma->sig != 0x53425355u)
        {
            usbh_dbg_dot(594, 0xFF0000u); // bad CSW sig

            if (attempt == 0)
            {
                usbh_dbg_dot(600, 0xFFFF00u);
                if (usbh_msc_bot_recover(d) == 0)
                    continue;
            }
            return -1;
        }

        if (g_csw_dma->tag != g_cbw_dma->tag)
        {
            usbh_dbg_dot(595, 0xFF0000u); // tag mismatch

            if (attempt == 0)
            {
                usbh_dbg_dot(601, 0xFFFF00u);
                if (usbh_msc_bot_recover(d) == 0)
                    continue;
            }
            return -1;
        }

        if (g_csw_dma->status != 0)
        {
            usbh_dbg_dot(596, 0xFF0000u); // CSW status fail/phase

            if (attempt == 0)
            {
                usbh_dbg_dot(602, 0xFFFF00u);
                if (usbh_msc_bot_recover(d) == 0)
                    continue;
            }
            return -1;
        }

        usbh_dbg_dot(507, 0x00FF00u);
        return 0;
    }

    return -1;
}

// Convenience wrapper for small transfers where caller buffer may NOT be DMA-safe
static int msc_cmd_small(usbh_dev_t *d, const CBW *cbw_in, void *out, uint32_t len, int dir_in)
{
    if (ensure_msc_dma() != 0)
        return -1;

    if (len > g_tmp_bytes)
        return -1;

    if (dir_in)
    {
        if (msc_cmd_dma(d, cbw_in, g_tmp_dma, len, 1))
            return -1;
        memcpy(out, g_tmp_dma, len);
        return 0;
    }
    else
    {
        memcpy(g_tmp_dma, out, len);
        return msc_cmd_dma(d, cbw_in, g_tmp_dma, len, 0);
    }
}

static int scsi_inquiry(usbh_dev_t *d, uint8_t *buf, uint32_t len)
{
    CBW c = {0};
    c.tag = mktag();
    c.lun = 0;
    c.cdb_len = 6;
    c.cdb[0] = 0x12;
    c.cdb[4] = (uint8_t)len;
    return msc_cmd_small(d, &c, buf, len, 1);
}

static int scsi_test_ready(usbh_dev_t *d)
{
    CBW c = {0};
    c.tag = mktag();
    c.lun = 0;
    c.cdb_len = 6;
    c.cdb[0] = 0x00;
    // no data stage, CBW/CSW still must be DMA-safe => use dma path
    return msc_cmd_dma(d, &c, 0, 0, 1);
}

static int scsi_read_capacity10(usbh_dev_t *d, uint8_t out[8])
{
    CBW c = {0};
    c.tag = mktag();
    c.lun = 0;
    c.cdb_len = 10;
    c.cdb[0] = 0x25;
    return msc_cmd_small(d, &c, out, 8, 1);
}

static int scsi_read10(usbh_dev_t *d, uint32_t lba, uint16_t blocks, void *buf, uint32_t len)
{
    CBW c = {0};
    c.tag = mktag();
    c.lun = 0;
    c.cdb_len = 10;
    c.cdb[0] = 0x28;
    c.cdb[2] = (lba >> 24) & 0xFF;
    c.cdb[3] = (lba >> 16) & 0xFF;
    c.cdb[4] = (lba >> 8) & 0xFF;
    c.cdb[5] = (lba >> 0) & 0xFF;
    c.cdb[7] = (blocks >> 8) & 0xFF;
    c.cdb[8] = (blocks >> 0) & 0xFF;
    // buf must be DMA-safe for big transfers (your block layer already ensures this)
    return msc_cmd_dma(d, &c, buf, len, 1);
}

static int scsi_write10(usbh_dev_t *d, uint32_t lba, uint16_t blocks, const void *buf, uint32_t len)
{
    CBW c = {0};
    c.tag = mktag();
    c.lun = 0;
    c.cdb_len = 10;
    c.cdb[0] = 0x2A;
    c.cdb[2] = (lba >> 24) & 0xFF;
    c.cdb[3] = (lba >> 16) & 0xFF;
    c.cdb[4] = (lba >> 8) & 0xFF;
    c.cdb[5] = (lba >> 0) & 0xFF;
    c.cdb[7] = (blocks >> 8) & 0xFF;
    c.cdb[8] = (blocks >> 0) & 0xFF;
    return msc_cmd_dma(d, &c, (void *)buf, len, 0);
}

int usb_msc_probe(usb_msc_t *m)
{
    if (!m)
        return -1;

    if (ensure_msc_dma() != 0)
        return -1;

    if (usbh_init(0, 0))
        return -1; // xHCI init
    if (usbh_enumerate_first_msc(&m->dev))
        return -1;

    uint8_t inq[36] = {0};
    if (scsi_inquiry(&m->dev, inq, sizeof(inq)))
        return -1;

    for (int i = 0; i < 50; i++)
    {
        if (scsi_test_ready(&m->dev) == 0)
            break;
        tiny_delay();
    }

    uint8_t cap[8] = {0};
    if (scsi_read_capacity10(&m->dev, cap))
        return -1;

    uint32_t last_lba = ((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) | ((uint32_t)cap[2] << 8) | cap[3];
    uint32_t bsz = ((uint32_t)cap[4] << 24) | ((uint32_t)cap[5] << 16) | ((uint32_t)cap[6] << 8) | cap[7];

    m->block_size = bsz ? bsz : 512;
    m->last_lba = last_lba;
    return 0;
}

int usb_msc_read(usb_msc_t *m, uint64_t lba, uint32_t cnt, void *buf)
{
    if (!m || !cnt)
        return -1;
    while (cnt)
    {
        uint32_t chunk = (cnt > 65535u) ? 65535u : cnt;
        uint32_t bytes = chunk * m->block_size;
        if (scsi_read10(&m->dev, (uint32_t)lba, (uint16_t)chunk, buf, bytes))
            return -1;
        lba += chunk;
        cnt -= chunk;
        buf = (uint8_t *)buf + bytes;
    }
    return 0;
}

int usb_msc_write(usb_msc_t *m, uint64_t lba, uint32_t cnt, const void *buf)
{
    if (!m || !cnt)
        return -1;
    while (cnt)
    {
        uint32_t chunk = (cnt > 65535u) ? 65535u : cnt;
        uint32_t bytes = chunk * m->block_size;
        if (scsi_write10(&m->dev, (uint32_t)lba, (uint16_t)chunk, buf, bytes))
            return -1;
        lba += chunk;
        cnt -= chunk;
        buf = (const uint8_t *)buf + bytes;
    }
    return 0;
}
