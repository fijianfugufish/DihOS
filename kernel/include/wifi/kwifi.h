#pragma once

#include "bootinfo.h"
#include <stdint.h>

void kwifi_init(boot_info *bi, int storage_mounted);
uint32_t kwifi_network_count(void);
const char *kwifi_network_name(uint32_t index);
uint32_t kwifi_network_hidden(uint32_t index);
int kwifi_network_refresh(void);
int kwifi_network_poll(uint32_t rounds);
