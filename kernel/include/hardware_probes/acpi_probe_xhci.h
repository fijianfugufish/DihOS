#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t acpi_xhci_get_mmios_from_rsdp(uint64_t rsdp_phys,
                                       uint64_t *out,
                                       uint32_t max_count);

#ifdef __cplusplus
}
#endif