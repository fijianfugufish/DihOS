#pragma once
#include "bootinfo.h"
#include "usb/blockdev.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int boot_volume_blockdev_init(const boot_info *bi, blockdev_t *out);

#ifdef __cplusplus
}
#endif
