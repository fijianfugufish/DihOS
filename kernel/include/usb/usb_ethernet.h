#pragma once

#include <stdint.h>
#include "bootinfo.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct usb_ethernet_status
{
    uint8_t online;
    uint8_t port_id;
    uint8_t subclass;
    uint8_t protocol;
    uint16_t mtu;
    uint8_t mac[6];
    uint8_t mac_valid;
    char driver[24];
    char detail[224];
} usb_ethernet_status;

int usb_ethernet_probe_multi(const uint64_t *xhci_mmio_hints,
                             uint32_t hint_count,
                             uint64_t acpi_rsdp_hint);
uint32_t usb_ethernet_online(void);
void usb_ethernet_get_status(usb_ethernet_status *out_status);
int usb_ethernet_get_mac(uint8_t out_mac[6]);
int usb_ethernet_send_frame(const void *frame, uint32_t len);
int usb_ethernet_recv_frame(void *frame, uint32_t cap, uint32_t *out_len);
uint32_t usb_ethernet_pending_frames(void);

#ifdef __cplusplus
}
#endif
