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
/* Boot CPU only, before secondary CPUs and userspace address spaces exist. */
int aarch64_mmu_prepare_normal_nc(void);
int  aarch64_mmu_map_device_identity(uint64_t phys, uint64_t size);
/* Exclusive owned RAM only; caller cleans/invalidates caches before remap.
 * Requires an existing MAIR Normal-NC slot, never substitutes Device memory. */
int  aarch64_mmu_map_normal_nc_identity(uint64_t phys, uint64_t size);
int  aarch64_mmu_map_pci_ecams_from_rsdp(uint64_t rsdp_phys);
