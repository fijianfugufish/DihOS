#include "system/kbusy.h"

#include <stdint.h>

#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"

static uint32_t G_busy_depth;
static kmouse_cursor G_saved_cursor = KMOUSE_CURSOR_ARROW;

void kbusy_begin(void)
{
    if (G_busy_depth++ == 0u)
    {
        G_saved_cursor = kmouse_current_cursor();
        (void)kmouse_set_cursor(KMOUSE_CURSOR_WAIT);
        kgfx_render_all(black);
    }
}

void kbusy_pump(void)
{
    if (!G_busy_depth)
        return;
    kinput_poll();
    kmouse_update();
    (void)kmouse_set_cursor(KMOUSE_CURSOR_WAIT);
    kgfx_render_all(black);
}

void kbusy_end(void)
{
    if (!G_busy_depth)
        return;
    if (--G_busy_depth == 0u)
        (void)kmouse_set_cursor(G_saved_cursor);
}

int kbusy_active(void)
{
    return G_busy_depth ? 1 : 0;
}
