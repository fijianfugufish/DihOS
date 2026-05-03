#pragma once
#include <stdint.h>

#define DIHOS_PCI_ECAM_MAX 16u

typedef struct dihos_pci_ecam
{
    uint64_t base;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
} dihos_pci_ecam;

uint32_t acpi_pci_get_ecams_from_rsdp(uint64_t rsdp_phys,
                                      dihos_pci_ecam *out,
                                      uint32_t max_count);

void acpi_pci_print_ecams_from_rsdp(uint64_t rsdp_phys);
