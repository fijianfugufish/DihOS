#include "usb/usb_hid.h"
#include "bootinfo.h"
#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"
#include "asm/asm.h"

static void hid_dbg(const char *msg)
{
    terminal_print(msg);
    kgfx_render_all(black);
}

static int hid_probe_controller_once(usb_hid_t *h, uint64_t mmio_base, uint64_t acpi_rsdp_hint)
{
    usbh_dev_t dev = {0};

    hid_dbg("usb_hid: init hid controller...");
    if (usbh_init(mmio_base, acpi_rsdp_hint))
    {
        hid_dbg("usb_hid: controller init failed");
        return 0;
    }

    hid_dbg("usb_hid: controller ok");
    hid_dbg("usb_hid: enumerate hid...");
    if (usbh_enumerate_first_hid(&dev))
    {
        hid_dbg("usb_hid: no hid found");
        return 0;
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

    return 1;
}

static void tiny_delay(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++)
        asm_relax();
}

int usb_hid_probe(usb_hid_t *h, uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{
    const uint64_t *hints = xhci_mmio_hint ? &xhci_mmio_hint : 0;
    uint32_t hint_count = xhci_mmio_hint ? 1u : 0u;

    return usb_hid_probe_multi(h, hints, hint_count, acpi_rsdp_hint);
}

int usb_hid_probe_multi(usb_hid_t *h,
                        const uint64_t *xhci_mmio_hints,
                        uint32_t hint_count,
                        uint64_t acpi_rsdp_hint)
{
    if (!h)
        return -1;

    h->has_keyboard = 0;
    h->has_mouse = 0;

    terminal_clear_no_flush();

    hid_dbg("usb_hid: probe start");

    if (hint_count > BOOTINFO_XHCI_MMIO_MAX)
        hint_count = BOOTINFO_XHCI_MMIO_MAX;

    if (xhci_mmio_hints && hint_count)
    {
        for (uint32_t i = 0; i < hint_count && (!h->has_keyboard || !h->has_mouse); ++i)
        {
            uint64_t mmio = xhci_mmio_hints[i] & ~0xFULL;
            int duplicate = 0;

            if (!mmio)
                continue;

            for (uint32_t j = 0; j < i; ++j)
            {
                if ((xhci_mmio_hints[j] & ~0xFULL) == mmio)
                {
                    duplicate = 1;
                    break;
                }
            }

            if (duplicate)
                continue;

            for (uint32_t attempt = 0; attempt < 8 && (!h->has_keyboard || !h->has_mouse); ++attempt)
            {
                if (!hid_probe_controller_once(h, mmio, acpi_rsdp_hint))
                    break;
            }
        }
    }
    else if (acpi_rsdp_hint)
    {
        for (uint32_t attempt = 0; attempt < 8 && (!h->has_keyboard || !h->has_mouse); ++attempt)
        {
            if (!hid_probe_controller_once(h, 0, acpi_rsdp_hint))
                break;
        }
    }

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
