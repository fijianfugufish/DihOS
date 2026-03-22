#include "kwrappers/kinput.h"
#include "i2c/i2c1_hidi2c.h"
#include <stdint.h>

static uint8_t g_keys_now[256];
static uint8_t g_keys_prev[256];
static kinput_mouse_state g_mouse;
static int16_t g_tpd_last_x = 0;
static int16_t g_tpd_last_y = 0;
static uint8_t g_tpd_have_last = 0;

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

static void parse_keyboard_report(const hidi2c_raw_report *r)
{
    uint32_t base = 0;
    uint8_t mods = 0;

    if (!r || !r->available || r->len < 8)
        return;

    keys_copy(g_keys_prev, g_keys_now);
    keys_clear(g_keys_now);

    if (r->len >= 9)
        base = 1;

    mods = r->data[base + 0];

    if (mods & 0x01)
        set_key(0xE0);
    if (mods & 0x02)
        set_key(0xE1);
    if (mods & 0x04)
        set_key(0xE2);
    if (mods & 0x08)
        set_key(0xE3);
    if (mods & 0x10)
        set_key(0xE4);
    if (mods & 0x20)
        set_key(0xE5);
    if (mods & 0x40)
        set_key(0xE6);
    if (mods & 0x80)
        set_key(0xE7);

    for (uint32_t i = 2; i < 8 && (base + i) < r->len; ++i)
    {
        uint8_t usage = r->data[base + i];
        if (usage != 0 && usage != 1)
            set_key(usage);
    }
}

static int16_t rd16s(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

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
            g_mouse.buttons = r->data[base + 0] & 0x07u;
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

        g_mouse.buttons = r->data[base + 0] & 0x07u;

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

void kinput_init(uint64_t rsdp_phys)
{
    keys_clear(g_keys_now);
    keys_clear(g_keys_prev);
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    g_mouse.buttons = 0;
    g_tpd_last_x = 0;
    g_tpd_last_y = 0;
    g_tpd_have_last = 0;

    i2c1_hidi2c_init(rsdp_phys);
}

void kinput_poll(void)
{
    const hidi2c_device *kbd;
    const hidi2c_device *tpd;

    i2c1_hidi2c_poll();

    kbd = i2c1_hidi2c_keyboard();
    tpd = i2c1_hidi2c_touchpad();

    if (kbd)
        parse_keyboard_report(&kbd->last_report);
    if (tpd)
        parse_touchpad_report(&tpd->last_report);
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
