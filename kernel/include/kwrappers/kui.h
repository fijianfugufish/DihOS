#pragma once

#include <stdint.h>
#include "kwrappers/kgfx.h"
#include "kwrappers/kbutton.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int idx;
    } kui_view_handle;

    typedef struct
    {
        int idx;
    } kui_dropdown_handle;

    typedef struct
    {
        int idx;
    } kui_radio_handle;

    typedef struct
    {
        int idx;
    } kui_toggle_handle;

    typedef void (*kui_on_change_fn)(uint32_t handle, int32_t value, void *user);

    enum
    {
        KUI_STATE_ALWAYS = 0xFFFFFFFFu,
    };

    enum
    {
        KUI_LAYOUT_NONE = 0,
        KUI_LAYOUT_ROW = 1,
        KUI_LAYOUT_COLUMN = 2,
        KUI_LAYOUT_GRID = 3,
    };

    enum
    {
        KUI_LAYOUT_VISIBLE_ONLY = 1u << 0,
        KUI_LAYOUT_SQUARE_CELLS = 1u << 1,
    };

    typedef struct kui_layout_desc
    {
        uint32_t kind;
        uint32_t padding_x;
        uint32_t padding_y;
        uint32_t gap_x;
        uint32_t gap_y;
        uint32_t columns;
        uint32_t flags;
    } kui_layout_desc;

    enum
    {
        KUI_TEXT_FIT_ELLIPSIZE = 1u << 0,
        KUI_TEXT_FIT_WRAP = 1u << 1,
        KUI_TEXT_FIT_ALLOW_FRACTIONAL = 1u << 2,
    };

    typedef struct kui_fit_desc
    {
        uint32_t max_w;
        uint32_t max_h;
        uint32_t padding_x;
        uint32_t padding_y;
        uint32_t min_scale;
        uint32_t max_scale;
        int32_t char_spacing;
        int32_t line_spacing;
        uint32_t max_lines;
        uint32_t flags;
    } kui_fit_desc;

    void kui_init(const struct kfont *font);
    void kui_set_font(const struct kfont *font);
    void kui_update_all(void);

    uint32_t kui_text_measure_line_px(const char *text, uint32_t scale, int32_t char_spacing);
    uint32_t kui_text_line_height(uint32_t scale, int32_t line_spacing);
    uint32_t kui_text_fit_scale(const char *text, const kui_fit_desc *desc);
    int kui_text_ellipsize(const char *text, uint32_t scale, int32_t char_spacing,
                           uint32_t max_w, char *out, uint32_t out_cap);
    int kui_text_wrap_words(const char *text, uint32_t scale, int32_t char_spacing,
                            uint32_t max_w, uint32_t max_lines, char *out, uint32_t out_cap);
    int kui_label_fit(kgfx_obj_handle text_obj, char *buffer, uint32_t buffer_cap,
                      const char *text, const kui_fit_desc *desc);
    int kui_button_fit_label(kbutton_handle button, kgfx_obj_handle label_obj,
                             char *buffer, uint32_t buffer_cap,
                             const char *text, const kui_fit_desc *desc);

    int kui_view_create_rect(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t h,
                             int32_t z, kcolor fill, uint32_t visible,
                             kui_view_handle *out_view, kgfx_obj_handle *out_root);
    int kui_view_destroy(kui_view_handle view);
    kgfx_obj_handle kui_view_root(kui_view_handle view);
    int kui_view_add_obj(kui_view_handle view, uint32_t state, kgfx_obj_handle obj);
    int kui_view_set_state(kui_view_handle view, uint32_t state);
    uint32_t kui_view_state(kui_view_handle view);
    int kui_view_set_visible(kui_view_handle view, uint32_t visible);
    int kui_view_set_layout(kui_view_handle view, const kui_layout_desc *desc);
    int kui_view_apply_layout(kui_view_handle view);
    int kui_view_set_bounds(kui_view_handle view, int32_t x, int32_t y, uint32_t w, uint32_t h);

    int kui_dropdown_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                            int32_t z, const char *const *items, uint32_t item_count,
                            uint32_t selected, kui_on_change_fn on_change, void *user,
                            kui_dropdown_handle *out_dropdown, kgfx_obj_handle *out_root);
    int kui_dropdown_destroy(kui_dropdown_handle dropdown);
    kgfx_obj_handle kui_dropdown_root(kui_dropdown_handle dropdown);
    int kui_dropdown_selected(kui_dropdown_handle dropdown);
    int kui_dropdown_set_selected(kui_dropdown_handle dropdown, uint32_t selected);
    int kui_dropdown_set_enabled(kui_dropdown_handle dropdown, uint32_t enabled);

    int kui_radio_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                         int32_t z, const char *const *items, uint32_t item_count,
                         uint32_t selected, kui_on_change_fn on_change, void *user,
                         kui_radio_handle *out_radio, kgfx_obj_handle *out_root);
    int kui_radio_destroy(kui_radio_handle radio);
    kgfx_obj_handle kui_radio_root(kui_radio_handle radio);
    int kui_radio_selected(kui_radio_handle radio);
    int kui_radio_set_selected(kui_radio_handle radio, uint32_t selected);
    int kui_radio_set_enabled(kui_radio_handle radio, uint32_t enabled);

    int kui_toggle_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t h,
                          int32_t z, const char *label, uint32_t checked,
                          kui_on_change_fn on_change, void *user,
                          kui_toggle_handle *out_toggle, kgfx_obj_handle *out_root);
    int kui_toggle_destroy(kui_toggle_handle toggle);
    kgfx_obj_handle kui_toggle_root(kui_toggle_handle toggle);
    int kui_toggle_checked(kui_toggle_handle toggle);
    int kui_toggle_set_checked(kui_toggle_handle toggle, uint32_t checked);
    int kui_toggle_set_enabled(kui_toggle_handle toggle, uint32_t enabled);

#ifdef __cplusplus
}
#endif
