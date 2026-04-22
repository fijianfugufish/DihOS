#pragma once
#include <stdint.h>

typedef enum
{
    USB_FULL = 1,
    USB_HIGH = 2,
    USB_SUPER = 3
} usb_speed_t;

typedef struct
{
    int configured;
    uint8_t addr; // device address
    uint8_t slot_id;
    uint8_t port_id;
    uint8_t msc_if_num;

    // MSC
    uint8_t ep_bulk_in; // endpoint number (1..15)
    uint8_t ep_bulk_out;
    uint16_t mps_bulk_in;
    uint16_t mps_bulk_out;

    // HID
    uint8_t ep_intr_in;   // interrupt IN endpoint number (1..15)
    uint16_t mps_intr_in; // max packet size
    uint8_t hid_if_num;   // interface number
    uint8_t hid_protocol; // 1=boot keyboard, 2=boot mouse
    uint8_t hid_poll_ms;
    uint8_t hid_has_report_id;
    uint8_t hid_report_id;
    uint8_t hid_mouse_valid;
    uint8_t hid_mouse_buttons;
    uint16_t hid_mouse_btn_bits;
    uint16_t hid_mouse_x_bits;
    uint16_t hid_mouse_y_bits;
    uint16_t hid_mouse_wheel_bits;
    uint8_t hid_mouse_btn_size;
    uint8_t hid_mouse_x_size;
    uint8_t hid_mouse_y_size;
    uint8_t hid_mouse_wheel_size;

    usb_speed_t speed;
    void *hc;     // xhci internal handle
    void *devctx; // device context ptr (xhci)
    void *input_ctx;
    void *ctrl_tr;
    void *bulk_in_tr;
    void *bulk_out_tr;
    void *intr_in_tr;
    void *intr_buf;
    uint32_t intr_buf_len;
    uint64_t intr_pending_trbptr;
    uint8_t intr_pending_active;
    uint32_t ctrl_enq;
    uint32_t bin_enq;
    uint32_t bout_enq;
    uint32_t iin_enq;
    uint8_t ctrl_cycle;
    uint8_t bin_cycle;
    uint8_t bout_cycle;
    uint8_t iin_cycle;
} usbh_dev_t;

void usbh_dbg_dot(int n, unsigned int rgb);

int usbh_poll(void);

/* High-level host */
int usbh_init(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint);
int usbh_enumerate_first_msc(usbh_dev_t *d);
int usbh_enumerate_first_hid(usbh_dev_t *d);
int usbh_enumerate_first_hid_keyboard(usbh_dev_t *d);
int usbh_enumerate_first_hid_mouse(usbh_dev_t *d);

/* DMA helper */
void *usbh_alloc_dma(uint32_t bytes);

/* Transfers */
int usbh_control_xfer(usbh_dev_t *d,
                      uint8_t bmRequestType, uint8_t bRequest,
                      uint16_t wValue, uint16_t wIndex,
                      void *data, uint16_t wLength);

int usbh_bulk_out(usbh_dev_t *d, const void *buf, uint32_t len);
int usbh_bulk_in(usbh_dev_t *d, void *buf, uint32_t len);

int usbh_intr_in(usbh_dev_t *d, void *buf, uint32_t len);
int usbh_intr_in_got(usbh_dev_t *d, void *buf, uint32_t len, uint32_t *got);

int usbh_msc_bot_recover(usbh_dev_t *d);
