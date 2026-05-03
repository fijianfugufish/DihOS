#include "kwrappers/kinput.h"
#include "i2c/i2c1_hidi2c.h"
#include "terminal/terminal_api.h"
#include "usb/usb_hid.h"
#include <stdint.h>

static uint8_t g_keys_now[256];
static uint8_t g_keys_prev[256];
static uint8_t g_i2c_kbd_keys[256];
static uint8_t g_usb_kbd_keys[256];
static kinput_mouse_state g_mouse;
static uint8_t g_tpd_buttons = 0;
static uint8_t g_usb_mouse_buttons = 0;
static int32_t g_tpd_last_x = 0;
static int32_t g_tpd_last_y = 0;
static uint8_t g_tpd_have_last = 0;
static int32_t g_tpd_motion_accum_x = 0;
static int32_t g_tpd_motion_accum_y = 0;

static usb_hid_t g_usb;
static uint8_t g_usb_ok = 0;

#define KINPUT_TPD_MAX_FINGERS 5u
#define KINPUT_TPD_MODE_NONE 0u
#define KINPUT_TPD_MODE_CURSOR 1u
#define KINPUT_TPD_MODE_SCROLL 2u
#define KINPUT_TPD_SCROLL_STEP 96
#define KINPUT_TPD_MOTION_PCT 35u
#define KINPUT_TPD_MOTION_PCT_MID 30u
#define KINPUT_TPD_MOTION_PCT_LOW 24u
#define KINPUT_TPD_MOTION_PCT_SLOW 18u
#define KINPUT_TPD_REL_SCROLL_START_DY 5
#define KINPUT_TPD_REL_SCROLL_MAX_DX 1
#define KINPUT_TPD_REL_SCROLL_STEP 6
#define KINPUT_TPD_DRAG_LOCK_POLLS 10u
#define KINPUT_TPD_DEBUG_RING 8u
#define KINPUT_TPD_DEBUG_LIVE_DUMPS 4u
#define KINPUT_KEY_A 0x04u
#define KINPUT_KEY_P 0x13u

typedef struct
{
    uint8_t data[128];
    uint32_t len;
    uint32_t seq;
    uint8_t available;
} kinput_tpd_debug_packet;

typedef struct
{
    uint8_t usage_page;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
    uint8_t has_report_id;
} kinput_hid_globals;

typedef struct
{
    uint16_t tip_bits;
    uint16_t id_bits;
    uint16_t x_bits;
    uint16_t y_bits;
    uint8_t tip_size;
    uint8_t id_size;
    uint8_t x_size;
    uint8_t y_size;
    uint8_t valid;
} kinput_tpd_finger_layout;

typedef struct
{
    uint8_t valid;
    uint8_t has_report_id;
    uint8_t report_id;
    uint8_t button_count;
    uint16_t button_bits;
    uint8_t button_size;
    uint16_t contact_count_bits;
    uint8_t contact_count_size;
    uint8_t finger_count;
    kinput_tpd_finger_layout fingers[KINPUT_TPD_MAX_FINGERS];
} kinput_tpd_mt_layout;

typedef struct
{
    uint8_t valid;
    uint8_t has_report_id;
    uint8_t report_id;
    uint8_t buttons;
    uint16_t btn_bits;
    uint16_t x_bits;
    uint16_t y_bits;
    uint16_t wheel_bits;
    uint8_t btn_size;
    uint8_t x_size;
    uint8_t y_size;
    uint8_t wheel_size;
} kinput_tpd_mouse_layout;

static kinput_tpd_mt_layout g_tpd_mt;
static kinput_tpd_mouse_layout g_tpd_mouse;
static uint8_t g_tpd_layout_ready = 0;
static uint8_t g_tpd_mode = KINPUT_TPD_MODE_NONE;
static int32_t g_tpd_scroll_accum_y = 0;
static uint8_t g_tpd_rel_scroll_pending = 0;
static int32_t g_tpd_rel_pending_dx = 0;
static int32_t g_tpd_rel_pending_dy = 0;
static uint8_t g_tpd_drag_lock_polls = 0;
static uint8_t g_tpd_latched_buttons = 0;
static kinput_tpd_debug_packet g_tpd_debug_ring[KINPUT_TPD_DEBUG_RING];
static uint8_t g_tpd_debug_ring_head = 0;
static uint8_t g_tpd_debug_ring_count = 0;
static uint8_t g_tpd_debug_live_budget = 0;
static uint32_t g_tpd_debug_seq = 0;

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

static void set_key_in(uint8_t *dst, uint8_t usage)
{
    if (dst && usage)
        dst[usage] = 1;
}

static void merge_key_bitmap(const uint8_t *src)
{
    if (!src)
        return;

    for (uint32_t i = 0; i < 256u; ++i)
    {
        if (src[i] != 0u)
            g_keys_now[i] = 1u;
    }
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

static int32_t kinput_abs_i32(int32_t v)
{
    return (v < 0) ? -v : v;
}

static int32_t tpd_scale_axis_with_accum(int32_t raw, int32_t *accum, uint32_t pct)
{
    int32_t emit = 0;

    if (!accum || raw == 0)
        return 0;

    *accum += raw * (int32_t)pct;
    emit = *accum / 100;
    *accum -= emit * 100;
    return emit;
}

static uint32_t tpd_motion_curve_pct(int32_t dx, int32_t dy)
{
    uint32_t mag_x = (uint32_t)kinput_abs_i32(dx);
    uint32_t mag_y = (uint32_t)kinput_abs_i32(dy);
    uint32_t mag = (mag_x > mag_y) ? mag_x : mag_y;

    if (mag <= 1u)
        return KINPUT_TPD_MOTION_PCT_SLOW;
    if (mag <= 3u)
        return KINPUT_TPD_MOTION_PCT_LOW;
    if (mag <= 6u)
        return KINPUT_TPD_MOTION_PCT_MID;

    return KINPUT_TPD_MOTION_PCT;
}

static uint32_t hid_item_u32(const uint8_t *p, uint8_t nbytes)
{
    uint32_t v = 0;

    if (!p)
        return 0;

    if (nbytes > 0u)
        v |= (uint32_t)p[0];
    if (nbytes > 1u)
        v |= (uint32_t)p[1] << 8;
    if (nbytes > 2u)
        v |= (uint32_t)p[2] << 16;
    if (nbytes > 3u)
        v |= (uint32_t)p[3] << 24;

    return v;
}

static uint32_t hid_usage_for_index(const uint32_t *usages, uint8_t usage_count,
                                    uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                    uint32_t index)
{
    if (index < usage_count)
        return usages[index];

    if (have_usage_range && usage_max >= usage_min)
    {
        uint32_t range_count = usage_max - usage_min + 1u;
        if (index < range_count)
            return usage_min + index;
    }

    return 0;
}

static void tpd_reset_tracking(void)
{
    g_tpd_last_x = 0;
    g_tpd_last_y = 0;
    g_tpd_have_last = 0;
    g_tpd_mode = KINPUT_TPD_MODE_NONE;
    g_tpd_scroll_accum_y = 0;
    g_tpd_rel_scroll_pending = 0u;
    g_tpd_rel_pending_dx = 0;
    g_tpd_rel_pending_dy = 0;
    g_tpd_drag_lock_polls = 0u;
    g_tpd_latched_buttons = 0u;
}

static void tpd_clear_layouts(void)
{
    g_tpd_mt = (kinput_tpd_mt_layout){0};
    g_tpd_mouse = (kinput_tpd_mouse_layout){0};
    g_tpd_layout_ready = 0;
}

static void tpd_add_cursor_motion(int32_t dx, int32_t dy)
{
    uint32_t pct = tpd_motion_curve_pct(dx, dy);

    g_mouse.dx += tpd_scale_axis_with_accum(dx, &g_tpd_motion_accum_x, pct);
    g_mouse.dy += tpd_scale_axis_with_accum(dy, &g_tpd_motion_accum_y, pct);
}

static void tpd_add_scroll_delta(int32_t dy, int32_t step)
{
    if (step <= 0)
        return;

    g_tpd_scroll_accum_y += dy;

    while (g_tpd_scroll_accum_y >= step)
    {
        g_mouse.wheel += 1;
        g_tpd_scroll_accum_y -= step;
    }

    while (g_tpd_scroll_accum_y <= -step)
    {
        g_mouse.wheel -= 1;
        g_tpd_scroll_accum_y += step;
    }
}

static uint8_t tpd_rel_scroll_candidate(int32_t dx, int32_t dy)
{
    return (kinput_abs_i32(dy) >= KINPUT_TPD_REL_SCROLL_START_DY &&
            kinput_abs_i32(dx) <= KINPUT_TPD_REL_SCROLL_MAX_DX) ? 1u : 0u;
}

static void tpd_clear_rel_scroll_pending(void)
{
    g_tpd_rel_scroll_pending = 0u;
    g_tpd_rel_pending_dx = 0;
    g_tpd_rel_pending_dy = 0;
}

static void tpd_flush_rel_scroll_pending_as_cursor(void)
{
    if (!g_tpd_rel_scroll_pending)
        return;

    tpd_add_cursor_motion(g_tpd_rel_pending_dx, g_tpd_rel_pending_dy);
    tpd_clear_rel_scroll_pending();
}

static void tpd_debug_reset(void)
{
    for (uint32_t i = 0; i < KINPUT_TPD_DEBUG_RING; ++i)
        g_tpd_debug_ring[i] = (kinput_tpd_debug_packet){0};

    g_tpd_debug_ring_head = 0u;
    g_tpd_debug_ring_count = 0u;
    g_tpd_debug_live_budget = 0u;
    g_tpd_debug_seq = 0u;
}

static void tpd_debug_print_packet_line(const char *tag, const kinput_tpd_debug_packet *pkt)
{
    uint32_t show = 0u;

    if (!tag || !pkt || !pkt->available)
        return;

    terminal_print("TCPD dbg ");
    terminal_print(tag);
    terminal_print(" seq:");
    terminal_print_hex32(pkt->seq);
    terminal_print(" len:");
    terminal_print_hex32(pkt->len);
    terminal_print(" data:");

    show = pkt->len;
    if (show > 24u)
        show = 24u;

    for (uint32_t i = 0; i < show; ++i)
    {
        terminal_print_inline_hex8(pkt->data[i]);
        terminal_print_inline(" ");
    }

    if (pkt->len > show)
        terminal_print_inline("...");

    terminal_print("\n");
}

static void tpd_debug_dump_descriptor_state(void)
{
    const hidi2c_device *tpd = i2c1_hidi2c_touchpad();

    if (!tpd)
    {
        terminal_print("TCPD dbg no touchpad device\n");
        return;
    }

    terminal_print("TCPD dbg online:");
    terminal_print_hex8(tpd->online);
    terminal_print(" hid_desc_reg:");
    terminal_print_hex32(tpd->hid_desc_reg);
    terminal_print(" rpt_valid:");
    terminal_print_hex8(tpd->report_desc_valid);
    terminal_print(" rpt_len:");
    terminal_print_hex32(tpd->report_desc_len);
    terminal_print("\n");

    terminal_print("TCPD dbg hid rpt_len:");
    terminal_print_hex32(tpd->desc.wReportDescLength);
    terminal_print(" rpt_reg:");
    terminal_print_hex32(tpd->desc.wReportDescRegister);
    terminal_print(" in_reg:");
    terminal_print_hex32(tpd->desc.wInputRegister);
    terminal_print(" max_in:");
    terminal_print_hex32(tpd->desc.wMaxInputLength);
    terminal_print("\n");

    if (tpd->report_desc_valid && tpd->report_desc_len != 0u)
    {
        uint32_t show = tpd->report_desc_len;

        if (show > 160u)
            show = 160u;

        for (uint32_t i = 0; i < show; i += 16u)
        {
            uint32_t line_end = i + 16u;
            if (line_end > show)
                line_end = show;

            terminal_print("TCPD dbg rpt ");
            terminal_print_hex32(i);
            terminal_print(":");

            for (uint32_t j = i; j < line_end; ++j)
            {
                terminal_print_inline_hex8(tpd->report_desc[j]);
                terminal_print_inline(" ");
            }

            if (line_end < tpd->report_desc_len && line_end == show)
                terminal_print_inline("...");

            terminal_print("\n");
        }
    }
}

static void tpd_debug_store_report(const hidi2c_raw_report *r)
{
    kinput_tpd_debug_packet *slot = 0;

    if (!r || !r->available || r->len == 0u)
        return;

    slot = &g_tpd_debug_ring[g_tpd_debug_ring_head];
    *slot = (kinput_tpd_debug_packet){0};
    slot->len = r->len;
    if (slot->len > sizeof(slot->data))
        slot->len = sizeof(slot->data);
    slot->seq = ++g_tpd_debug_seq;
    slot->available = 1u;

    for (uint32_t i = 0; i < slot->len; ++i)
        slot->data[i] = r->data[i];

    g_tpd_debug_ring_head = (uint8_t)((g_tpd_debug_ring_head + 1u) % KINPUT_TPD_DEBUG_RING);
    if (g_tpd_debug_ring_count < KINPUT_TPD_DEBUG_RING)
        g_tpd_debug_ring_count++;

    if (g_tpd_debug_live_budget > 0u)
    {
        tpd_debug_print_packet_line("live", slot);
        g_tpd_debug_live_budget--;
    }
}

static void tpd_debug_dump_recent(void)
{
    terminal_print("TCPD dbg dump begin\n");

    if (g_tpd_mt.valid)
        terminal_print("TCPD dbg parser: multitouch\n");
    else if (g_tpd_mouse.valid)
        terminal_print("TCPD dbg parser: hid-mouse\n");
    else
        terminal_print("TCPD dbg parser: heuristic\n");

    tpd_debug_dump_descriptor_state();

    if (g_tpd_debug_ring_count == 0u)
    {
        terminal_print("TCPD dbg no cached packets\n");
    }
    else
    {
        uint8_t start = 0u;

        if (g_tpd_debug_ring_count < KINPUT_TPD_DEBUG_RING)
            start = 0u;
        else
            start = g_tpd_debug_ring_head;

        for (uint32_t i = 0; i < g_tpd_debug_ring_count; ++i)
        {
            uint8_t idx = (uint8_t)((start + i) % KINPUT_TPD_DEBUG_RING);
            tpd_debug_print_packet_line("cache", &g_tpd_debug_ring[idx]);
        }
    }

    g_tpd_debug_live_budget = KINPUT_TPD_DEBUG_LIVE_DUMPS;
    terminal_print("TCPD dbg armed live:");
    terminal_print_hex32(g_tpd_debug_live_budget);
    terminal_print("\n");
}

static void tpd_capture_mouse_input(kinput_tpd_mouse_layout *out, const kinput_hid_globals *g,
                                    const uint32_t *usages, uint8_t usage_count,
                                    uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                    uint16_t bit_base)
{
    uint8_t report_id = 0;
    uint8_t buttons = 0;

    if (!out || !g || g->report_size == 0u || g->report_count == 0u)
        return;

    report_id = g->has_report_id ? g->report_id : 0u;
    if (out->valid || out->buttons || out->x_size || out->y_size || out->wheel_size)
    {
        uint8_t out_report_id = out->has_report_id ? out->report_id : 0u;
        if (out_report_id != report_id)
            return;
    }
    else
    {
        out->has_report_id = g->has_report_id;
        out->report_id = report_id;
    }

    if (g->usage_page == 0x09u && out->buttons == 0u)
    {
        if (have_usage_range && usage_max >= usage_min && usage_min <= 8u)
        {
            uint32_t count = usage_max - usage_min + 1u;
            if (count > g->report_count)
                count = g->report_count;
            if (count > 8u)
                count = 8u;
            buttons = (uint8_t)count;
        }
        else if (usage_count > 0u)
        {
            buttons = usage_count;
            if (buttons > g->report_count)
                buttons = (uint8_t)g->report_count;
            if (buttons > 8u)
                buttons = 8u;
        }

        if (buttons > 0u)
        {
            out->buttons = buttons;
            out->btn_bits = bit_base;
            out->btn_size = (uint8_t)g->report_size;
        }
    }

    if (g->usage_page == 0x01u)
    {
        for (uint32_t i = 0; i < g->report_count; ++i)
        {
            uint32_t usage = hid_usage_for_index(usages, usage_count, have_usage_range, usage_min, usage_max, i);
            uint16_t bit_off = (uint16_t)(bit_base + (uint16_t)(i * g->report_size));

            if (usage == 0x30u && out->x_size == 0u)
            {
                out->x_bits = bit_off;
                out->x_size = (uint8_t)g->report_size;
            }
            else if (usage == 0x31u && out->y_size == 0u)
            {
                out->y_bits = bit_off;
                out->y_size = (uint8_t)g->report_size;
            }
            else if (usage == 0x38u && out->wheel_size == 0u)
            {
                out->wheel_bits = bit_off;
                out->wheel_size = (uint8_t)g->report_size;
            }
        }
    }

    if (out->buttons > 0u && out->x_size > 0u && out->y_size > 0u)
        out->valid = 1u;
}

static int tpd_parse_mouse_report_desc(const uint8_t *desc, uint16_t len, kinput_tpd_mouse_layout *out)
{
    kinput_hid_globals g;
    kinput_hid_globals g_stack[4];
    uint16_t bit_cursor[256];
    uint32_t usages[16];
    uint8_t usage_count = 0;
    uint8_t have_usage_range = 0;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
    uint8_t collection_mouse[16];
    uint8_t collection_depth = 0;
    uint8_t stack_depth = 0;
    uint16_t i = 0;

    if (!desc || !out)
        return -1;

    g = (kinput_hid_globals){0};
    for (uint32_t k = 0; k < 256u; ++k)
        bit_cursor[k] = 0u;
    for (uint32_t k = 0; k < 16u; ++k)
        collection_mouse[k] = 0u;

    *out = (kinput_tpd_mouse_layout){0};

    while (i < len)
    {
        uint8_t b = desc[i++];
        uint8_t size_code;
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t val;
        uint8_t current_mouse = (collection_depth > 0u) ? collection_mouse[collection_depth - 1u] : 0u;

        if (b == 0xFEu)
        {
            uint8_t long_size;
            if (i + 1u >= len)
                break;
            long_size = desc[i];
            i += 2u;
            if ((uint32_t)i + long_size > len)
                break;
            i = (uint16_t)(i + long_size);
            continue;
        }

        size_code = b & 0x03u;
        size = (size_code == 3u) ? 4u : size_code;
        type = (b >> 2) & 0x03u;
        tag = (b >> 4) & 0x0Fu;

        if ((uint32_t)i + size > len)
            break;

        val = hid_item_u32(&desc[i], size);

        if (type == 0u)
        {
            if (tag == 8u)
            {
                if (current_mouse && (val & 0x01u) == 0u)
                    tpd_capture_mouse_input(out, &g, usages, usage_count, have_usage_range, usage_min, usage_max, bit_cursor[g.report_id]);

                bit_cursor[g.report_id] = (uint16_t)(bit_cursor[g.report_id] + (uint16_t)(g.report_size * g.report_count));
            }
            else if (tag == 10u)
            {
                uint32_t usage = 0u;
                uint8_t is_mouse = current_mouse;

                if (usage_count > 0u)
                    usage = usages[usage_count - 1u];
                else if (have_usage_range)
                    usage = usage_min;

                if ((val & 0xFFu) == 1u && g.usage_page == 0x01u && usage == 0x02u)
                    is_mouse = 1u;

                if (collection_depth < 16u)
                    collection_mouse[collection_depth++] = is_mouse;
            }
            else if (tag == 12u)
            {
                if (collection_depth > 0u)
                    collection_depth--;
            }

            usage_count = 0u;
            have_usage_range = 0u;
        }
        else if (type == 1u)
        {
            if (tag == 0u)
                g.usage_page = (uint8_t)val;
            else if (tag == 7u)
                g.report_size = val;
            else if (tag == 8u)
            {
                g.report_id = (uint8_t)val;
                g.has_report_id = 1u;
            }
            else if (tag == 9u)
                g.report_count = val;
            else if (tag == 10u)
            {
                if (stack_depth < 4u)
                    g_stack[stack_depth++] = g;
            }
            else if (tag == 11u)
            {
                if (stack_depth > 0u)
                    g = g_stack[--stack_depth];
            }
        }
        else if (type == 2u)
        {
            if (tag == 0u)
            {
                if (usage_count < 16u)
                    usages[usage_count++] = val;
            }
            else if (tag == 1u)
            {
                usage_min = val;
                have_usage_range = 1u;
            }
            else if (tag == 2u)
            {
                usage_max = val;
                have_usage_range = 1u;
            }
        }

        i = (uint16_t)(i + size);
    }

    return out->valid ? 0 : -1;
}

static void tpd_capture_mt_input(kinput_tpd_mt_layout *out, const kinput_hid_globals *g,
                                 const uint32_t *usages, uint8_t usage_count,
                                 uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                 uint16_t bit_base, int finger_slot)
{
    uint8_t report_id = 0;

    if (!out || !g || g->report_size == 0u || g->report_count == 0u)
        return;

    report_id = g->has_report_id ? g->report_id : 0u;
    if (out->finger_count || out->button_count || out->contact_count_size)
    {
        uint8_t out_report_id = out->has_report_id ? out->report_id : 0u;
        if (out_report_id != report_id)
            return;
    }
    else
    {
        out->has_report_id = g->has_report_id;
        out->report_id = report_id;
    }

    if (finger_slot >= 0 && finger_slot < (int)KINPUT_TPD_MAX_FINGERS)
    {
        kinput_tpd_finger_layout *finger = &out->fingers[(uint32_t)finger_slot];

        for (uint32_t i = 0; i < g->report_count; ++i)
        {
            uint32_t usage = hid_usage_for_index(usages, usage_count, have_usage_range, usage_min, usage_max, i);
            uint16_t bit_off = (uint16_t)(bit_base + (uint16_t)(i * g->report_size));

            if (g->usage_page == 0x0Du)
            {
                if (usage == 0x42u && finger->tip_size == 0u)
                {
                    finger->tip_bits = bit_off;
                    finger->tip_size = (uint8_t)g->report_size;
                }
                else if (usage == 0x51u && finger->id_size == 0u)
                {
                    finger->id_bits = bit_off;
                    finger->id_size = (uint8_t)g->report_size;
                }
            }
            else if (g->usage_page == 0x01u)
            {
                if (usage == 0x30u && finger->x_size == 0u)
                {
                    finger->x_bits = bit_off;
                    finger->x_size = (uint8_t)g->report_size;
                }
                else if (usage == 0x31u && finger->y_size == 0u)
                {
                    finger->y_bits = bit_off;
                    finger->y_size = (uint8_t)g->report_size;
                }
            }
        }

        if (finger->x_size > 0u && finger->y_size > 0u)
        {
            finger->valid = 1u;
            if (out->finger_count < (uint8_t)(finger_slot + 1))
                out->finger_count = (uint8_t)(finger_slot + 1);
        }
        return;
    }

    if (g->usage_page == 0x09u && out->button_count == 0u)
    {
        uint8_t buttons = 0u;

        if (have_usage_range && usage_max >= usage_min && usage_min <= 8u)
        {
            uint32_t count = usage_max - usage_min + 1u;
            if (count > g->report_count)
                count = g->report_count;
            if (count > 8u)
                count = 8u;
            buttons = (uint8_t)count;
        }
        else if (usage_count > 0u)
        {
            buttons = usage_count;
            if (buttons > g->report_count)
                buttons = (uint8_t)g->report_count;
            if (buttons > 8u)
                buttons = 8u;
        }

        if (buttons > 0u)
        {
            out->button_count = buttons;
            out->button_bits = bit_base;
            out->button_size = (uint8_t)g->report_size;
        }
    }

    for (uint32_t i = 0; i < g->report_count; ++i)
    {
        uint32_t usage = hid_usage_for_index(usages, usage_count, have_usage_range, usage_min, usage_max, i);
        uint16_t bit_off = (uint16_t)(bit_base + (uint16_t)(i * g->report_size));

        if (g->usage_page == 0x0Du && usage == 0x54u && out->contact_count_size == 0u)
        {
            out->contact_count_bits = bit_off;
            out->contact_count_size = (uint8_t)g->report_size;
        }
        else if (g->usage_page == 0x01u)
        {
            kinput_tpd_finger_layout *finger = &out->fingers[0];

            if (usage == 0x30u && finger->x_size == 0u)
            {
                finger->x_bits = bit_off;
                finger->x_size = (uint8_t)g->report_size;
            }
            else if (usage == 0x31u && finger->y_size == 0u)
            {
                finger->y_bits = bit_off;
                finger->y_size = (uint8_t)g->report_size;
            }

            if (finger->x_size > 0u && finger->y_size > 0u)
            {
                finger->valid = 1u;
                if (out->finger_count == 0u)
                    out->finger_count = 1u;
            }
        }
    }
}

static int tpd_parse_mt_report_desc(const uint8_t *desc, uint16_t len, kinput_tpd_mt_layout *out)
{
    kinput_hid_globals g;
    kinput_hid_globals g_stack[4];
    uint16_t bit_cursor[256];
    uint32_t usages[16];
    uint8_t usage_count = 0u;
    uint8_t have_usage_range = 0u;
    uint32_t usage_min = 0u;
    uint32_t usage_max = 0u;
    uint8_t collection_touchpad[16];
    int8_t collection_finger_slot[16];
    uint8_t collection_depth = 0u;
    uint8_t stack_depth = 0u;
    uint8_t next_finger_slot = 0u;
    uint16_t i = 0u;

    if (!desc || !out)
        return -1;

    g = (kinput_hid_globals){0};
    *out = (kinput_tpd_mt_layout){0};

    for (uint32_t k = 0; k < 256u; ++k)
        bit_cursor[k] = 0u;
    for (uint32_t k = 0; k < 16u; ++k)
    {
        collection_touchpad[k] = 0u;
        collection_finger_slot[k] = -1;
    }

    while (i < len)
    {
        uint8_t b = desc[i++];
        uint8_t size_code;
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t val;
        uint8_t current_touchpad = (collection_depth > 0u) ? collection_touchpad[collection_depth - 1u] : 0u;
        int current_finger_slot = (collection_depth > 0u) ? collection_finger_slot[collection_depth - 1u] : -1;

        if (b == 0xFEu)
        {
            uint8_t long_size;
            if (i + 1u >= len)
                break;
            long_size = desc[i];
            i += 2u;
            if ((uint32_t)i + long_size > len)
                break;
            i = (uint16_t)(i + long_size);
            continue;
        }

        size_code = b & 0x03u;
        size = (size_code == 3u) ? 4u : size_code;
        type = (b >> 2) & 0x03u;
        tag = (b >> 4) & 0x0Fu;

        if ((uint32_t)i + size > len)
            break;

        val = hid_item_u32(&desc[i], size);

        if (type == 0u)
        {
            if (tag == 8u)
            {
                if (current_touchpad && (val & 0x01u) == 0u)
                    tpd_capture_mt_input(out, &g, usages, usage_count, have_usage_range, usage_min, usage_max, bit_cursor[g.report_id], current_finger_slot);

                bit_cursor[g.report_id] = (uint16_t)(bit_cursor[g.report_id] + (uint16_t)(g.report_size * g.report_count));
            }
            else if (tag == 10u)
            {
                uint32_t usage = 0u;
                uint8_t is_touchpad = current_touchpad;
                int new_finger_slot = current_finger_slot;

                if (usage_count > 0u)
                    usage = usages[usage_count - 1u];
                else if (have_usage_range)
                    usage = usage_min;

                if ((val & 0xFFu) == 1u && g.usage_page == 0x0Du && usage == 0x05u)
                    is_touchpad = 1u;

                if (is_touchpad && g.usage_page == 0x0Du && usage == 0x22u &&
                    new_finger_slot < 0 && next_finger_slot < KINPUT_TPD_MAX_FINGERS)
                    new_finger_slot = (int)next_finger_slot++;

                if (collection_depth < 16u)
                {
                    collection_touchpad[collection_depth] = is_touchpad;
                    collection_finger_slot[collection_depth] = (int8_t)new_finger_slot;
                    collection_depth++;
                }
            }
            else if (tag == 12u)
            {
                if (collection_depth > 0u)
                    collection_depth--;
            }

            usage_count = 0u;
            have_usage_range = 0u;
        }
        else if (type == 1u)
        {
            if (tag == 0u)
                g.usage_page = (uint8_t)val;
            else if (tag == 7u)
                g.report_size = val;
            else if (tag == 8u)
            {
                g.report_id = (uint8_t)val;
                g.has_report_id = 1u;
            }
            else if (tag == 9u)
                g.report_count = val;
            else if (tag == 10u)
            {
                if (stack_depth < 4u)
                    g_stack[stack_depth++] = g;
            }
            else if (tag == 11u)
            {
                if (stack_depth > 0u)
                    g = g_stack[--stack_depth];
            }
        }
        else if (type == 2u)
        {
            if (tag == 0u)
            {
                if (usage_count < 16u)
                    usages[usage_count++] = val;
            }
            else if (tag == 1u)
            {
                usage_min = val;
                have_usage_range = 1u;
            }
            else if (tag == 2u)
            {
                usage_max = val;
                have_usage_range = 1u;
            }
        }

        i = (uint16_t)(i + size);
    }

    for (uint32_t finger = 0; finger < KINPUT_TPD_MAX_FINGERS; ++finger)
    {
        if (out->fingers[finger].valid)
        {
            out->valid = 1u;
            break;
        }
    }

    return out->valid ? 0 : -1;
}

static void maybe_configure_touchpad_layouts(const hidi2c_device *tpd)
{
    if (g_tpd_layout_ready)
        return;

    if (!tpd || !tpd->report_desc_valid || tpd->report_desc_len == 0u)
        return;

    g_tpd_mt = (kinput_tpd_mt_layout){0};
    g_tpd_mouse = (kinput_tpd_mouse_layout){0};

    (void)tpd_parse_mt_report_desc(tpd->report_desc, tpd->report_desc_len, &g_tpd_mt);
    (void)tpd_parse_mouse_report_desc(tpd->report_desc, tpd->report_desc_len, &g_tpd_mouse);

    g_tpd_layout_ready = 1u;
}

/* -------------------- keyboard parsers -------------------- */

static void decode_keyboard_boot_report(uint8_t *dst, const uint8_t *data, uint32_t len)
{
    uint8_t mods = 0;

    if (!dst)
        return;

    keys_clear(dst);

    if (!data || len < 8u)
        return;

    mods = data[0];

    if (mods & 0x01u) set_key_in(dst, 0xE0u);
    if (mods & 0x02u) set_key_in(dst, 0xE1u);
    if (mods & 0x04u) set_key_in(dst, 0xE2u);
    if (mods & 0x08u) set_key_in(dst, 0xE3u);
    if (mods & 0x10u) set_key_in(dst, 0xE4u);
    if (mods & 0x20u) set_key_in(dst, 0xE5u);
    if (mods & 0x40u) set_key_in(dst, 0xE6u);
    if (mods & 0x80u) set_key_in(dst, 0xE7u);

    for (uint32_t i = 2u; i < 8u; ++i)
    {
        uint8_t usage = data[i];
        if (usage != 0u && usage != 1u)
            set_key_in(dst, usage);
    }
}

static uint8_t decode_i2c_keyboard_report_into(uint8_t *dst, const hidi2c_raw_report *r)
{
    uint32_t base = 0u;

    if (!dst || !r || !r->available || r->len < 8u)
        return 0u;

    if (r->len >= 9u)
        base = 1u;

    decode_keyboard_boot_report(dst, &r->data[base], r->len - base);
    return 1u;
}

/* -------------------- mouse parsers -------------------- */

static uint8_t parse_touchpad_report_id2_rel_report(const hidi2c_raw_report *r)
{
    int32_t dx = 0;
    int32_t dy = 0;
    uint8_t buttons = 0;

    if (!r || !r->available || r->len != 4u)
        return 0;

    /*
    Observed TCPD packets are currently:
      [0] report id (0x02)
      [1] low button bits
      [2] relative horizontal-ish delta
      [3] relative vertical / scroll-ish delta
    This keeps byte0 out of the button field and stops treating byte3 as
    unconditional wheel data.
    */
    if (r->data[0] != 0x02u || (r->data[1] & 0xF8u) != 0u)
        return 0;

    buttons = (uint8_t)(r->data[1] & 0x07u);
    dx = (int8_t)r->data[2];
    dy = (int8_t)r->data[3];

    if (buttons != 0u)
    {
        tpd_flush_rel_scroll_pending_as_cursor();
        g_tpd_buttons = buttons;
        g_tpd_latched_buttons = buttons;
        g_tpd_drag_lock_polls = KINPUT_TPD_DRAG_LOCK_POLLS;
        g_tpd_mode = KINPUT_TPD_MODE_CURSOR;
        g_tpd_scroll_accum_y = 0;
        tpd_add_cursor_motion(dx, dy);
        return 1;
    }

    if (g_tpd_drag_lock_polls > 0u &&
        g_tpd_latched_buttons != 0u &&
        (dx != 0 || dy != 0))
    {
        tpd_flush_rel_scroll_pending_as_cursor();
        g_tpd_buttons = g_tpd_latched_buttons;
        g_tpd_drag_lock_polls--;
        g_tpd_mode = KINPUT_TPD_MODE_CURSOR;
        g_tpd_scroll_accum_y = 0;
        tpd_add_cursor_motion(dx, dy);
        return 1;
    }

    g_tpd_buttons = 0u;
    g_tpd_drag_lock_polls = 0u;
    g_tpd_latched_buttons = 0u;

    if (tpd_rel_scroll_candidate(dx, dy))
    {
        if (g_tpd_mode == KINPUT_TPD_MODE_SCROLL)
        {
            tpd_clear_rel_scroll_pending();
            tpd_add_scroll_delta(dy, KINPUT_TPD_REL_SCROLL_STEP);
            return 1;
        }

        if (g_tpd_rel_scroll_pending)
        {
            if ((g_tpd_rel_pending_dy < 0 && dy < 0) ||
                (g_tpd_rel_pending_dy > 0 && dy > 0))
            {
                g_tpd_mode = KINPUT_TPD_MODE_SCROLL;
                tpd_add_scroll_delta(g_tpd_rel_pending_dy, KINPUT_TPD_REL_SCROLL_STEP);
                tpd_add_scroll_delta(dy, KINPUT_TPD_REL_SCROLL_STEP);
                tpd_clear_rel_scroll_pending();
                return 1;
            }

            tpd_flush_rel_scroll_pending_as_cursor();
        }

        g_tpd_rel_scroll_pending = 1u;
        g_tpd_rel_pending_dx = dx;
        g_tpd_rel_pending_dy = dy;
        return 1;
    }

    tpd_flush_rel_scroll_pending_as_cursor();
    g_tpd_mode = KINPUT_TPD_MODE_CURSOR;
    g_tpd_scroll_accum_y = 0;
    tpd_add_cursor_motion(dx, dy);
    return 1;
}

static uint8_t parse_touchpad_boot_report(const hidi2c_raw_report *r)
{
    uint32_t base = 0;

    if (!r || !r->available || r->len < 3u)
        return 0;

    if (r->len == 5u && r->data[0] == 1u && (r->data[1] & 0xF8u) == 0u)
    {
        base = 1u;
    }
    else if (r->len == 4u && (r->data[0] & 0xF8u) == 0u)
    {
        base = 0u;
    }
    else if (r->len == 4u && (r->data[1] & 0xF8u) == 0u)
    {
        base = 1u;
    }
    else if (r->len == 3u && (r->data[0] & 0xF8u) == 0u)
    {
        base = 0u;
    }
    else
    {
        return 0;
    }

    tpd_reset_tracking();
    g_tpd_buttons = (uint8_t)(r->data[base + 0u] & 0x07u);
    tpd_add_cursor_motion((int8_t)r->data[base + 1u],
                          (int8_t)r->data[base + 2u]);

    return 1;
}

static uint8_t parse_touchpad_hid_mouse_report(const hidi2c_raw_report *r)
{
    const uint8_t *payload = 0;
    uint32_t payload_len = 0;
    uint8_t buttons = 0u;
    uint8_t effective_buttons = 0u;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t wheel = 0;

    if (!r || !r->available || !g_tpd_mouse.valid)
        return 0;

    payload = r->data;
    payload_len = r->len;

    if (g_tpd_mouse.has_report_id)
    {
        if (payload_len == 0u || payload[0] != g_tpd_mouse.report_id)
            return 0;

        payload += 1;
        payload_len -= 1;
    }

    for (uint32_t i = 0; i < g_tpd_mouse.buttons && i < 3u; ++i)
    {
        uint16_t bit_off = (uint16_t)(g_tpd_mouse.btn_bits + (uint16_t)(i * g_tpd_mouse.btn_size));
        if (usb_hid_extract_bits(payload, payload_len, bit_off, g_tpd_mouse.btn_size) != 0u)
            buttons |= (uint8_t)(1u << i);
    }

    dx = usb_hid_extract_signed_bits(payload, payload_len, g_tpd_mouse.x_bits, g_tpd_mouse.x_size);
    dy = usb_hid_extract_signed_bits(payload, payload_len, g_tpd_mouse.y_bits, g_tpd_mouse.y_size);

    if (g_tpd_mouse.wheel_size)
        wheel = usb_hid_extract_signed_bits(payload, payload_len, g_tpd_mouse.wheel_bits, g_tpd_mouse.wheel_size);

    /*
      HID mouse packets are relative, so drop any absolute anchor from the
      multitouch path. Keep the drag latch alive here though, because this is
      the path that now handles the real TCPD button bit.
    */
    g_tpd_last_x = 0;
    g_tpd_last_y = 0;
    g_tpd_have_last = 0u;
    g_tpd_mode = KINPUT_TPD_MODE_CURSOR;
    g_tpd_scroll_accum_y = 0;
    tpd_clear_rel_scroll_pending();

    if (buttons != 0u)
    {
        effective_buttons = buttons;
        g_tpd_latched_buttons = buttons;
        g_tpd_drag_lock_polls = KINPUT_TPD_DRAG_LOCK_POLLS;
    }
    else if (g_tpd_latched_buttons != 0u && g_tpd_drag_lock_polls > 0u)
    {
        effective_buttons = g_tpd_latched_buttons;
        g_tpd_drag_lock_polls--;
    }
    else
    {
        g_tpd_latched_buttons = 0u;
        g_tpd_drag_lock_polls = 0u;
    }

    g_tpd_buttons = effective_buttons;

    if (dx != 0 || dy != 0)
        tpd_add_cursor_motion(dx, dy);

    g_mouse.wheel += wheel;
    return 1;
}

static uint8_t parse_touchpad_mt_report(const hidi2c_raw_report *r)
{
    const uint8_t *payload = 0;
    uint32_t payload_len = 0;
    uint8_t buttons = 0u;
    uint32_t contact_count = 0u;
    uint32_t active_count = 0u;
    int32_t sum_x = 0;
    int32_t sum_y = 0;

    if (!r || !r->available || !g_tpd_mt.valid)
        return 0;

    payload = r->data;
    payload_len = r->len;

    if (g_tpd_mt.has_report_id)
    {
        if (payload_len == 0u || payload[0] != g_tpd_mt.report_id)
            return 0;

        payload += 1;
        payload_len -= 1;
    }

    for (uint32_t i = 0; i < g_tpd_mt.button_count && i < 3u; ++i)
    {
        uint16_t bit_off = (uint16_t)(g_tpd_mt.button_bits + (uint16_t)(i * g_tpd_mt.button_size));
        if (usb_hid_extract_bits(payload, payload_len, bit_off, g_tpd_mt.button_size) != 0u)
            buttons |= (uint8_t)(1u << i);
    }

    if (g_tpd_mt.contact_count_size)
        contact_count = usb_hid_extract_bits(payload, payload_len, g_tpd_mt.contact_count_bits, g_tpd_mt.contact_count_size);

    g_tpd_buttons = buttons;

    for (uint32_t finger_idx = 0; finger_idx < g_tpd_mt.finger_count && finger_idx < KINPUT_TPD_MAX_FINGERS; ++finger_idx)
    {
        const kinput_tpd_finger_layout *finger = &g_tpd_mt.fingers[finger_idx];
        uint8_t active = 1u;
        int32_t x = 0;
        int32_t y = 0;

        if (!finger->valid)
            continue;

        if (finger->tip_size)
            active = usb_hid_extract_bits(payload, payload_len, finger->tip_bits, finger->tip_size) != 0u;
        else if (g_tpd_mt.contact_count_size && active_count >= contact_count)
            active = 0u;

        if (!active)
            continue;

        x = (int32_t)usb_hid_extract_bits(payload, payload_len, finger->x_bits, finger->x_size);
        y = (int32_t)usb_hid_extract_bits(payload, payload_len, finger->y_bits, finger->y_size);

        sum_x += x;
        sum_y += y;
        active_count++;
    }

    if (active_count == 0u)
    {
        tpd_reset_tracking();
        return 1;
    }

    {
        int32_t cx = sum_x / (int32_t)active_count;
        int32_t cy = sum_y / (int32_t)active_count;

        if (active_count >= 2u)
        {
            if (g_tpd_have_last && g_tpd_mode == KINPUT_TPD_MODE_SCROLL)
            {
                tpd_add_scroll_delta(cy - g_tpd_last_y, KINPUT_TPD_SCROLL_STEP);
            }
            else
            {
                g_tpd_scroll_accum_y = 0;
            }

            g_tpd_mode = KINPUT_TPD_MODE_SCROLL;
        }
        else
        {
            if (g_tpd_have_last && g_tpd_mode == KINPUT_TPD_MODE_CURSOR)
            {
                tpd_add_cursor_motion(cx - g_tpd_last_x,
                                      cy - g_tpd_last_y);
            }

            g_tpd_scroll_accum_y = 0;
            g_tpd_mode = KINPUT_TPD_MODE_CURSOR;
        }

        g_tpd_last_x = cx;
        g_tpd_last_y = cy;
        g_tpd_have_last = 1u;
    }

    return 1;
}

static void parse_touchpad_report(const hidi2c_raw_report *r)
{
    uint32_t base = 0;

    if (!r || !r->available)
        return;

    if (parse_touchpad_mt_report(r))
        return;

    if (parse_touchpad_hid_mouse_report(r))
        return;

    if (parse_touchpad_report_id2_rel_report(r))
        return;

    if (parse_touchpad_boot_report(r))
        return;

    if (r->len < 5u)
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
            tpd_add_cursor_motion(dx, dy);
            tpd_reset_tracking();
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
            tpd_add_cursor_motion((int32_t)(x - g_tpd_last_x),
                                  (int32_t)(y - g_tpd_last_y));
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
    const uint64_t *bases = xhci_mmio_base ? &xhci_mmio_base : 0;
    uint32_t count = xhci_mmio_base ? 1u : 0u;

    kinput_init_multi(bases, count, rsdp_phys);
}

void kinput_init_multi(const uint64_t *xhci_mmio_bases, uint32_t xhci_mmio_count, uint64_t rsdp_phys)
{
    keys_clear(g_keys_now);
    keys_clear(g_keys_prev);
    keys_clear(g_i2c_kbd_keys);
    keys_clear(g_usb_kbd_keys);

    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    g_mouse.buttons = 0;
    g_tpd_buttons = 0;
    g_usb_mouse_buttons = 0;
    g_tpd_motion_accum_x = 0;
    g_tpd_motion_accum_y = 0;

    tpd_reset_tracking();
    tpd_clear_layouts();
    tpd_debug_reset();

    g_usb_ok = 0;
    g_usb.has_keyboard = 0;
    g_usb.has_mouse = 0;

    /* Existing ACPI/I2C HID path */
    terminal_set_quiet();
    i2c1_hidi2c_init(rsdp_phys);
    maybe_configure_touchpad_layouts(i2c1_hidi2c_touchpad());
    terminal_set_loud();

    terminal_flush_log();

    /* Also try USB HID.*/
    /* disable for now
    if (usb_hid_probe_multi(&g_usb, xhci_mmio_bases, xhci_mmio_count, rsdp_phys) == 0)
        g_usb_ok = 1;
    */
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
    {
        (void)decode_i2c_keyboard_report_into(g_i2c_kbd_keys, &kbd->last_report);
        merge_key_bitmap(g_i2c_kbd_keys);
    }

    if (tpd)
    {
        maybe_configure_touchpad_layouts(tpd);
        tpd_debug_store_report(&tpd->last_report);
        if (tpd->last_report.available)
            parse_touchpad_report(&tpd->last_report);
        else
            tpd_flush_rel_scroll_pending_as_cursor();
    }
    else
    {
        tpd_flush_rel_scroll_pending_as_cursor();
    }

    /* Poll USB HID devices too */
    if (g_usb_ok)
    {
        got = 0;
        if (g_usb.has_keyboard && usb_hid_kbd_read(&g_usb, kbd_report, &got) == 0 && got >= 8)
            decode_keyboard_boot_report(g_usb_kbd_keys, kbd_report, got);
        if (g_usb.has_keyboard)
            merge_key_bitmap(g_usb_kbd_keys);

        got = 0;
        if (g_usb.has_mouse && usb_hid_mouse_read(&g_usb, mouse_report, &got) == 0 && got >= 1)
            parse_usb_mouse_report(&g_usb.mouse, mouse_report, got);
    }

    g_mouse.buttons = (uint8_t)(g_tpd_buttons | g_usb_mouse_buttons);

    /* debug stuff
    if (g_keys_now[KINPUT_KEY_A] != 0u && g_keys_prev[KINPUT_KEY_A] == 0u)
        tpd_debug_dump_recent();

    if (g_keys_now[KINPUT_KEY_P] != 0u && g_keys_prev[KINPUT_KEY_P] == 0u)
        terminal_flush_log();
    */
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
