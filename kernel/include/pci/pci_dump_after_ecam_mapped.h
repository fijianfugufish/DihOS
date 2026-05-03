#pragma once
#include <stdint.h>

/*
   Call this ONLY after the ECAM regions printed by
   pci_ecam_print_map_plan_from_rsdp() are mapped as device MMIO.
*/
void pci_dump_first_slot_each_ecam_after_mapping(uint64_t rsdp_phys);
