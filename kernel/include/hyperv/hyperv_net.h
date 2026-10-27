#pragma once
#include <stdint.h>
int hyperv_net_init(void);
int hyperv_net_online(void);
int hyperv_net_mac(uint8_t mac[6]);
int hyperv_net_send(const void *frame, uint32_t bytes);
int hyperv_net_receive(void *frame, uint32_t capacity, uint32_t *bytes);
uint32_t hyperv_net_pending(void);
void hyperv_net_discard(void);
void hyperv_net_poll(void);
const char *hyperv_net_detail(void);

void hyperv_net_diag_print(void);
