#pragma once
#include <stdint.h>

/* Small, bounded mouse HID decoder shared with the host regression tests. */
typedef struct hv_hid_field {
    uint16_t bit;
    uint8_t bits, id, usage, page, relative;
    int32_t minimum, maximum;
} hv_hid_field;
typedef struct hv_hid_layout {
    hv_hid_field fields[48];
    uint16_t report_bits[256];
    uint8_t count, numbered;
} hv_hid_layout;
typedef struct hv_pointer_report {
    int32_t x, y, wheel;
    uint8_t buttons, absolute;
} hv_pointer_report;

int hv_hid_parse(hv_hid_layout *out, const uint8_t *desc, uint32_t size);
int hv_hid_decode(const hv_hid_layout *layout, const uint8_t *data,
                  uint32_t size, hv_pointer_report *out);
