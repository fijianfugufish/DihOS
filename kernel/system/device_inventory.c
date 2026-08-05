#include "system/device_inventory.h"
#include "bootinfo.h"
#include "kwrappers/kinput.h"
#include "i2c/i2c1_hidi2c.h"
#include "wifi/kwifi.h"
#include "usb/usb_ethernet.h"

extern const boot_info *k_bootinfo_ptr;

static void inv_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;

    if (!dst || cap == 0u)
        return;
    if (!src)
        src = "";
    while (src[i] && i + 1u < cap)
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void inv_append(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0;

    if (!dst || cap == 0u)
        return;
    while (dst[len] && len + 1u < cap)
        ++len;
    if (!src)
        return;
    while (*src && len + 1u < cap)
        dst[len++] = *src++;
    dst[len] = 0;
}

static void inv_append_u32(char *dst, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t len = 0;

    if (value == 0u)
    {
        inv_append(dst, cap, "0");
        return;
    }
    while (value && len < sizeof(tmp))
    {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len)
    {
        char s[2];
        s[0] = tmp[--len];
        s[1] = 0;
        inv_append(dst, cap, s);
    }
}

static void inv_append_hex64(char *dst, uint32_t cap, uint64_t value)
{
    const char *hex = "0123456789ABCDEF";
    char out[19];

    out[0] = '0';
    out[1] = 'x';
    for (uint32_t i = 0u; i < 16u; ++i)
        out[2u + i] = hex[(value >> ((15u - i) * 4u)) & 0xFull];
    out[18] = 0;
    inv_append(dst, cap, out);
}

static void inv_add(device_inventory_row *rows, uint32_t max_rows, uint32_t *count,
                    const char *group, const char *name, const char *status, const char *detail)
{
    device_inventory_row *row = 0;

    if (!rows || !count || *count >= max_rows)
        return;

    row = &rows[(*count)++];
    inv_copy(row->group, sizeof(row->group), group);
    inv_copy(row->name, sizeof(row->name), name);
    inv_copy(row->status, sizeof(row->status), status);
    inv_copy(row->detail, sizeof(row->detail), detail);
}

uint32_t device_inventory_snapshot(device_inventory_row *out_rows, uint32_t max_rows)
{
    uint32_t count = 0u;
    kinput_device_status input_status;
    const hidi2c_device *kbd = 0;
    const hidi2c_device *tpd = 0;
    usb_ethernet_status eth_status;
    char detail[DEVICE_INVENTORY_DETAIL_CAP];

    if (!out_rows || max_rows == 0u)
        return 0u;

    if (k_bootinfo_ptr && k_bootinfo_ptr->xhci_mmio_count)
    {
        uint32_t xhci_count = k_bootinfo_ptr->xhci_mmio_count;
        if (xhci_count > BOOTINFO_XHCI_MMIO_MAX)
            xhci_count = BOOTINFO_XHCI_MMIO_MAX;
        for (uint32_t i = 0u; i < xhci_count; ++i)
        {
            detail[0] = 0;
            inv_append(detail, sizeof(detail), "MMIO ");
            inv_append_hex64(detail, sizeof(detail), k_bootinfo_ptr->xhci_mmio_bases[i]);
            inv_append(detail, sizeof(detail), " source ");
            inv_append_u32(detail, sizeof(detail), k_bootinfo_ptr->xhci_mmio_sources[i]);
            inv_add(out_rows, max_rows, &count, "USB", "xHCI Controller", "present", detail);
        }
    }
    kinput_get_device_status(&input_status);
    if (input_status.usb_keyboard_present)
        inv_add(out_rows, max_rows, &count, "USB", "USB HID Keyboard", "online", "USB HID keyboard report endpoint discovered");
    if (input_status.usb_mouse_present)
        inv_add(out_rows, max_rows, &count, "USB", "USB HID Mouse", "online", "USB HID mouse report endpoint discovered");

    kbd = i2c1_hidi2c_keyboard();
    detail[0] = 0;
    if (kbd)
    {
        inv_append(detail, sizeof(detail), "addr ");
        inv_append_hex64(detail, sizeof(detail), kbd->i2c_addr_7bit);
        inv_append(detail, sizeof(detail), " gpio ");
        inv_append_u32(detail, sizeof(detail), kbd->gpio_pin);
        inv_append(detail, sizeof(detail), " report-desc ");
        inv_append_u32(detail, sizeof(detail), kbd->report_desc_len);
    }
    if (kbd && kbd->online)
        inv_add(out_rows, max_rows, &count, "I2C HID", "ECKB Keyboard", "online", detail);

    tpd = i2c1_hidi2c_touchpad();
    detail[0] = 0;
    if (tpd)
    {
        inv_append(detail, sizeof(detail), "addr ");
        inv_append_hex64(detail, sizeof(detail), tpd->i2c_addr_7bit);
        inv_append(detail, sizeof(detail), " gpio ");
        inv_append_u32(detail, sizeof(detail), tpd->gpio_pin);
        inv_append(detail, sizeof(detail), " report-desc ");
        inv_append_u32(detail, sizeof(detail), tpd->report_desc_len);
    }
    if (tpd && tpd->online)
        inv_add(out_rows, max_rows, &count, "I2C HID", "TCPD Touchpad", "online", detail);

    if ((k_bootinfo_ptr && (k_bootinfo_ptr->pci_nic_count || k_bootinfo_ptr->wifi_fw_count)) ||
        kwifi_current_connected())
    {
        detail[0] = 0;
        inv_append(detail, sizeof(detail), "status ");
        inv_append(detail, sizeof(detail), kwifi_current_status());
        inv_append(detail, sizeof(detail), " auth ");
        inv_append(detail, sizeof(detail), kwifi_current_auth_mode());
        if (kwifi_current_connected())
        {
            inv_append(detail, sizeof(detail), " ssid ");
            inv_append(detail, sizeof(detail), kwifi_current_ssid());
        }
        inv_add(out_rows, max_rows, &count, "Network", "Wi-Fi", kwifi_current_connected() ? "connected" : "present", detail);
    }

    usb_ethernet_get_status(&eth_status);
    if (eth_status.online)
        inv_add(out_rows, max_rows, &count, "Network", eth_status.driver, "online", eth_status.detail);

    if (k_bootinfo_ptr && k_bootinfo_ptr->boot_volume_size_bytes)
    {
        detail[0] = 0;
        inv_append(detail, sizeof(detail), "snapshot bytes ");
        inv_append_hex64(detail, sizeof(detail), k_bootinfo_ptr->boot_volume_size_bytes);
        inv_add(out_rows, max_rows, &count, "Storage", "Boot Volume", "present", detail);
    }

    return count;
}
