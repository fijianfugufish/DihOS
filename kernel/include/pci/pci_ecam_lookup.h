#pragma once
#include <stdint.h>
#include "hardware_probes/acpi_probe_pci_lookup.h"

uint64_t pci_ecam_config_phys(const dihos_pci_ecam *ecam,
                              uint8_t bus,
                              uint8_t dev,
                              uint8_t func,
                              uint16_t offset);

void pci_print_lookup_examples_from_rsdp(uint64_t rsdp_phys);
