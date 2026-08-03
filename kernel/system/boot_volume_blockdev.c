#include "system/boot_volume_blockdev.h"
#include "memory/pmem.h"

typedef struct
{
    const uint8_t *base;
    uint64_t size_bytes;
    uint32_t sector_size;
} boot_volume_ctx;

static boot_volume_ctx G_boot_volume;

static int boot_volume_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
    boot_volume_ctx *vol = (boot_volume_ctx *)ctx;
    uint64_t off = 0;
    uint64_t bytes = 0;
    uint8_t *dst = (uint8_t *)buf;
    const uint8_t *src = 0;

    if (!vol || !vol->base || !buf || !count || !vol->sector_size)
        return -1;

    off = lba * (uint64_t)vol->sector_size;
    bytes = (uint64_t)count * (uint64_t)vol->sector_size;
    if (off > vol->size_bytes || bytes > vol->size_bytes - off)
        return -1;

    src = vol->base + off;
    for (uint64_t i = 0; i < bytes; ++i)
        dst[i] = src[i];
    return 0;
}

static int boot_volume_write(void *ctx, uint64_t lba, uint32_t count, const void *buf)
{
    boot_volume_ctx *vol = (boot_volume_ctx *)ctx;
    uint64_t off = 0;
    uint64_t bytes = 0;
    uint8_t *dst = 0;
    const uint8_t *src = (const uint8_t *)buf;

    if (!vol || !vol->base || !buf || !count || !vol->sector_size)
        return -1;

    off = lba * (uint64_t)vol->sector_size;
    bytes = (uint64_t)count * (uint64_t)vol->sector_size;
    if (off > vol->size_bytes || bytes > vol->size_bytes - off)
        return -1;

    dst = (uint8_t *)vol->base + off;
    for (uint64_t i = 0; i < bytes; ++i)
        dst[i] = src[i];
    return 0;
}

int boot_volume_blockdev_init(const boot_info *bi, blockdev_t *out)
{
    if (!bi || !out || !bi->boot_volume_base_phys || !bi->boot_volume_size_bytes)
        return -1;
    if (bi->boot_volume_sector_size != 512u)
        return -1;

    G_boot_volume.base = (const uint8_t *)pmem_phys_to_virt(bi->boot_volume_base_phys);
    G_boot_volume.size_bytes = bi->boot_volume_size_bytes;
    G_boot_volume.sector_size = bi->boot_volume_sector_size;

    out->ctx = &G_boot_volume;
    out->sector_size = G_boot_volume.sector_size;
    out->read = boot_volume_read;
    out->write = boot_volume_write;
    return 0;
}
