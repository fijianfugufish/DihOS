#pragma once
#include <stdint.h>
#include "usb/usbh.h"

typedef struct
{
    usbh_dev_t dev;
    uint32_t block_size; // usually 512
    uint64_t last_lba;   // from READ CAPACITY
} usb_msc_t;

int usb_msc_probe(usb_msc_t *m);
int usb_msc_read(usb_msc_t *m, uint64_t lba, uint32_t cnt, void *buf);
int usb_msc_write(usb_msc_t *m, uint64_t lba, uint32_t cnt, const void *buf);
