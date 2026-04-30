#pragma once

#include <stdint.h>
#include "sacx_keys.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SACX_API_ABI_VERSION 1u

    enum
    {
        SACX_FILE_READ = 1u << 0,
        SACX_FILE_WRITE = 1u << 1,
        SACX_FILE_CREATE = 1u << 2,
        SACX_FILE_TRUNC = 1u << 3,
        SACX_FILE_APPEND = 1u << 4,
    };

    enum
    {
        SACX_TEXT_ALIGN_LEFT = 0,
        SACX_TEXT_ALIGN_CENTER = 1,
        SACX_TEXT_ALIGN_RIGHT = 2,
    };

    enum
    {
        SACX_GFX_IMAGE_SAMPLE_NEAREST = 0,
        SACX_GFX_IMAGE_SAMPLE_BILINEAR = 1,
    };

    enum
    {
        SACX_MOUSE_CURSOR_ARROW = 0,
        SACX_MOUSE_CURSOR_BEAM,
        SACX_MOUSE_CURSOR_WAIT,
        SACX_MOUSE_CURSOR_SIZE3,
        SACX_MOUSE_CURSOR_SIZE1,
        SACX_MOUSE_CURSOR_SIZE2,
        SACX_MOUSE_CURSOR_SIZE4,
        SACX_MOUSE_CURSOR_NO,
        SACX_MOUSE_CURSOR_CROSS,
        SACX_MOUSE_CURSOR_BUSY,
        SACX_MOUSE_CURSOR_MOVE,
        SACX_MOUSE_CURSOR_LINK,
        SACX_MOUSE_CURSOR_COUNT
    };

    typedef struct sacx_api sacx_api;
    typedef int (*sacx_update_fn)(const sacx_api *api);
    typedef int (*sacx_entry_fn)(const sacx_api *api);
    typedef void (*sacx_button_on_click_fn)(uint32_t button_handle, void *user);
    typedef void (*sacx_textbox_on_submit_fn)(uint32_t textbox_handle, const char *text, void *user);
    typedef void (*sacx_file_dialog_fn)(int accepted, const char *raw_path, const char *friendly_path, void *user);

    typedef struct sacx_color
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } sacx_color;

    typedef struct sacx_mouse_state
    {
        int32_t x;
        int32_t y;
        int32_t dx;
        int32_t dy;
        int32_t wheel;
        uint8_t buttons;
        uint8_t visible;
    } sacx_mouse_state;

    typedef struct sacx_dirent
    {
        char name[256];
        uint8_t is_dir;
        uint32_t size;
    } sacx_dirent;

    typedef struct sacx_button_style
    {
        sacx_color fill;
        sacx_color hover_fill;
        sacx_color pressed_fill;
        sacx_color outline;
        uint8_t alpha;
        uint8_t outline_alpha;
        uint16_t outline_width;
    } sacx_button_style;

    typedef struct sacx_textbox_style
    {
        sacx_color fill;
        sacx_color hover_fill;
        sacx_color focus_fill;
        sacx_color outline;
        sacx_color focus_outline;
        sacx_color text_color;
        uint8_t alpha;
        uint8_t outline_alpha;
        uint16_t outline_width;
        uint16_t padding_x;
        uint16_t padding_y;
        uint32_t text_scale;
    } sacx_textbox_style;

    typedef struct sacx_window_style
    {
        sacx_color body_fill;
        sacx_color body_outline;
        sacx_color titlebar_fill;
        sacx_color title_color;
        sacx_color close_text_color;
        sacx_color fullscreen_text_color;
        sacx_button_style close_button_style;
        sacx_button_style fullscreen_button_style;
        uint16_t body_outline_width;
        uint32_t titlebar_height;
        uint32_t close_button_width;
        uint32_t close_button_height;
        uint32_t fullscreen_button_width;
        uint32_t fullscreen_button_height;
        uint32_t title_scale;
        uint32_t close_glyph_scale;
        uint32_t fullscreen_glyph_scale;
    } sacx_window_style;

    static inline sacx_button_style sacx_button_style_default(void)
    {
        sacx_button_style s;
        s.fill = (sacx_color){105, 105, 105};
        s.hover_fill = (sacx_color){112, 128, 144};
        s.pressed_fill = (sacx_color){70, 130, 180};
        s.outline = (sacx_color){255, 255, 255};
        s.alpha = 255;
        s.outline_alpha = 255;
        s.outline_width = 2;
        return s;
    }

    static inline sacx_textbox_style sacx_textbox_style_default(void)
    {
        sacx_textbox_style s;
        s.fill = (sacx_color){0, 0, 0};
        s.hover_fill = (sacx_color){47, 79, 79};
        s.focus_fill = (sacx_color){169, 169, 169};
        s.outline = (sacx_color){112, 128, 144};
        s.focus_outline = (sacx_color){0, 255, 255};
        s.text_color = (sacx_color){255, 255, 255};
        s.alpha = 255;
        s.outline_alpha = 255;
        s.outline_width = 1;
        s.padding_x = 6;
        s.padding_y = 2;
        s.text_scale = 1;
        return s;
    }

    static inline sacx_window_style sacx_window_style_default(void)
    {
        sacx_window_style s;
        s.body_fill = (sacx_color){169, 169, 169};
        s.body_outline = (sacx_color){0, 0, 255};
        s.titlebar_fill = (sacx_color){47, 79, 79};
        s.title_color = (sacx_color){255, 255, 255};
        s.close_text_color = (sacx_color){255, 255, 255};
        s.fullscreen_text_color = (sacx_color){0, 0, 0};
        s.close_button_style = sacx_button_style_default();
        s.fullscreen_button_style = sacx_button_style_default();
        s.close_button_style.fill = (sacx_color){255, 0, 0};
        s.close_button_style.hover_fill = (sacx_color){255, 99, 71};
        s.close_button_style.pressed_fill = (sacx_color){139, 0, 0};
        s.close_button_style.outline = (sacx_color){255, 255, 255};
        s.close_button_style.outline_width = 1;
        s.fullscreen_button_style.fill = (sacx_color){255, 255, 0};
        s.fullscreen_button_style.hover_fill = (sacx_color){255, 215, 0};
        s.fullscreen_button_style.pressed_fill = (sacx_color){218, 165, 32};
        s.fullscreen_button_style.outline = (sacx_color){255, 255, 255};
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

    struct sacx_api
    {
        uint32_t abi_version;
        uint32_t struct_size;
        void *reserved0;

        int (*app_set_update)(sacx_update_fn fn);
        int (*app_exit)(int status, const char *text);
        int (*app_yield)(void);
        int (*app_sleep_ticks)(uint64_t ticks);

        uint64_t (*time_ticks)(void);
        uint64_t (*time_seconds)(void);
        void (*log)(const char *text);

        int (*file_open)(const char *path, uint32_t flags, uint32_t *out_handle);
        int (*file_read)(uint32_t handle, void *buf, uint32_t n, uint32_t *out_read);
        int (*file_write)(uint32_t handle, const void *buf, uint32_t n, uint32_t *out_written);
        int (*file_seek)(uint32_t handle, uint64_t offs);
        uint64_t (*file_size)(uint32_t handle);
        int (*file_close)(uint32_t handle);
        int (*file_unlink)(const char *path);
        int (*file_rename)(const char *src, const char *dst);
        int (*file_mkdir)(const char *path);

        int (*window_create)(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *title, uint32_t *out_handle);
        int (*window_destroy)(uint32_t handle);
        int (*window_set_visible)(uint32_t handle, uint32_t visible);
        int (*window_set_title)(uint32_t handle, const char *title);

        void (*gfx_fill_rgb)(uint8_t r, uint8_t g, uint8_t b);
        void (*gfx_rect_rgb)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b);
        void (*gfx_flush)(void);

        int (*input_key_down)(uint8_t usage);
        int (*input_key_pressed)(uint8_t usage);
        int (*input_key_released)(uint8_t usage);

        int (*dir_open)(const char *path, uint32_t *out_handle);
        int (*dir_next)(uint32_t handle, sacx_dirent *out_entry);
        int (*dir_close)(uint32_t handle);

        int (*window_create_ex)(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                const char *title, const sacx_window_style *style, uint32_t *out_handle);
        int (*window_visible)(uint32_t handle);
        int (*window_raise)(uint32_t handle);
        void (*window_set_work_area_bottom_inset)(uint32_t px);
        int (*window_root)(uint32_t window_handle, uint32_t *out_obj_handle);
        int (*window_point_can_receive_input)(uint32_t window_handle, int32_t x, int32_t y);

        int (*gfx_obj_add_rect)(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                sacx_color fill, uint32_t visible, uint32_t *out_obj_handle);
        int (*gfx_obj_add_circle)(int32_t cx, int32_t cy, uint32_t r, int32_t z,
                                  sacx_color fill, uint32_t visible, uint32_t *out_obj_handle);
        int (*gfx_obj_add_text)(const char *text, int32_t x, int32_t y, int32_t z,
                                sacx_color color, uint8_t alpha, uint32_t scale,
                                int32_t char_spacing, int32_t line_spacing, uint32_t align,
                                uint32_t visible, uint32_t *out_obj_handle);
        int (*gfx_obj_add_image_from_img)(uint32_t image_handle, int32_t x, int32_t y, uint32_t *out_obj_handle);
        int (*gfx_obj_destroy)(uint32_t obj_handle);
        int (*gfx_obj_set_visible)(uint32_t obj_handle, uint32_t visible);
        int (*gfx_obj_visible)(uint32_t obj_handle);
        int (*gfx_obj_set_z)(uint32_t obj_handle, int32_t z);
        int (*gfx_obj_z)(uint32_t obj_handle, int32_t *out_z);
        int (*gfx_obj_set_parent)(uint32_t child_obj_handle, uint32_t parent_obj_handle);
        int (*gfx_obj_clear_parent)(uint32_t child_obj_handle);
        int (*gfx_obj_set_clip_to_parent)(uint32_t obj_handle, uint32_t enabled);
        int (*gfx_obj_set_fill_rgb)(uint32_t obj_handle, uint8_t r, uint8_t g, uint8_t b);
        int (*gfx_obj_set_alpha)(uint32_t obj_handle, uint8_t alpha);
        int (*gfx_obj_set_outline_rgb)(uint32_t obj_handle, uint8_t r, uint8_t g, uint8_t b);
        int (*gfx_obj_set_outline_width)(uint32_t obj_handle, uint32_t width);
        int (*gfx_obj_set_outline_alpha)(uint32_t obj_handle, uint8_t alpha);
        int (*gfx_obj_set_rect)(uint32_t obj_handle, int32_t x, int32_t y, uint32_t w, uint32_t h);
        int (*gfx_obj_get_rect)(uint32_t obj_handle, int32_t *out_x, int32_t *out_y, uint32_t *out_w, uint32_t *out_h);
        int (*gfx_obj_set_rotation_deg)(uint32_t obj_handle, int32_t deg);
        int (*gfx_obj_rotation_deg)(uint32_t obj_handle, int32_t *out_deg);
        int (*gfx_obj_set_rotation_pivot)(uint32_t obj_handle, int32_t x, int32_t y);
        int (*gfx_obj_clear_rotation_pivot)(uint32_t obj_handle);
        int (*gfx_obj_set_circle)(uint32_t obj_handle, int32_t cx, int32_t cy, uint32_t r);
        int (*gfx_text_set)(uint32_t obj_handle, const char *text);
        int (*gfx_text_set_align)(uint32_t obj_handle, uint32_t align);
        int (*gfx_text_set_spacing)(uint32_t obj_handle, int32_t char_spacing, int32_t line_spacing);
        int (*gfx_text_set_scale)(uint32_t obj_handle, uint32_t scale);
        int (*gfx_text_set_pos)(uint32_t obj_handle, int32_t x, int32_t y);
        int (*gfx_image_set_size)(uint32_t obj_handle, uint32_t w, uint32_t h);
        int (*gfx_image_set_pos)(uint32_t obj_handle, int32_t x, int32_t y);
        int (*gfx_image_set_scale_pct)(uint32_t obj_handle, uint32_t scale_pct);
        int (*gfx_image_set_sample_mode)(uint32_t obj_handle, uint32_t sample_mode);

        int (*button_add_rect)(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                               const sacx_button_style *style, sacx_button_on_click_fn on_click,
                               void *user, uint32_t *out_button_handle);
        int (*button_destroy)(uint32_t button_handle);
        int (*button_root)(uint32_t button_handle, uint32_t *out_obj_handle);
        int (*button_set_callback)(uint32_t button_handle, sacx_button_on_click_fn on_click, void *user);
        int (*button_set_style)(uint32_t button_handle, const sacx_button_style *style);
        int (*button_set_enabled)(uint32_t button_handle, uint32_t enabled);
        int (*button_enabled)(uint32_t button_handle);
        int (*button_hovered)(uint32_t button_handle);
        int (*button_pressed)(uint32_t button_handle);

        int (*textbox_add_rect)(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                const sacx_textbox_style *style, sacx_textbox_on_submit_fn on_submit,
                                void *user, uint32_t *out_textbox_handle);
        int (*textbox_destroy)(uint32_t textbox_handle);
        int (*textbox_root)(uint32_t textbox_handle, uint32_t *out_obj_handle);
        int (*textbox_set_callback)(uint32_t textbox_handle, sacx_textbox_on_submit_fn on_submit, void *user);
        int (*textbox_set_enabled)(uint32_t textbox_handle, uint32_t enabled);
        int (*textbox_enabled)(uint32_t textbox_handle);
        int (*textbox_set_focus)(uint32_t textbox_handle, uint32_t focused);
        void (*textbox_clear_focus)(void);
        int (*textbox_focused)(uint32_t textbox_handle);
        int (*textbox_set_bounds)(uint32_t textbox_handle, int32_t x, int32_t y, uint32_t w, uint32_t h);
        int (*textbox_set_text)(uint32_t textbox_handle, const char *text);
        int (*textbox_clear)(uint32_t textbox_handle);
        int (*textbox_text_copy)(uint32_t textbox_handle, char *dst, uint32_t cap);

        int32_t (*input_mouse_dx)(void);
        int32_t (*input_mouse_dy)(void);
        int32_t (*input_mouse_wheel)(void);
        uint8_t (*input_mouse_buttons)(void);
        int (*input_mouse_consume)(sacx_mouse_state *out_state);

        int (*mouse_set_cursor)(uint32_t cursor);
        uint32_t (*mouse_current_cursor)(void);
        void (*mouse_set_sensitivity_pct)(uint32_t pct);
        uint32_t (*mouse_sensitivity_pct)(void);
        int32_t (*mouse_x)(void);
        int32_t (*mouse_y)(void);
        int32_t (*mouse_dx)(void);
        int32_t (*mouse_dy)(void);
        int32_t (*mouse_wheel)(void);
        uint8_t (*mouse_buttons)(void);
        uint8_t (*mouse_visible)(void);
        int (*mouse_get_state)(sacx_mouse_state *out_state);

        int (*text_draw)(int32_t x, int32_t y, const char *text,
                         sacx_color color, uint8_t alpha, uint32_t scale,
                         int32_t char_spacing, int32_t line_spacing);
        int (*text_draw_align)(int32_t anchor_x, int32_t y, const char *text,
                               sacx_color color, uint8_t alpha, uint32_t scale,
                               int32_t char_spacing, int32_t line_spacing, uint32_t align);
        int (*text_draw_outline_align)(int32_t anchor_x, int32_t y, const char *text,
                                       sacx_color fill, uint8_t fill_alpha, uint32_t scale,
                                       int32_t char_spacing, int32_t line_spacing, uint32_t align,
                                       uint32_t outline_width, sacx_color outline, uint8_t outline_alpha);
        uint32_t (*text_measure_line_px)(const char *text, uint32_t scale, int32_t char_spacing);
        uint32_t (*text_line_height)(uint32_t scale, int32_t line_spacing);
        uint32_t (*text_scale_mul_px)(uint32_t px, uint32_t scale);

        int (*img_load)(const char *path, uint32_t *out_image_handle);
        int (*img_load_bmp)(const char *path, uint32_t *out_image_handle);
        int (*img_load_png)(const char *path, uint32_t *out_image_handle);
        int (*img_load_jpg)(const char *path, uint32_t *out_image_handle);
        int (*img_draw)(uint32_t image_handle, int32_t x, int32_t y, uint8_t alpha);
        int (*img_destroy)(uint32_t image_handle);
        int (*img_size)(uint32_t image_handle, uint32_t *out_w, uint32_t *out_h);

        int (*sched_preempt_guard_enter)(void);
        int (*sched_preempt_guard_leave)(void);
        uint32_t (*sched_quantum_ticks)(void);
        uint32_t (*sched_preemptions)(void);
        int (*app_set_console_visible)(uint32_t visible);
        const char *(*app_arg_raw_path)(void);
        const char *(*app_arg_friendly_path)(void);
        int (*dialog_open_file)(const char *initial_dir, const char *suggested_name,
                                sacx_file_dialog_fn on_result, void *user);
        int (*dialog_active)(void);
        int (*window_focused)(uint32_t handle);
    };

    static inline int sacx_app_set_console_visible(const sacx_api *api, uint32_t visible)
    {
        if (!api || !api->app_set_console_visible)
            return -1;
        return api->app_set_console_visible(visible);
    }

    static inline const char *sacx_app_arg_raw_path(const sacx_api *api)
    {
        if (!api || !api->app_arg_raw_path)
            return "";
        return api->app_arg_raw_path();
    }

    static inline const char *sacx_app_arg_friendly_path(const sacx_api *api)
    {
        if (!api || !api->app_arg_friendly_path)
            return "";
        return api->app_arg_friendly_path();
    }

#define SACX_APP_NO_CONSOLE(api_ptr) sacx_app_set_console_visible((api_ptr), 0u)
#define SACX_APP_SHOW_CONSOLE(api_ptr) sacx_app_set_console_visible((api_ptr), 1u)

#ifdef __cplusplus
}
#endif
