#pragma once
#include <stdint.h>

typedef struct blockdev
{
    void *ctx;
    uint32_t sector_size;                                                   // bytes per sector (512 typical)
    int (*read)(void *ctx, uint64_t lba, uint32_t count, void *buf);        // 0 = ok
    int (*write)(void *ctx, uint64_t lba, uint32_t count, const void *buf); // 0 = ok
} blockdev_t;
