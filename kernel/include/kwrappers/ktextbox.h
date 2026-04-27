#pragma once

#ifndef KTEXTBOX_H
#define KTEXTBOX_H

#include <stdint.h>
#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int idx;
    } ktextbox_handle;

    typedef void (*ktextbox_on_submit_fn)(ktextbox_handle textbox, const char *text, void *user);

    typedef struct ktextbox_style
    {
        kcolor fill;
        kcolor hover_fill;
        kcolor focus_fill;
        kcolor outline;
        kcolor focus_outline;
        kcolor text_color;
        uint8_t alpha;
        uint8_t outline_alpha;
        uint16_t outline_width;
        uint16_t padding_x;
        uint16_t padding_y;
        uint32_t text_scale;
    } ktextbox_style;

    static inline ktextbox_style ktextbox_style_default(void)
    {
        ktextbox_style s;
        s.fill = black;
        s.hover_fill = dark_slate_gray;
        s.focus_fill = dark_gray;
        s.outline = slate_gray;
        s.focus_outline = cyan;
        s.text_color = white;
        s.alpha = 255;
        s.outline_alpha = 255;
        s.outline_width = 1;
        s.padding_x = 6;
        s.padding_y = 2;
        s.text_scale = 1;
        return s;
    }

    void ktextbox_init(void);
    ktextbox_handle ktextbox_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                      int32_t z, const kfont *font,
                                      const ktextbox_style *style,
                                      ktextbox_on_submit_fn on_submit, void *user);
    int ktextbox_destroy(ktextbox_handle h);
    void ktextbox_update_all(void);

    kgfx_obj_handle ktextbox_root(ktextbox_handle h);
    void ktextbox_set_callback(ktextbox_handle h, ktextbox_on_submit_fn on_submit, void *user);
    void ktextbox_set_enabled(ktextbox_handle h, uint8_t enabled);
    int ktextbox_enabled(ktextbox_handle h);
    void ktextbox_set_focus(ktextbox_handle h, uint8_t focused);
    int ktextbox_focused(ktextbox_handle h);
    void ktextbox_set_bounds(ktextbox_handle h, int32_t x, int32_t y, uint32_t w, uint32_t h_px);
    void ktextbox_set_font(ktextbox_handle h, const kfont *font);
    void ktextbox_clear(ktextbox_handle h);
    const char *ktextbox_text(ktextbox_handle h);

#ifdef __cplusplus
}
#endif

#endif
