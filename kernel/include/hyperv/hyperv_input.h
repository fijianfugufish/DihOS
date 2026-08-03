#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int hyperv_input_init(void);
    void hyperv_input_poll(void);
    const uint8_t *hyperv_input_keyboard_bitmap(void);
    int hyperv_input_mouse_state(int32_t *x, int32_t *y,
                                 int32_t *dx, int32_t *dy,
                                 int32_t *wheel, uint8_t *buttons,
                                 uint8_t *absolute);
    int hyperv_input_keyboard_online(void);
    int hyperv_input_mouse_online(void);

#ifdef __cplusplus
}
#endif
