#include "kwrappers/kfile.h"
#include "usb/ff.h"
#include "usb/diskio.h"
#include "usb/blockdev.h"

static FATFS g_fs;
static blockdev_t *g_bd = 0;

/* from our glue */
void fatfs_mount_blockdev(blockdev_t *dev);

int kfile_bind_blockdev(void *blockdev_ptr)
{
    g_bd = (blockdev_t *)blockdev_ptr;
    fatfs_mount_blockdev(g_bd);
    return 0;
}

extern void usbh_dbg_dot(int n, unsigned int rgb);

// bit = 0 -> black, bit = 1 -> white
static void dbg_bit(int dot, int bit)
{
    usbh_dbg_dot(dot, bit ? 0xFFFFFFu : 0x000000u);
}

// MSB..LSB across 8 dots
static void dbg_byte_bits(int dot_base, unsigned char v)
{
    // 8 bits
    dbg_bit(dot_base + 0, (v >> 7) & 1);
    dbg_bit(dot_base + 1, (v >> 6) & 1);
    dbg_bit(dot_base + 2, (v >> 5) & 1);
    dbg_bit(dot_base + 3, (v >> 4) & 1);
    dbg_bit(dot_base + 4, (v >> 3) & 1);
    dbg_bit(dot_base + 5, (v >> 2) & 1);
    dbg_bit(dot_base + 6, (v >> 1) & 1);
    dbg_bit(dot_base + 7, (v >> 0) & 1);

    // separator marker (cyan)
    usbh_dbg_dot(dot_base + 8, 0x00FFFFu);
}

int kfile_mount0(void)
{
    usbh_dbg_dot(358, 0x00FFFFu); // entered

    static unsigned char sec0[512] __attribute__((aligned(64)));

    if (!g_bd || !g_bd->read)
    {
        usbh_dbg_dot(359, 0xFF0000u);
        return -1;
    }

    if (g_bd->read(g_bd->ctx, 0, 1, sec0) != 0)
    {
        usbh_dbg_dot(359, 0xFF0000u);
        return -1;
    }

    // --- Dump key bytes as bits (black=0 white=1) ---

    // First 3 bytes of a FAT boot sector should be EB ?? 90  OR  E9 ?? ??
    dbg_byte_bits(370, sec0[0]); // expect 11101011 (EB) or 11101001 (E9)
    dbg_byte_bits(379, sec0[1]);
    dbg_byte_bits(388, sec0[2]);

    // OEM name at 3..10 often "MSDOS5.0" etc (not mandatory, but informative)
    dbg_byte_bits(397, sec0[3]);
    dbg_byte_bits(406, sec0[4]);

    // Bytes per sector at 11..12 should be 0x00 0x02 for 512-byte sectors
    dbg_byte_bits(415, sec0[11]); // expect 00000000
    dbg_byte_bits(424, sec0[12]); // expect 00000010

    // End signature at 510..511 should be 55 AA
    dbg_byte_bits(433, sec0[510]); // expect 01010101
    dbg_byte_bits(442, sec0[511]); // expect 10101010

    // FAT32 label starts at offset 82 (0x52): usually "FAT32   " -> first char 'F' = 0x46
    dbg_byte_bits(451, sec0[82]); // expect 01000110

    // Now mount
    FRESULT r = f_mount(&g_fs, "0:", 1);

    // FRESULT bits at 360..367
    dbg_byte_bits(360, (unsigned char)r);

    usbh_dbg_dot(368, (r == FR_OK) ? 0x00FF00u : 0xFF0000u);
    return (r == FR_OK) ? 0 : -1;
}
int kfile_umount0(void) { return (f_mount(0, "0:", 0) == FR_OK) ? 0 : -1; }

static BYTE to_mode(uint32_t flags)
{
    BYTE m = 0;
    if (flags & KFILE_READ)
        m |= FA_READ;
    if (flags & KFILE_WRITE)
        m |= FA_WRITE;
    if (flags & KFILE_TRUNC)
        m |= FA_CREATE_ALWAYS;
    else if (flags & KFILE_CREATE)
        m |= FA_OPEN_ALWAYS;
    return m ? m : FA_READ;
}

int kfile_open(KFile *f, const char *path, uint32_t flags)
{
    if (!f || !path)
        return -1;
    FRESULT r = f_open(&f->fil, path, to_mode(flags));
    if (r != FR_OK)
        return -1;
    if (flags & KFILE_APPEND)
        f_lseek(&f->fil, f_size(&f->fil));
    return 0;
}

int kfile_read(KFile *f, void *dst, uint32_t len, uint32_t *out_read)
{
    if (out_read)
        *out_read = 0;
    if (!f || !dst || len == 0)
        return -1;

    uint8_t *p = (uint8_t *)dst;
    uint32_t total = 0;

    while (total < len)
    {
        UINT br = 0;
        FRESULT r = f_read(&f->fil, p + total, (UINT)(len - total), &br);

        if (r != FR_OK)
            break;

        if (br == 0)
            break; // EOF

        total += (uint32_t)br;
    }

    if (out_read)
        *out_read = total;

    return (total > 0) ? 0 : -1;
}

int kfile_write(KFile *f, const void *buf, uint32_t n, uint32_t *out_written)
{
    if (!f || !buf)
        return -1;
    UINT bw = 0;
    FRESULT r = f_write(&f->fil, buf, n, &bw);
    if (out_written)
        *out_written = (uint32_t)bw;
    return (r == FR_OK) ? 0 : -1;
}
int kfile_seek(KFile *f, uint64_t offs)
{
    if (!f)
        return -1;
    return (f_lseek(&f->fil, (FSIZE_t)offs) == FR_OK) ? 0 : -1;
}
uint64_t kfile_size(KFile *f) { return (uint64_t)f_size(&f->fil); }
void kfile_close(KFile *f)
{
    if (f)
        f_close(&f->fil);
}

int kfile_unlink(const char *p) { return (f_unlink(p) == FR_OK) ? 0 : -1; }
int kfile_rename(const char *a, const char *b) { return (f_rename(a, b) == FR_OK) ? 0 : -1; }
int kfile_mkdir(const char *p) { return (f_mkdir(p) == FR_OK) ? 0 : -1; }

/* dirs */
int kdir_open(KDir *d, const char *path)
{
    if (!d || !path)
        return -1;
    d->fi.fname[0] = 0;
    return (f_opendir(&d->dir, path) == FR_OK) ? 0 : -1;
}
int kdir_next(KDir *d, kdirent *out)
{
    if (!d || !out)
        return -1;
    for (;;)
    {
        FRESULT r = f_readdir(&d->dir, &d->fi);
        if (r != FR_OK)
            return -1;
        if (d->fi.fname[0] == 0)
            return 0; // end
        if (d->fi.fname[0] == '.' && (!d->fi.fname[1] || (d->fi.fname[1] == '.' && !d->fi.fname[2])))
            continue;
        unsigned i = 0;
        while (d->fi.fname[i] && i < sizeof(out->name) - 1)
        {
            out->name[i] = d->fi.fname[i];
            i++;
        }
        out->name[i] = 0;
        out->is_dir = (d->fi.fattrib & AM_DIR) ? 1u : 0u;
        out->size = (uint32_t)d->fi.fsize;
        return 1; // produced an entry
    }
}
void kdir_close(KDir *d)
{
    if (d)
        f_closedir(&d->dir);
}
