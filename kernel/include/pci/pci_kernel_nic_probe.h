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
int pci_kernel_wifi_set_connect_override(const uint8_t *bssid, uint32_t bssid_valid, uint32_t chan_mhz);
int pci_kernel_wifi_set_peer_authorize(uint32_t authorize);
int pci_kernel_wifi_tx_l2_frame(const uint8_t *frame, uint32_t len);
int pci_kernel_wifi_tx_l2_frame_mode(const uint8_t *frame, uint32_t len, uint32_t mode);
int pci_kernel_wifi_get_local_mac(uint8_t out[6]);
int pci_kernel_wifi_get_bssid(uint8_t out[6]);
int pci_kernel_wifi_mgmt_tx_status(uint32_t *count, uint32_t *last_desc, uint32_t *last_status);
uint32_t pci_kernel_wifi_peer_assoc_done(void);
uint32_t pci_kernel_wifi_install_key_done(void);
uint32_t pci_kernel_wifi_roam_reason(void);
int32_t pci_kernel_wifi_roam_rssi(void);
uint32_t pci_kernel_wifi_rx_queue_count(void);
uint32_t pci_kernel_wifi_rx_queue_dropped(void);
int pci_kernel_wifi_rx_frame_pop(uint8_t *out_frame, uint32_t out_cap, uint32_t *out_len, uint32_t *out_kind);
