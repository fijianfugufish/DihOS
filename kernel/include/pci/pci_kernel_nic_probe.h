#pragma once
#include <stdint.h>
#include "bootinfo.h"

void pci_kernel_set_net_hints(uint32_t hints);
void pci_kernel_probe_nics_from_mcfg(const boot_info *bi);
uint32_t pci_kernel_wifi_network_count(void);
const char *pci_kernel_wifi_network_name(uint32_t index);
uint32_t pci_kernel_wifi_network_hidden(uint32_t index);
