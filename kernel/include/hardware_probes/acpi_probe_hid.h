#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void acpi_probe_hid_from_rsdp(uint64_t rsdp_phys);

#ifdef __cplusplus
}
#endif