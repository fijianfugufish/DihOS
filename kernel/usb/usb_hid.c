#include "usb/usb_hid.h"
#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"

#define USB_HID_KBD_MMIO_BASE 0x000000000A800000ULL
#define USB_HID_MOUSE_MMIO_BASE 0x000000000A000000ULL

static void hid_dbg(const char *msg)
{
    terminal_print(msg);
    kgfx_render_all(black);
}

static void hid_probe_fixed_controller(usb_hid_t *h, uint64_t mmio_base, const char *label, uint64_t acpi_rsdp_hint)
{
    usbh_dev_t dev = {0};

    hid_dbg(label);
    if (usbh_init(mmio_base, acpi_rsdp_hint))
    {
        hid_dbg("usb_hid: controller init failed");
        return;
    }

    hid_dbg("usb_hid: controller ok");
    hid_dbg("usb_hid: enumerate hid...");
    if (usbh_enumerate_first_hid(&dev))
    {
        hid_dbg("usb_hid: no hid found");
        return;
    }

    if (dev.hid_protocol == 1)
    {
        if (!h->has_keyboard)
        {
            h->kbd = dev;
            h->has_keyboard = 1;
            hid_dbg("usb_hid: keyboard ok");
        }
        else
        {
            hid_dbg("usb_hid: extra keyboard ignored");
        }
    }
    else if (dev.hid_protocol == 2)
    {
        if (!h->has_mouse)
        {
            h->mouse = dev;
            h->has_mouse = 1;
            hid_dbg("usb_hid: mouse ok");
        }
        else
        {
            hid_dbg("usb_hid: extra mouse ignored");
        }
    }
    else
    {
        hid_dbg("usb_hid: unknown hid protocol");
    }
}

static void tiny_delay(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++)
        __asm__ volatile("nop");
}

int usb_hid_probe(usb_hid_t *h, uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{
    (void)xhci_mmio_hint;

    if (!h)
        return -1;

    h->has_keyboard = 0;
    h->has_mouse = 0;

    terminal_clear_no_flush();

    hid_dbg("usb_hid: probe start");
    hid_probe_fixed_controller(h, USB_HID_KBD_MMIO_BASE, "usb_hid: init hid controller a8...", acpi_rsdp_hint);
    hid_probe_fixed_controller(h, USB_HID_MOUSE_MMIO_BASE, "usb_hid: init hid controller a0...", acpi_rsdp_hint);

    terminal_flush_log();

    return (h->has_keyboard || h->has_mouse) ? 0 : -1;
}

int usb_hid_kbd_read(usb_hid_t *h, uint8_t report[8], uint32_t *got)
{
    if (!h || !h->has_keyboard || !report)
        return -1;

    return usbh_intr_in_got(&h->kbd, report, 8, got);
}

int usb_hid_mouse_read(usb_hid_t *h, uint8_t report[16], uint32_t *got)
{
    if (!h || !h->has_mouse || !report)
        return -1;

    return usbh_intr_in_got(&h->mouse, report, 16, got);
}
