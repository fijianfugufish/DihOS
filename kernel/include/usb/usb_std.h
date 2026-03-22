#pragma once
#include <stdint.h>

/* USB standard requests / descriptor types used in our code */
enum
{
    USB_REQ_GET_STATUS = 0x00,
    USB_REQ_CLEAR_FEATURE = 0x01,
    USB_REQ_SET_FEATURE = 0x03,
    USB_REQ_SET_ADDRESS = 0x05,
    USB_REQ_GET_DESCRIPTOR = 0x06,
    USB_REQ_SET_DESCRIPTOR = 0x07,
    USB_REQ_GET_CONFIGURATION = 0x08,
    USB_REQ_SET_CONFIGURATION = 0x09,
};

enum
{
    USB_DESC_DEVICE = 1,
    USB_DESC_CONFIG = 2,
    USB_DESC_STRING = 3,
    USB_DESC_INTERFACE = 4,
    USB_DESC_ENDPOINT = 5,
};

typedef struct
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;
