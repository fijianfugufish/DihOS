#include "usb/ff.h"
#include "usb/diskio.h"
#include "usb/blockdev.h"
#include "asm/asm.h"

static blockdev_t *g_dev = 0;
static volatile uint32_t g_ff_mutexes[FF_VOLUMES + 1u];
static volatile uint32_t g_diskio_lock;

extern void usbh_dbg_dot(int n, unsigned int rgb);

volatile uint32_t g_disk_last_lba_lo = 0;
volatile uint32_t g_disk_last_count = 0;
volatile int g_disk_last_rc = 0;

static void diskio_lock(void)
{
    while (__atomic_exchange_n(&g_diskio_lock, 1u, __ATOMIC_ACQUIRE))
        asm_relax();
}

static void diskio_unlock(void)
{
    __atomic_store_n(&g_diskio_lock, 0u, __ATOMIC_RELEASE);
}

int ff_mutex_create(int vol)
{
    if (vol < 0 || vol > FF_VOLUMES)
        return 0;
    __atomic_store_n(&g_ff_mutexes[vol], 0u, __ATOMIC_RELEASE);
    return 1;
}

void ff_mutex_delete(int vol)
{
    if (vol < 0 || vol > FF_VOLUMES)
        return;
    __atomic_store_n(&g_ff_mutexes[vol], 0u, __ATOMIC_RELEASE);
}

int ff_mutex_take(int vol)
{
    uint32_t spins = 0u;
    if (vol < 0 || vol > FF_VOLUMES)
        return 0;
    while (__atomic_exchange_n(&g_ff_mutexes[vol], 1u, __ATOMIC_ACQUIRE))
    {
        asm_relax();
        if (++spins > 100000000u)
            return 0;
    }
    return 1;
}

void ff_mutex_give(int vol)
{
    if (vol < 0 || vol > FF_VOLUMES)
        return;
    __atomic_store_n(&g_ff_mutexes[vol], 0u, __ATOMIC_RELEASE);
}

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

    diskio_lock();
    int r = g_dev->read(g_dev->ctx, (uint64_t)sector, (uint32_t)count, buff);
    diskio_unlock();
    g_disk_last_rc = r;

    return r ? RES_ERROR : RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (!g_dev || !g_dev->write || !buff || count == 0)
        return RES_NOTRDY;

    diskio_lock();
    int r = g_dev->write(g_dev->ctx, (uint64_t)sector, (uint32_t)count, buff);
    diskio_unlock();
    g_disk_last_lba_lo = (uint32_t)sector;
    g_disk_last_count = (uint32_t)count;
    g_disk_last_rc = r;
    return r ? RES_ERROR : RES_OK;
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
