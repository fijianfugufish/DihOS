#pragma once
#include <stdint.h>
#include "hardware_probes/acpi_probe_pci_lookup.h"

typedef struct dihos_pci_ecam_map_request
{
    uint64_t phys_base;
    uint64_t size_bytes;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
} dihos_pci_ecam_map_request;

uint32_t pci_ecam_get_map_requests_from_rsdp(uint64_t rsdp_phys,
                                             dihos_pci_ecam_map_request *out,
                                             uint32_t max_count);

void pci_ecam_print_map_plan_from_rsdp(uint64_t rsdp_phys);
