#pragma once

#ifndef KMOUSE_H
#define KMOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

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

    int kmouse_init(const char *cursor_path);
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
