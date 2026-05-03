#pragma once
#include <stdint.h>

void pci_dump_mapped_ecam_bus0_from_rsdp(uint64_t rsdp_phys);
void pci_dump_mapped_ecam_bus0_one_segment_from_rsdp(uint64_t rsdp_phys, uint16_t segment);
