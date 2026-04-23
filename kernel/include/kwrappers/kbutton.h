#pragma once

#ifndef KBUTTON_H
#define KBUTTON_H

#include <stdint.h>
#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int idx;
    } kbutton_handle;

    typedef void (*kbutton_on_click_fn)(kbutton_handle button, void *user);

    typedef struct kbutton_style
    {
        kcolor fill;
        kcolor hover_fill;
        kcolor pressed_fill;
        kcolor outline;
        uint8_t alpha;
        uint8_t outline_alpha;
        uint16_t outline_width;
    } kbutton_style;

    static inline kbutton_style kbutton_style_default(void)
    {
        kbutton_style s;
        s.fill = dim_gray;
        s.hover_fill = slate_gray;
        s.pressed_fill = steel_blue;
        s.outline = white;
        s.alpha = 255;
        s.outline_alpha = 255;
        s.outline_width = 2;
        return s;
    }

    void kbutton_init(void);
    kbutton_handle kbutton_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                    int32_t z, const kbutton_style *style,
                                    kbutton_on_click_fn on_click, void *user);
    int kbutton_destroy(kbutton_handle h);
    void kbutton_update_all(void);

    kgfx_obj_handle kbutton_root(kbutton_handle h);
    void kbutton_set_callback(kbutton_handle h, kbutton_on_click_fn on_click, void *user);
    void kbutton_set_enabled(kbutton_handle h, uint8_t enabled);
    int kbutton_enabled(kbutton_handle h);
    int kbutton_hovered(kbutton_handle h);
    int kbutton_pressed(kbutton_handle h);

#ifdef __cplusplus
}
#endif

#endif
