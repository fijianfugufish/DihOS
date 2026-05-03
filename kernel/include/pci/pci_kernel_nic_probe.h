#pragma once
#include <stdint.h>
#include "bootinfo.h"

void pci_kernel_set_net_hints(uint32_t hints);
void pci_kernel_probe_nics_from_mcfg(const boot_info *bi);
