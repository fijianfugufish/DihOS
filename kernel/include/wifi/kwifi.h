#pragma once

#include "bootinfo.h"
#include <stdint.h>

#define KWIFI_NETWORK_SCAN_POLL_ROUNDS 192u

void kwifi_init(boot_info *bi, int storage_mounted);
uint32_t kwifi_network_count(void);
const char *kwifi_network_name(uint32_t index);
uint32_t kwifi_network_hidden(uint32_t index);
int kwifi_network_refresh(void);
int kwifi_network_poll(uint32_t rounds);
uint32_t kwifi_network_scan_running(void);
int kwifi_connect_request(const char *ssid,
                          const char *username,
                          const char *password,
                          const char *bssid_text,
                          const char *channel_text);
uint32_t kwifi_current_connected(void);
const char *kwifi_current_ssid(void);
const char *kwifi_current_username(void);
const char *kwifi_current_status(void);
const char *kwifi_current_auth_mode(void);
const char *kwifi_current_eap_phase(void);
const char *kwifi_current_peap_phase(void);
int kwifi_poll_connection(uint32_t rounds);
int kwifi_set_supplicant_ready(uint32_t ready);
uint32_t kwifi_rx_queue_count(void);
uint32_t kwifi_rx_queue_dropped(void);
int kwifi_rx_frame_pop(uint8_t *out_frame, uint32_t out_cap, uint32_t *out_len, uint32_t *out_kind);
