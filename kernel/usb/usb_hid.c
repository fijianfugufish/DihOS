#include "usb/usb_hid.h"
#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"

static void hid_dbg(const char *msg)
{
    terminal_print(msg);
    kgfx_render_all(black);
}

static void tiny_delay(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++)
        __asm__ volatile("nop");
}

int usb_hid_probe(usb_hid_t *h, uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{
    if (!h)
        return -1;

    h->has_keyboard = 0;
    h->has_mouse = 0;

    hid_dbg("usb_hid: probe start");

    if (!xhci_mmio_hint && !acpi_rsdp_hint)
    {
        hid_dbg("usb_hid: no xhci/acpi hints");
        return -1;
    }

    hid_dbg("usb_hid: usbh_init...");
    if (usbh_init(xhci_mmio_hint, acpi_rsdp_hint))
    {
        hid_dbg("usb_hid: usbh_init failed");
        return -1;
    }
    hid_dbg("usb_hid: usbh_init ok");

    hid_dbg("usb_hid: enumerate keyboard...");
    if (usbh_enumerate_first_hid_keyboard(&h->kbd) == 0)
    {
        h->has_keyboard = 1;
        hid_dbg("usb_hid: keyboard ok");
    }
    else
    {
        hid_dbg("usb_hid: keyboard not found");
    }

    // For now, SKIP mouse completely until keyboard path is stable.
    // The second usbh_init pass is too invasive while debugging.
    h->has_mouse = 0;
    hid_dbg("usb_hid: mouse skipped for now");

    return h->has_keyboard ? 0 : -1;
}

int usb_hid_kbd_read(usb_hid_t *h, uint8_t report[8], uint32_t *got)
{
    if (!h || !h->has_keyboard || !report)
        return -1;

    return usbh_intr_in_got(&h->kbd, report, 8, got);
}

int usb_hid_mouse_read(usb_hid_t *h, uint8_t report[8], uint32_t *got)
{
    if (!h || !h->has_mouse || !report)
        return -1;

    return usbh_intr_in_got(&h->mouse, report, 8, got);
}