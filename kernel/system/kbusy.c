#include "system/kbusy.h"

#include <stdint.h>

#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/kwindow.h"

static uint32_t G_busy_depth;
static kmouse_cursor G_saved_cursor = KMOUSE_CURSOR_ARROW;
static uint8_t G_has_region;
static int32_t G_region_x;
static int32_t G_region_y;
static uint32_t G_region_w;
static uint32_t G_region_h;
static kwindow_handle G_window = {-1};

static int kbusy_mouse_in_region(void)
{
    int32_t x = kmouse_x();
    int32_t y = kmouse_y();
    if (G_window.idx >= 0)
        return kwindow_point_can_receive_input(G_window, x, y);
    return G_has_region && x >= G_region_x && y >= G_region_y &&
           x < G_region_x + (int32_t)G_region_w &&
           y < G_region_y + (int32_t)G_region_h;
}

static int kbusy_update_cursor(void)
{
    kmouse_cursor desired = kbusy_mouse_in_region() ? KMOUSE_CURSOR_WAIT : G_saved_cursor;
    if (kmouse_current_cursor() == desired)
        return 0;
    (void)kmouse_set_cursor(desired);
    return 1;
}

void kbusy_begin(void)
{
    if (G_busy_depth++ == 0u)
    {
        G_saved_cursor = kmouse_current_cursor();
        if (G_saved_cursor == KMOUSE_CURSOR_WAIT || G_saved_cursor == KMOUSE_CURSOR_BUSY)
            G_saved_cursor = KMOUSE_CURSOR_ARROW;
        G_has_region = 0u;
        G_window.idx = -1;
    }
}

void kbusy_begin_region(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (G_busy_depth++ == 0u)
    {
        G_saved_cursor = kmouse_current_cursor();
        if (G_saved_cursor == KMOUSE_CURSOR_WAIT || G_saved_cursor == KMOUSE_CURSOR_BUSY)
            G_saved_cursor = KMOUSE_CURSOR_ARROW;
        G_has_region = (w && h) ? 1u : 0u;
        G_region_x = x;
        G_region_y = y;
        G_region_w = w;
        G_region_h = h;
        G_window.idx = -1;
        if (kbusy_update_cursor())
            kgfx_render_all(black);
    }
}

void kbusy_begin_window(kwindow_handle window)
{
    if (G_busy_depth++ == 0u)
    {
        G_saved_cursor = kmouse_current_cursor();
        if (G_saved_cursor == KMOUSE_CURSOR_WAIT || G_saved_cursor == KMOUSE_CURSOR_BUSY)
            G_saved_cursor = KMOUSE_CURSOR_ARROW;
        G_has_region = 0u;
        G_window = window;
        if (kbusy_update_cursor())
            kgfx_render_all(black);
    }
}

void kbusy_pump(void)
{
    int cursor_changed;
    if (!G_busy_depth)
        return;
    kinput_poll();
    kmouse_update();
    cursor_changed = kbusy_update_cursor();
    if (cursor_changed || kmouse_dx() || kmouse_dy() || kmouse_wheel())
        kgfx_render_all(black);
}

void kbusy_end(void)
{
    if (!G_busy_depth)
        return;
    if (--G_busy_depth == 0u)
    {
        (void)kmouse_set_cursor(G_saved_cursor);
        G_has_region = 0u;
        G_window.idx = -1;
    }
}

int kbusy_active(void)
{
    return G_busy_depth ? 1 : 0;
}
