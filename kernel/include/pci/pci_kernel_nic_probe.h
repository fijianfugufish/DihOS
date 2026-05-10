#pragma once
#include <stdint.h>
#include "bootinfo.h"

void pci_kernel_set_net_hints(uint32_t hints);
void pci_kernel_probe_nics_from_mcfg(const boot_info *bi);
uint32_t pci_kernel_wifi_network_count(void);
const char *pci_kernel_wifi_network_name(uint32_t index);
uint32_t pci_kernel_wifi_network_hidden(uint32_t index);
int pci_kernel_wifi_trigger_scan(void);
int pci_kernel_wifi_poll_events(uint32_t rounds);
uint32_t pci_kernel_wifi_scan_running(void);
int pci_kernel_wifi_connect_ssid(const char *ssid);
int pci_kernel_wifi_tx_l2_frame(const uint8_t *frame, uint32_t len);
int pci_kernel_wifi_mgmt_tx_status(uint32_t *count, uint32_t *last_desc, uint32_t *last_status);
