#pragma once
#include <stdint.h>

/* Cross-architecture MMIO mapper API.
   Kernel/PCI code should include this file, not the arch-specific files. */

void mmio_map_print_state(void);
int  mmio_map_device_identity(uint64_t phys, uint64_t size);
int  mmio_map_pci_ecams_from_rsdp(uint64_t rsdp_phys);
