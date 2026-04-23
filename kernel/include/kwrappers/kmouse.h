#pragma once

#ifndef KMOUSE_H
#define KMOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum kmouse_cursor
    {
        KMOUSE_CURSOR_ARROW = 0,
        KMOUSE_CURSOR_BEAM,
        KMOUSE_CURSOR_WAIT,
        KMOUSE_CURSOR_SIZE3,
        KMOUSE_CURSOR_SIZE1,
        KMOUSE_CURSOR_SIZE2,
        KMOUSE_CURSOR_SIZE4,
        KMOUSE_CURSOR_NO,
        KMOUSE_CURSOR_CROSS,
        KMOUSE_CURSOR_BUSY,
        KMOUSE_CURSOR_MOVE,
        KMOUSE_CURSOR_LINK,
        KMOUSE_CURSOR_COUNT
    } kmouse_cursor;

    typedef struct kmouse_state
    {
        int32_t x;
        int32_t y;
        int32_t dx;
        int32_t dy;
        int32_t wheel;
        uint8_t buttons;
        uint8_t visible;
    } kmouse_state;

    int kmouse_init(void);
    int kmouse_set_cursor(kmouse_cursor cursor);
    int kmouse_switch_cursor(kmouse_cursor cursor);
    kmouse_cursor kmouse_current_cursor(void);
    void kmouse_set_sensitivity_pct(uint32_t pct);
    uint32_t kmouse_sensitivity_pct(void);
    void kmouse_update(void);

    int32_t kmouse_x(void);
    int32_t kmouse_y(void);
    int32_t kmouse_dx(void);
    int32_t kmouse_dy(void);
    int32_t kmouse_wheel(void);
    uint8_t kmouse_buttons(void);
    uint8_t kmouse_visible(void);
    void kmouse_get_state(kmouse_state *out);

#ifdef __cplusplus
}
#endif

#endif
