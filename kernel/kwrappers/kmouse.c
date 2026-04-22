#include "kwrappers/kmouse.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kinput.h"

typedef struct
{
    kmouse_state state;
    kimg cursor_img;
    kgfx_obj_handle cursor_handle;
    uint32_t sensitivity_pct;
    uint8_t initialized;
    uint8_t has_cursor;
} kmouse_ctx_t;

static kmouse_ctx_t G;

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int32_t kmouse_scale_delta(int32_t raw, uint32_t sensitivity_pct)
{
    uint32_t mag = 0;
    uint32_t factor_pct = sensitivity_pct;
    uint32_t scaled = 0;

    if (raw == 0)
        return 0;

    mag = (raw < 0) ? (uint32_t)(-raw) : (uint32_t)raw;

    if (mag >= 24u)
        factor_pct = (factor_pct * 220u) / 100u;
    else if (mag >= 12u)
        factor_pct = (factor_pct * 170u) / 100u;
    else if (mag >= 6u)
        factor_pct = (factor_pct * 130u) / 100u;

    scaled = (mag * factor_pct + 50u) / 100u;
    if (scaled == 0u)
        scaled = 1u;

    return (raw < 0) ? -(int32_t)scaled : (int32_t)scaled;
}

static void kmouse_sync_cursor(void)
{
    const kfb *fb = kgfx_info();
    kgfx_obj *cursor_obj = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;

    if (!fb)
        return;

    if (G.has_cursor)
    {
        if (fb->width > G.cursor_img.w)
            max_x = (int32_t)(fb->width - G.cursor_img.w);
        if (fb->height > G.cursor_img.h)
            max_y = (int32_t)(fb->height - G.cursor_img.h);
    }
    else
    {
        if (fb->width)
            max_x = (int32_t)fb->width - 1;
        if (fb->height)
            max_y = (int32_t)fb->height - 1;
    }

    G.state.x = clamp_i32(G.state.x, 0, max_x);
    G.state.y = clamp_i32(G.state.y, 0, max_y);

    cursor_obj = kgfx_obj_ref(G.cursor_handle);
    if (cursor_obj && cursor_obj->kind == KGFX_OBJ_IMAGE)
    {
        cursor_obj->u.image.x = G.state.x;
        cursor_obj->u.image.y = G.state.y;
    }
}

int kmouse_init(const char *cursor_path)
{
    const kfb *fb = kgfx_info();

    G.state = (kmouse_state){0};
    G.cursor_img = (kimg){0};
    G.cursor_handle.idx = -1;
    G.sensitivity_pct = 100u;
    G.initialized = 1;
    G.has_cursor = 0;

    if (fb)
    {
        G.state.x = (int32_t)fb->width / 2;
        G.state.y = (int32_t)fb->height / 2;
    }

    if (!cursor_path)
        return -1;

    if (kimg_load_bmp_flags(&G.cursor_img, cursor_path, KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) != 0)
        return -1;

    G.cursor_handle = kgfx_obj_add_image(G.cursor_img.px,
                                         G.cursor_img.w,
                                         G.cursor_img.h,
                                         G.state.x,
                                         G.state.y,
                                         G.cursor_img.w);

    {
        kgfx_obj *cursor_obj = kgfx_obj_ref(G.cursor_handle);
        if (!cursor_obj)
            return -1;

        cursor_obj->alpha = 255;
        cursor_obj->z = 1000;
    }

    G.has_cursor = 1;
    G.state.visible = 1;
    kmouse_sync_cursor();
    return 0;
}

void kmouse_set_sensitivity_pct(uint32_t pct)
{
    if (pct < 10u)
        pct = 10u;
    if (pct > 400u)
        pct = 400u;
    G.sensitivity_pct = pct;
}

uint32_t kmouse_sensitivity_pct(void)
{
    return G.sensitivity_pct;
}

void kmouse_update(void)
{
    kinput_mouse_state raw = {0};

    if (!G.initialized)
        return;

    kinput_mouse_consume(&raw);

    G.state.dx = kmouse_scale_delta(raw.dx, G.sensitivity_pct);
    G.state.dy = kmouse_scale_delta(raw.dy, G.sensitivity_pct);
    G.state.wheel = raw.wheel;
    G.state.buttons = raw.buttons;

    G.state.x += G.state.dx;
    G.state.y += G.state.dy;

    kmouse_sync_cursor();
}

int32_t kmouse_x(void) { return G.state.x; }
int32_t kmouse_y(void) { return G.state.y; }
int32_t kmouse_dx(void) { return G.state.dx; }
int32_t kmouse_dy(void) { return G.state.dy; }
int32_t kmouse_wheel(void) { return G.state.wheel; }
uint8_t kmouse_buttons(void) { return G.state.buttons; }
uint8_t kmouse_visible(void) { return G.state.visible; }

void kmouse_get_state(kmouse_state *out)
{
    if (out)
        *out = G.state;
}
