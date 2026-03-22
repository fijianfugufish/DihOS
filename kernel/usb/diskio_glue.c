#include "usb/ff.h"
#include "usb/diskio.h"
#include "usb/blockdev.h"

static blockdev_t *g_dev = 0;

extern void usbh_dbg_dot(int n, unsigned int rgb);

volatile uint32_t g_disk_last_lba_lo = 0;
volatile uint32_t g_disk_last_count = 0;
volatile int g_disk_last_rc = 0;

// call once, before f_mount()
void fatfs_mount_blockdev(blockdev_t *dev) { g_dev = dev; }

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return (g_dev && g_dev->read) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return (g_dev && g_dev->read) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!g_dev || !g_dev->read || !buff || count == 0)
        return RES_NOTRDY;

    g_disk_last_lba_lo = (uint32_t)sector;
    g_disk_last_count = (uint32_t)count;

    int r = g_dev->read(g_dev->ctx, (uint64_t)sector, (uint32_t)count, buff);
    g_disk_last_rc = r;

    return r ? RES_ERROR : RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (!g_dev || !g_dev->write || !buff || count == 0)
        return RES_NOTRDY;

    return g_dev->write(g_dev->ctx, (uint64_t)sector, (uint32_t)count, buff) ? RES_ERROR : RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    if (!g_dev || !g_dev->read)
        return RES_NOTRDY;

    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (!buff)
            return RES_PARERR;
        *(WORD *)buff = (WORD)g_dev->sector_size;
        return RES_OK;

    // These aren’t strictly required for mount, but harmless if unknown:
    case GET_BLOCK_SIZE:
        if (!buff)
            return RES_PARERR;
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
