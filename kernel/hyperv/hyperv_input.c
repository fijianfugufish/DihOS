#include "hyperv/hyperv_input.h"
#include "hyperv/vmbus.h"
#include "terminal/terminal_api.h"

enum
{
    SYNTH_KBD_PROTOCOL_REQUEST = 1u,
    SYNTH_KBD_PROTOCOL_RESPONSE = 2u,
    SYNTH_KBD_EVENT = 3u,
    SYNTH_KBD_VERSION = 0x00010000u,
    SYNTH_KBD_PROTOCOL_ACCEPTED = 1u,
    SYNTH_KBD_IS_BREAK = 1u << 1,
    SYNTH_KBD_IS_E0 = 1u << 2,
    SYNTH_HID_PROTOCOL_REQUEST = 0u,
    SYNTH_HID_PROTOCOL_RESPONSE = 1u,
    SYNTH_HID_INITIAL_DEVICE_INFO = 2u,
    SYNTH_HID_INITIAL_DEVICE_INFO_ACK = 3u,
    SYNTH_HID_INPUT_REPORT = 4u,
    SYNTH_HID_VERSION = 0x00020000u,
    SYNTH_HID_PROTOCOL_ACCEPTED = 1u,
    PIPE_MESSAGE_DATA = 1u,
};

static uint8_t G_hv_keys[256];
static uint8_t G_hv_mod_keys[256];
static uint8_t G_hv_keyboard_online;
static uint8_t G_hv_mouse_online;
static int32_t G_hv_mouse_x;
static int32_t G_hv_mouse_y;
static int32_t G_hv_mouse_dx;
static int32_t G_hv_mouse_dy;
static int32_t G_hv_mouse_wheel;
static uint8_t G_hv_mouse_buttons;
static uint8_t G_hv_mouse_physical_buttons;
static uint8_t G_hv_mouse_short_press_buttons;
static uint8_t G_hv_mouse_pending_press_buttons;
static uint8_t G_hv_mouse_synth_release_buttons;
static uint8_t G_hv_mouse_absolute;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static const uint8_t *mouse_pipe_data(const uint8_t *payload, uint32_t bytes, uint32_t *data_bytes)
{
    uint32_t size;

    if (data_bytes)
        *data_bytes = 0u;
    if (!payload || bytes < 8u || rd32(payload + 0) != PIPE_MESSAGE_DATA)
        return 0;

    size = rd32(payload + 4);
    if (size > bytes - 8u)
        return 0;
    if (data_bytes)
        *data_bytes = size;
    return payload + 8u;
}

static uint32_t build_mouse_pipe_protocol_request(uint8_t *out, uint32_t cap)
{
    if (!out || cap < 20u)
        return 0u;

    wr32(out + 0, PIPE_MESSAGE_DATA);
    wr32(out + 4, 12u);
    wr32(out + 8, SYNTH_HID_PROTOCOL_REQUEST);
    wr32(out + 12, 4u);
    wr32(out + 16, SYNTH_HID_VERSION);
    return 20u;
}

static uint32_t build_mouse_pipe_device_info_ack(uint8_t *out, uint32_t cap)
{
    if (!out || cap < 17u)
        return 0u;

    wr32(out + 0, PIPE_MESSAGE_DATA);
    wr32(out + 4, 9u);
    wr32(out + 8, SYNTH_HID_INITIAL_DEVICE_INFO_ACK);
    wr32(out + 12, 1u);
    out[16] = 0u;
    return 17u;
}

static void clear_keys(void)
{
    for (uint32_t i = 0; i < 256u; ++i)
    {
        G_hv_keys[i] = 0u;
        G_hv_mod_keys[i] = 0u;
    }
}

static void clear_frame_keys(void)
{
    for (uint32_t i = 0; i < 256u; ++i)
        G_hv_keys[i] = 0u;
}

static void merge_held_modifiers(void)
{
    for (uint32_t i = 0xE0u; i <= 0xE7u; ++i)
    {
        if (G_hv_mod_keys[i])
            G_hv_keys[i] = 1u;
    }
}

static uint8_t is_modifier_usage(uint8_t usage)
{
    return usage >= 0xE0u && usage <= 0xE7u;
}

static uint8_t set1_plain_to_hid(uint16_t sc)
{
    static const uint8_t map[128] = {
        [0x01] = 0x29, [0x02] = 0x1E, [0x03] = 0x1F, [0x04] = 0x20,
        [0x05] = 0x21, [0x06] = 0x22, [0x07] = 0x23, [0x08] = 0x24,
        [0x09] = 0x25, [0x0A] = 0x26, [0x0B] = 0x27, [0x0C] = 0x2D,
        [0x0D] = 0x2E, [0x0E] = 0x2A, [0x0F] = 0x2B, [0x10] = 0x14,
        [0x11] = 0x1A, [0x12] = 0x08, [0x13] = 0x15, [0x14] = 0x17,
        [0x15] = 0x1C, [0x16] = 0x18, [0x17] = 0x0C, [0x18] = 0x12,
        [0x19] = 0x13, [0x1A] = 0x2F, [0x1B] = 0x30, [0x1C] = 0x28,
        [0x1D] = 0xE0, [0x1E] = 0x04, [0x1F] = 0x16, [0x20] = 0x07,
        [0x21] = 0x09, [0x22] = 0x0A, [0x23] = 0x0B, [0x24] = 0x0D,
        [0x25] = 0x0E, [0x26] = 0x0F, [0x27] = 0x33, [0x28] = 0x34,
        [0x29] = 0x35, [0x2A] = 0xE1, [0x2B] = 0x31, [0x2C] = 0x1D,
        [0x2D] = 0x1B, [0x2E] = 0x06, [0x2F] = 0x19, [0x30] = 0x05,
        [0x31] = 0x11, [0x32] = 0x10, [0x33] = 0x36, [0x34] = 0x37,
        [0x35] = 0x38, [0x36] = 0xE5, [0x37] = 0x55, [0x38] = 0xE2,
        [0x39] = 0x2C, [0x3A] = 0x39, [0x3B] = 0x3A, [0x3C] = 0x3B,
        [0x3D] = 0x3C, [0x3E] = 0x3D, [0x3F] = 0x3E, [0x40] = 0x3F,
        [0x41] = 0x40, [0x42] = 0x41, [0x43] = 0x42, [0x44] = 0x43,
        [0x45] = 0x53, [0x46] = 0x47, [0x47] = 0x5F, [0x48] = 0x60,
        [0x49] = 0x61, [0x4A] = 0x56, [0x4B] = 0x5C, [0x4C] = 0x5D,
        [0x4D] = 0x5E, [0x4E] = 0x57, [0x4F] = 0x59, [0x50] = 0x5A,
        [0x51] = 0x5B, [0x52] = 0x62, [0x53] = 0x63, [0x57] = 0x44,
        [0x58] = 0x45};

    if (sc < 128u)
        return map[sc];
    return 0u;
}

static uint8_t set1_e0_to_hid(uint16_t sc)
{
    switch (sc)
    {
    case 0x1C: return 0x58;
    case 0x1D: return 0xE4;
    case 0x35: return 0x54;
    case 0x38: return 0xE6;
    case 0x47: return 0x4A;
    case 0x48: return 0x52;
    case 0x49: return 0x4B;
    case 0x4B: return 0x50;
    case 0x4D: return 0x4F;
    case 0x4F: return 0x4D;
    case 0x50: return 0x51;
    case 0x51: return 0x4E;
    case 0x52: return 0x49;
    case 0x53: return 0x4C;
    case 0x5B: return 0xE3;
    case 0x5C: return 0xE7;
    case 0x5D: return 0x65;
    default: return 0u;
    }
}

static void update_mouse_buttons(uint8_t buttons)
{
    uint8_t pressed = (uint8_t)(buttons & (uint8_t)~G_hv_mouse_physical_buttons);

    if (pressed)
        G_hv_mouse_short_press_buttons |= pressed;
    G_hv_mouse_physical_buttons = buttons;
}

static void handle_keyboard_event(const uint8_t *payload, uint32_t bytes)
{
    uint16_t sc;
    uint32_t info;
    uint8_t usage;

    if (!payload || bytes < 12u || rd32(payload) != SYNTH_KBD_EVENT)
        return;

    sc = rd16(payload + 4) & 0x7Fu;
    info = rd32(payload + 8);
    usage = (info & SYNTH_KBD_IS_E0) ? set1_e0_to_hid(sc) : set1_plain_to_hid(sc);
    if (!usage)
        return;

    if (is_modifier_usage(usage))
    {
        G_hv_mod_keys[usage] = (info & SYNTH_KBD_IS_BREAK) ? 0u : 1u;
        G_hv_keys[usage] = G_hv_mod_keys[usage];
        return;
    }

    if ((info & SYNTH_KBD_IS_BREAK) == 0u)
        G_hv_keys[usage] = 1u;
}

static void handle_mouse_report_payload(const uint8_t *report, uint32_t bytes)
{
    uint32_t base = 0u;

    if (!report || bytes < 3u)
        return;

    if (bytes >= 6u && report[0] <= 8u && (report[1] & 0xF8u) == 0u)
        base = 1u;

    if (bytes >= base + 5u)
    {
        uint8_t buttons = (uint8_t)(report[base] & 0x07u);
        uint16_t x = rd16(report + base + 1u);
        uint16_t y = rd16(report + base + 3u);

        update_mouse_buttons(buttons);
        G_hv_mouse_x = (int32_t)((x > 0x7FFFu) ? (x >> 1u) : x);
        G_hv_mouse_y = (int32_t)((y > 0x7FFFu) ? (y >> 1u) : y);
        G_hv_mouse_absolute = 1u;
        if (bytes >= base + 6u)
            G_hv_mouse_wheel += (int8_t)report[base + 5u];
        return;
    }

    if (bytes >= base + 3u)
    {
        update_mouse_buttons((uint8_t)(report[base] & 0x07u));
        G_hv_mouse_dx += (int8_t)report[base + 1u];
        G_hv_mouse_dy += (int8_t)report[base + 2u];
        G_hv_mouse_absolute = 0u;
        if (bytes >= base + 4u)
            G_hv_mouse_wheel += (int8_t)report[base + 3u];
    }
}

static void handle_mouse_message(const uint8_t *payload, uint32_t bytes)
{
    const uint8_t *pipe_data;
    uint32_t pipe_bytes = 0u;
    uint32_t type;
    uint32_t size;

    if (!payload || bytes == 0u)
        return;

    pipe_data = mouse_pipe_data(payload, bytes, &pipe_bytes);
    if (pipe_data)
    {
        payload = pipe_data;
        bytes = pipe_bytes;
    }

    if (bytes >= 8u)
    {
        type = rd32(payload + 0);
        size = rd32(payload + 4);
        if (type == SYNTH_HID_INPUT_REPORT && size <= bytes - 8u)
        {
            handle_mouse_report_payload(payload + 8u, size);
            return;
        }
    }

    handle_mouse_report_payload(payload, bytes);
}

static int wait_keyboard_protocol_response(void)
{
    uint8_t payload[256];
    uint32_t bytes = 0;

    for (uint32_t round = 0; round < 10000000u; ++round)
    {
        int rc = vmbus_keyboard_receive(payload, sizeof(payload), &bytes);
        if (rc == 1)
            continue;
        if (rc != 0)
            return -1;
        if (bytes >= 8u && rd32(payload) == SYNTH_KBD_PROTOCOL_RESPONSE)
            return (rd32(payload + 4) & SYNTH_KBD_PROTOCOL_ACCEPTED) ? 0 : -1;
    }
    return -1;
}

static int wait_mouse_protocol(void)
{
    uint8_t payload[256];
    uint8_t ack[24];
    const uint8_t *data;
    uint32_t bytes = 0;
    uint32_t data_bytes = 0u;
    uint32_t ack_bytes = 0u;
    uint8_t accepted = 0u;
    uint8_t saw_device = 0u;

    for (uint32_t round = 0; round < 10000000u; ++round)
    {
        int rc = vmbus_mouse_receive(payload, sizeof(payload), &bytes);
        if (rc == 1)
            continue;
        if (rc != 0)
            return -1;

        data = mouse_pipe_data(payload, bytes, &data_bytes);
        if (!data)
            continue;

        if (data_bytes >= 13u && rd32(data) == SYNTH_HID_PROTOCOL_RESPONSE)
        {
            accepted = data[12] & SYNTH_HID_PROTOCOL_ACCEPTED ? 1u : 0u;
            if (!accepted)
                return -1;
        }
        else if (data_bytes >= 8u && rd32(data) == SYNTH_HID_INITIAL_DEVICE_INFO)
        {
            ack_bytes = build_mouse_pipe_device_info_ack(ack, sizeof(ack));
            if (ack_bytes)
                (void)vmbus_mouse_send_inband(ack, ack_bytes);
            saw_device = 1u;
        }

        if (accepted && saw_device)
            return 0;
    }
    return -1;
}

int hyperv_input_init(void)
{
    uint8_t request[8] = {0};

    clear_keys();
    G_hv_keyboard_online = 0u;
    G_hv_mouse_online = 0u;
    G_hv_mouse_x = 0;
    G_hv_mouse_y = 0;
    G_hv_mouse_dx = 0;
    G_hv_mouse_dy = 0;
    G_hv_mouse_wheel = 0;
    G_hv_mouse_buttons = 0u;
    G_hv_mouse_physical_buttons = 0u;
    G_hv_mouse_short_press_buttons = 0u;
    G_hv_mouse_pending_press_buttons = 0u;
    G_hv_mouse_synth_release_buttons = 0u;
    G_hv_mouse_absolute = 0u;

    if (vmbus_open_keyboard_channel() != 0)
    {
        terminal_warn("hypervinput: keyboard channel unavailable");
    }
    else
    {
        wr32(request + 0, SYNTH_KBD_PROTOCOL_REQUEST);
        wr32(request + 4, SYNTH_KBD_VERSION);
        terminal_print("hypervinput: keyboard protocol request");
        if (vmbus_keyboard_send_inband(request, sizeof(request)) == 0 &&
            wait_keyboard_protocol_response() == 0)
        {
            G_hv_keyboard_online = 1u;
            terminal_success("hypervinput: keyboard online");
        }
        else
        {
            terminal_error("hypervinput: keyboard protocol rejected/timeout");
        }
    }

    if (vmbus_open_mouse_channel() != 0)
    {
        terminal_warn("hypervinput: pointer channel unavailable");
    }
    else
    {
        uint8_t mouse_request[24] = {0};
        uint32_t mouse_request_bytes =
            build_mouse_pipe_protocol_request(mouse_request, sizeof(mouse_request));

        terminal_print("hypervinput: pointer protocol request");
        if (mouse_request_bytes &&
            vmbus_mouse_send_inband(mouse_request, mouse_request_bytes) == 0 &&
            wait_mouse_protocol() == 0)
        {
            G_hv_mouse_online = 1u;
            terminal_success("hypervinput: pointer online");
        }
        else
        {
            terminal_error("hypervinput: pointer protocol rejected/timeout");
        }
    }

    return (G_hv_keyboard_online || G_hv_mouse_online) ? 0 : -1;
}

void hyperv_input_poll(void)
{
    uint8_t payload[256];
    uint32_t bytes = 0;
    uint8_t short_presses = 0u;

    clear_frame_keys();
    merge_held_modifiers();

    if (G_hv_keyboard_online)
    {
        for (uint32_t i = 0; i < 16u; ++i)
        {
            int rc = vmbus_keyboard_receive(payload, sizeof(payload), &bytes);
            if (rc != 0)
                break;
            handle_keyboard_event(payload, bytes);
        }
    }

    if (!G_hv_mouse_online)
        return;

    G_hv_mouse_dx = 0;
    G_hv_mouse_dy = 0;
    G_hv_mouse_wheel = 0;
    G_hv_mouse_absolute = 0u;
    G_hv_mouse_short_press_buttons = 0u;
    for (uint32_t i = 0; i < 16u; ++i)
    {
        int rc = vmbus_mouse_receive(payload, sizeof(payload), &bytes);
        if (rc != 0)
            break;
        handle_mouse_message(payload, bytes);
    }

    short_presses =
        (uint8_t)(G_hv_mouse_short_press_buttons & (uint8_t)~G_hv_mouse_physical_buttons);

    if (G_hv_mouse_synth_release_buttons)
    {
        G_hv_mouse_buttons = G_hv_mouse_physical_buttons;
        G_hv_mouse_synth_release_buttons = 0u;
        G_hv_mouse_pending_press_buttons |= short_presses;
    }
    else
    {
        short_presses |= G_hv_mouse_pending_press_buttons;
        G_hv_mouse_pending_press_buttons = 0u;
        if (short_presses)
        {
            G_hv_mouse_buttons = (uint8_t)(G_hv_mouse_physical_buttons | short_presses);
            G_hv_mouse_synth_release_buttons = short_presses;
        }
        else
        {
            G_hv_mouse_buttons = G_hv_mouse_physical_buttons;
        }
    }
}

const uint8_t *hyperv_input_keyboard_bitmap(void)
{
    return G_hv_keys;
}

int hyperv_input_mouse_state(int32_t *x, int32_t *y,
                             int32_t *dx, int32_t *dy,
                             int32_t *wheel, uint8_t *buttons,
                             uint8_t *absolute)
{
    if (!G_hv_mouse_online)
        return -1;
    if (x) *x = G_hv_mouse_x;
    if (y) *y = G_hv_mouse_y;
    if (dx) *dx = G_hv_mouse_dx;
    if (dy) *dy = G_hv_mouse_dy;
    if (wheel) *wheel = G_hv_mouse_wheel;
    if (buttons) *buttons = G_hv_mouse_buttons;
    if (absolute) *absolute = G_hv_mouse_absolute;
    return 0;
}

int hyperv_input_keyboard_online(void)
{
    return G_hv_keyboard_online != 0u;
}

int hyperv_input_mouse_online(void)
{
    return G_hv_mouse_online != 0u;
}
