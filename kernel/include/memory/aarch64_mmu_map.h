#pragma once
#include <stdint.h>

/* Tiny AArch64 mapper for DihOS.
   It patches the CURRENT TTBR0_EL1 page tables and identity-maps MMIO.
   Assumptions for this first version:
   - 4 KiB translation granule
   - EL1 kernel using TTBR0_EL1
   - page-table memory returned by pmem_alloc_pages() is identity mapped
*/

void aarch64_mmu_print_state(void);
int  aarch64_mmu_map_device_identity(uint64_t phys, uint64_t size);
int  aarch64_mmu_map_pci_ecams_from_rsdp(uint64_t rsdp_phys);
