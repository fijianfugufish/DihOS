#pragma once
#include <stdint.h>
#include "usb/usbh.h"

typedef struct
{
    usbh_dev_t kbd;
    usbh_dev_t mouse;

    uint8_t has_keyboard;
    uint8_t has_mouse;
} usb_hid_t;

int usb_hid_probe(usb_hid_t *h, uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint);

int usb_hid_kbd_read(usb_hid_t *h, uint8_t report[8], uint32_t *got);
int usb_hid_mouse_read(usb_hid_t *h, uint8_t report[8], uint32_t *got);