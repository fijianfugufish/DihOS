#pragma once
#include "usb/blockdev.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int hyperv_storage_try_bind(blockdev_t *out, uint64_t acpi_rsdp);

#ifdef __cplusplus
}
#endif
