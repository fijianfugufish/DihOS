#ifndef KINPUT_H
#define KINPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kinput_mouse_state
    {
        int32_t dx;
        int32_t dy;
        int32_t wheel;
        uint8_t buttons;
    } kinput_mouse_state;

    void kinput_init(uint64_t rsdp_phys);
    void kinput_poll(void);

    int kinput_key_down(uint8_t usage);
    int kinput_key_pressed(uint8_t usage);
    int kinput_key_released(uint8_t usage);

    int32_t kinput_mouse_dx(void);
    int32_t kinput_mouse_dy(void);
    int32_t kinput_mouse_wheel(void);
    uint8_t kinput_mouse_buttons(void);

    void kinput_mouse_consume(kinput_mouse_state *out);

#ifdef __cplusplus
}
#endif

#endif
