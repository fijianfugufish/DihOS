#pragma once

#include "bootinfo.h"
#include <stdint.h>

#define KWIFI_NETWORK_SCAN_POLL_ROUNDS 8192u

void kwifi_init(boot_info *bi, int storage_mounted);
uint32_t kwifi_network_count(void);
const char *kwifi_network_name(uint32_t index);
uint32_t kwifi_network_hidden(uint32_t index);
int kwifi_network_refresh(void);
int kwifi_network_poll(uint32_t rounds);
uint32_t kwifi_network_scan_running(void);
int kwifi_connect_request(const char *ssid, const char *username, const char *password);
uint32_t kwifi_current_connected(void);
const char *kwifi_current_ssid(void);
const char *kwifi_current_username(void);
const char *kwifi_current_status(void);
const char *kwifi_current_auth_mode(void);
const char *kwifi_current_eap_phase(void);
const char *kwifi_current_peap_phase(void);
int kwifi_poll_connection(uint32_t rounds);
int kwifi_set_supplicant_ready(uint32_t ready);
