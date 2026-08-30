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
/* Suppress terminal/UI logging while a request runs on a worker core. */
void knet_usb_set_worker_quiet(uint32_t quiet);
int knet_usb_get_url(const char *url, uint32_t max_bytes);
int knet_usb_fetch_url(const char *url, uint8_t *response, uint32_t capacity,
                       uint32_t *out_size, uint8_t *out_truncated,
                       volatile uint32_t *cancelled, uint32_t timeout_ms);
int knet_usb_fetch_request(const char *url, const char *method,
                           const uint8_t *body, uint32_t body_size,
                           const char *content_type,
                           uint8_t *response, uint32_t capacity,
                           uint32_t *out_size, uint8_t *out_truncated,
                           volatile uint32_t *cancelled, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
