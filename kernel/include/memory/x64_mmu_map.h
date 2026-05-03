#pragma once
#include <stdint.h>

/* Tiny x86_64 mapper for DihOS.
   It patches the CURRENT CR3 page tables and identity-maps MMIO.
   Assumptions for this first version:
   - 4 KiB pages are supported
   - kernel can write its current page tables
   - pmem_alloc_pages(1) returns memory accessible by the kernel
   - identity mapping is wanted: virtual == physical for MMIO
*/

void x64_mmu_print_state(void);
int  x64_mmu_map_device_identity(uint64_t phys, uint64_t size);
int  x64_mmu_map_pci_ecams_from_rsdp(uint64_t rsdp_phys);
