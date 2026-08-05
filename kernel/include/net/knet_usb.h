#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct knet_usb_status
{
    uint8_t link_online;
    uint8_t configured;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t mask;
    uint32_t router;
    uint32_t dns;
    uint8_t router_mac[6];
    uint8_t router_mac_valid;
    char detail[96];
} knet_usb_status;

int knet_usb_dhcp(uint32_t rounds);
void knet_usb_get_status(knet_usb_status *out_status);
int knet_usb_get_url(const char *url, uint32_t max_bytes);

#ifdef __cplusplus
}
#endif
