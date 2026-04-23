#include "kwrappers/kinput.h"
#include "i2c/i2c1_hidi2c.h"
#include "usb/usb_hid.h"
#include <stdint.h>

static uint8_t g_keys_now[256];
static uint8_t g_keys_prev[256];
static kinput_mouse_state g_mouse;
static uint8_t g_tpd_buttons = 0;
static uint8_t g_usb_mouse_buttons = 0;
static int16_t g_tpd_last_x = 0;
static int16_t g_tpd_last_y = 0;
static uint8_t g_tpd_have_last = 0;

static usb_hid_t g_usb;
static uint8_t g_usb_ok = 0;

static void keys_clear(uint8_t *a)
{
    for (uint32_t i = 0; i < 256; ++i)
        a[i] = 0;
}

static void keys_copy(uint8_t *dst, const uint8_t *src)
{
    for (uint32_t i = 0; i < 256; ++i)
        dst[i] = src[i];
}

static void set_key(uint8_t usage)
{
    if (usage)
        g_keys_now[usage] = 1;
}

static int16_t rd16s(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t usb_hid_extract_bits(const uint8_t *data, uint32_t len, uint16_t bit_off, uint8_t bit_size)
{
    uint32_t v = 0;

    if (!data || bit_size == 0 || bit_size > 32)
        return 0;

    for (uint32_t i = 0; i < bit_size; ++i)
    {
        uint32_t abs_bit = (uint32_t)bit_off + i;
        uint32_t byte_idx = abs_bit >> 3;
        uint32_t bit_idx = abs_bit & 7u;

        if (byte_idx >= len)
            break;

        if ((data[byte_idx] >> bit_idx) & 1u)
            v |= (1u << i);
    }

    return v;
}

static int32_t usb_hid_sign_extend(uint32_t v, uint8_t bits)
{
    if (bits == 0 || bits >= 32)
        return (int32_t)v;

    if (v & (1u << (bits - 1u)))
        v |= ~((1u << bits) - 1u);

    return (int32_t)v;
}

static int32_t usb_hid_extract_signed_bits(const uint8_t *data, uint32_t len, uint16_t bit_off, uint8_t bit_size)
{
    return usb_hid_sign_extend(usb_hid_extract_bits(data, len, bit_off, bit_size), bit_size);
}

/* -------------------- keyboard parsers -------------------- */

static void merge_keyboard_boot_report(const uint8_t *data, uint32_t len)
{
    uint8_t mods = 0;

    if (!data || len < 8)
        return;

    mods = data[0];

    if (mods & 0x01) set_key(0xE0);
    if (mods & 0x02) set_key(0xE1);
    if (mods & 0x04) set_key(0xE2);
    if (mods & 0x08) set_key(0xE3);
    if (mods & 0x10) set_key(0xE4);
    if (mods & 0x20) set_key(0xE5);
    if (mods & 0x40) set_key(0xE6);
    if (mods & 0x80) set_key(0xE7);

    for (uint32_t i = 2; i < 8; ++i)
    {
        uint8_t usage = data[i];
        if (usage != 0 && usage != 1)
            set_key(usage);
    }
}

static void merge_i2c_keyboard_report(const hidi2c_raw_report *r)
{
    uint32_t base = 0;

    if (!r || !r->available || r->len < 8)
        return;

    if (r->len >= 9)
        base = 1;

    merge_keyboard_boot_report(&r->data[base], r->len - base);
}

/* -------------------- mouse parsers -------------------- */

static void parse_touchpad_report(const hidi2c_raw_report *r)
{
    uint32_t base = 0;

    if (!r || !r->available || r->len < 5)
        return;

    if (r->len >= 6)
        base = 1;

    if ((base + 6) <= r->len)
    {
        int16_t dx = rd16s(&r->data[base + 1]);
        int16_t dy = rd16s(&r->data[base + 3]);

        if (dx > -2048 && dx < 2048 && dy > -2048 && dy < 2048)
        {
            g_tpd_buttons = (uint8_t)(r->data[base + 0] & 0x07u);
            g_mouse.dx += dx;
            g_mouse.dy += dy;
            if ((base + 5) < r->len)
                g_mouse.wheel += (int8_t)r->data[base + 5];
            return;
        }
    }

    if ((base + 5) <= r->len)
    {
        int16_t x = rd16s(&r->data[base + 1]);
        int16_t y = rd16s(&r->data[base + 3]);

        g_tpd_buttons = (uint8_t)(r->data[base + 0] & 0x07u);

        if (g_tpd_have_last)
        {
            g_mouse.dx += (int32_t)(x - g_tpd_last_x);
            g_mouse.dy += (int32_t)(y - g_tpd_last_y);
        }

        g_tpd_last_x = x;
        g_tpd_last_y = y;
        g_tpd_have_last = 1;
    }
}

static void parse_usb_mouse_report(const usbh_dev_t *dev, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0)
        return;

    if (dev && dev->hid_mouse_valid)
    {
        const uint8_t *payload = data;
        uint32_t payload_len = len;
        uint8_t buttons = 0;
        int32_t dx = 0;
        int32_t dy = 0;
        int32_t wheel = 0;

        if (dev->hid_has_report_id)
        {
            if (payload_len == 0 || payload[0] != dev->hid_report_id)
                return;

            payload += 1;
            payload_len -= 1;
        }

        for (uint32_t i = 0; i < dev->hid_mouse_buttons && i < 3u; ++i)
        {
            uint16_t bit_off = (uint16_t)(dev->hid_mouse_btn_bits + (uint16_t)(i * dev->hid_mouse_btn_size));
            if (usb_hid_extract_bits(payload, payload_len, bit_off, dev->hid_mouse_btn_size) != 0)
                buttons |= (uint8_t)(1u << i);
        }

        dx = usb_hid_extract_signed_bits(payload, payload_len, dev->hid_mouse_x_bits, dev->hid_mouse_x_size);
        dy = usb_hid_extract_signed_bits(payload, payload_len, dev->hid_mouse_y_bits, dev->hid_mouse_y_size);

        if (dev->hid_mouse_wheel_size)
            wheel = usb_hid_extract_signed_bits(payload, payload_len, dev->hid_mouse_wheel_bits, dev->hid_mouse_wheel_size);

        g_usb_mouse_buttons = buttons;
        g_mouse.dx += dx;
        g_mouse.dy += dy;
        g_mouse.wheel += wheel;
        return;
    }

    if (len < 3)
        return;

    g_usb_mouse_buttons = (uint8_t)(data[0] & 0x07u);
    g_mouse.dx += (int8_t)data[1];
    g_mouse.dy += (int8_t)data[2];

    if (len >= 4)
        g_mouse.wheel += (int8_t)data[3];
}

/* -------------------- public API -------------------- */

void kinput_init(uint64_t xhci_mmio_base, uint64_t rsdp_phys)
{
    keys_clear(g_keys_now);
    keys_clear(g_keys_prev);

    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    g_mouse.buttons = 0;
    g_tpd_buttons = 0;
    g_usb_mouse_buttons = 0;

    g_tpd_last_x = 0;
    g_tpd_last_y = 0;
    g_tpd_have_last = 0;

    g_usb_ok = 0;
    g_usb.has_keyboard = 0;
    g_usb.has_mouse = 0;

    /* Existing ACPI/I2C HID path */
    i2c1_hidi2c_init(rsdp_phys);

    /* Also try USB HID.*/
    if (usb_hid_probe(&g_usb, xhci_mmio_base, rsdp_phys) == 0)
        g_usb_ok = 1;
}

void kinput_poll(void)
{
    const hidi2c_device *kbd;
    const hidi2c_device *tpd;
    uint8_t kbd_report[8];
    uint8_t mouse_report[16];
    uint32_t got = 0;

    /* Snapshot last frame once, then rebuild current frame from all devices */
    keys_copy(g_keys_prev, g_keys_now);
    keys_clear(g_keys_now);

    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;

    /* Poll ACPI/I2C HID devices */
    i2c1_hidi2c_poll();

    kbd = i2c1_hidi2c_keyboard();
    tpd = i2c1_hidi2c_touchpad();

    if (kbd)
        merge_i2c_keyboard_report(&kbd->last_report);

    if (tpd)
        parse_touchpad_report(&tpd->last_report);

    /* Poll USB HID devices too */
    if (g_usb_ok)
    {
        got = 0;
        if (g_usb.has_keyboard && usb_hid_kbd_read(&g_usb, kbd_report, &got) == 0 && got >= 8)
            merge_keyboard_boot_report(kbd_report, got);

        got = 0;
        if (g_usb.has_mouse && usb_hid_mouse_read(&g_usb, mouse_report, &got) == 0 && got >= 1)
            parse_usb_mouse_report(&g_usb.mouse, mouse_report, got);
    }

    g_mouse.buttons = (uint8_t)(g_tpd_buttons | g_usb_mouse_buttons);
}

int kinput_key_down(uint8_t usage)
{
    return g_keys_now[usage] != 0;
}

int kinput_key_pressed(uint8_t usage)
{
    return g_keys_now[usage] != 0 && g_keys_prev[usage] == 0;
}

int kinput_key_released(uint8_t usage)
{
    return g_keys_now[usage] == 0 && g_keys_prev[usage] != 0;
}

int32_t kinput_mouse_dx(void)
{
    return g_mouse.dx;
}

int32_t kinput_mouse_dy(void)
{
    return g_mouse.dy;
}

int32_t kinput_mouse_wheel(void)
{
    return g_mouse.wheel;
}

uint8_t kinput_mouse_buttons(void)
{
    return g_mouse.buttons;
}

void kinput_mouse_consume(kinput_mouse_state *out)
{
    if (out)
        *out = g_mouse;

    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
}
