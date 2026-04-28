#pragma once

#ifndef KWINDOW_H
#define KWINDOW_H

#include <stdint.h>
#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/ktext.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int idx;
    } kwindow_handle;

    typedef struct kwindow_style
    {
        kcolor body_fill;
        kcolor body_outline;
        kcolor titlebar_fill;
        kcolor title_color;
        kcolor close_text_color;
        kcolor fullscreen_text_color;
        kbutton_style close_button_style;
        kbutton_style fullscreen_button_style;
        uint16_t body_outline_width;
        uint32_t titlebar_height;
        uint32_t close_button_width;
        uint32_t close_button_height;
        uint32_t fullscreen_button_width;
        uint32_t fullscreen_button_height;
        uint32_t title_scale;
        uint32_t close_glyph_scale;
        uint32_t fullscreen_glyph_scale;
    } kwindow_style;

    static inline kwindow_style kwindow_style_default(void)
    {
        kwindow_style s;
        s.body_fill = dark_gray;
        s.body_outline = blue;
        s.titlebar_fill = dark_slate_gray;
        s.title_color = white;
        s.close_text_color = white;
        s.fullscreen_text_color = black;
        s.close_button_style = kbutton_style_default();
        s.fullscreen_button_style = kbutton_style_default();
        s.close_button_style.fill = red;
        s.close_button_style.hover_fill = tomato;
        s.close_button_style.pressed_fill = dark_red;
        s.close_button_style.outline = white;
        s.close_button_style.outline_width = 1;
        s.fullscreen_button_style.fill = yellow;
        s.fullscreen_button_style.hover_fill = gold;
        s.fullscreen_button_style.pressed_fill = goldenrod;
        s.fullscreen_button_style.outline = white;
        s.fullscreen_button_style.outline_width = 1;
        s.body_outline_width = 2;
        s.titlebar_height = 42;
        s.close_button_width = 34;
        s.close_button_height = 30;
        s.fullscreen_button_width = 34;
        s.fullscreen_button_height = 30;
        s.title_scale = 3;
        s.close_glyph_scale = 3;
        s.fullscreen_glyph_scale = 3;
        return s;
    }

    void kwindow_init(void);
    kwindow_handle kwindow_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                  int32_t z, const kfont *font, const char *title,
                                  const kwindow_style *style);
    int kwindow_destroy(kwindow_handle h);
    void kwindow_update_all(void);

    void kwindow_set_visible(kwindow_handle h, uint8_t visible);
    int kwindow_visible(kwindow_handle h);
    int kwindow_raise(kwindow_handle h);
    void kwindow_set_title(kwindow_handle h, const char *title);
    kgfx_obj_handle kwindow_root(kwindow_handle h);
    int kwindow_point_can_receive_input(kwindow_handle h, int32_t x, int32_t y);
    int kwindow_obj_can_receive_input(kgfx_obj_handle h, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif
