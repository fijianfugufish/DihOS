#pragma once

#include <stdint.h>
#include "apps/sacx_keys.h"

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
        SACX_IMG_FORMAT_PNG = 1,
        SACX_IMG_FORMAT_JPEG = 2,
        SACX_IMG_FORMAT_BMP = 3,
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

    typedef struct sacx3d_vec3
    {
        float x;
        float y;
        float z;
    } sacx3d_vec3;

    typedef struct sacx3d_viewport_desc
    {
        uint32_t parent_obj_handle;
        int32_t x;
        int32_t y;
        int32_t z;
        uint32_t w;
        uint32_t h;
        uint32_t internal_w;
        uint32_t internal_h;
    } sacx3d_viewport_desc;

    typedef struct sacx3d_camera
    {
        sacx3d_vec3 pos;
        float yaw_deg;
        float pitch_deg;
        float roll_deg;
        float fov_deg;
        float near_z;
        float far_z;
    } sacx3d_camera;

    typedef struct sacx3d_fog
    {
        uint8_t enabled;
        sacx_color color;
        float start;
        float end;
    } sacx3d_fog;

    typedef struct sacx3d_point_light
    {
        sacx3d_vec3 pos;
        sacx_color color;
        float radius;
        float intensity;
        uint8_t enabled;
    } sacx3d_point_light;

    typedef struct sacx3d_box
    {
        float min_x;
        float min_y;
        float min_z;
        float max_x;
        float max_y;
        float max_z;
    } sacx3d_box;

    typedef struct sacx3d_player_desc
    {
        sacx3d_camera camera;
        float walk_speed;
        float free_speed;
        float mouse_sensitivity;
        float radius;
        float eye_height;
        uint8_t free_mode;
        uint8_t drag_to_look;
    } sacx3d_player_desc;

    typedef struct sacx3d_room_desc
    {
        float center_x;
        float floor_y;
        float center_z;
        float w;
        float h;
        float d;
        float wall_thickness;
        sacx_color floor_color;
        sacx_color ceiling_color;
        sacx_color left_wall_color;
        sacx_color right_wall_color;
        sacx_color front_wall_color;
        sacx_color back_wall_color;
    } sacx3d_room_desc;

    enum
    {
        SACX3D_SURFACE_FRONT = 0u,
        SACX3D_SURFACE_BACK = 1u,
        SACX3D_SURFACE_RIGHT = 2u,
        SACX3D_SURFACE_LEFT = 3u,
        SACX3D_SURFACE_TOP = 4u,
        SACX3D_SURFACE_BOTTOM = 5u,
    };

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

        int (*k3d_scene_create)(const sacx3d_viewport_desc *desc, uint32_t *out_scene_handle, uint32_t *out_obj_handle);
        int (*k3d_scene_destroy)(uint32_t scene_handle);
        int (*k3d_scene_render)(uint32_t scene_handle);
        int (*k3d_scene_resize)(uint32_t scene_handle, uint32_t viewport_w, uint32_t viewport_h,
                                uint32_t internal_w, uint32_t internal_h);
        int (*k3d_scene_root_obj)(uint32_t scene_handle, uint32_t *out_obj_handle);
        int (*k3d_scene_set_camera)(uint32_t scene_handle, const sacx3d_camera *camera);
        int (*k3d_scene_get_camera)(uint32_t scene_handle, sacx3d_camera *out_camera);
        int (*k3d_scene_set_ambient)(uint32_t scene_handle, sacx_color color, float intensity);
        int (*k3d_scene_set_directional_light)(uint32_t scene_handle, sacx3d_vec3 dir, sacx_color color, float intensity);
        int (*k3d_scene_clear_point_lights)(uint32_t scene_handle);
        int (*k3d_scene_add_point_light)(uint32_t scene_handle, const sacx3d_point_light *light, uint32_t *out_light);
        int (*k3d_scene_set_point_light)(uint32_t scene_handle, uint32_t light_idx, const sacx3d_point_light *light);
        int (*k3d_scene_set_fog)(uint32_t scene_handle, const sacx3d_fog *fog);
        int (*k3d_scene_new_cube)(uint32_t scene_handle, float w, float h, float d,
                                  float x, float y, float z, sacx_color color, uint32_t *out_instance);
        int (*k3d_scene_load_obj)(uint32_t scene_handle, const char *path,
                                  float x, float y, float z, uint32_t *out_instance);
        int (*k3d_instance_set_pos)(uint32_t scene_handle, uint32_t instance_handle, float x, float y, float z);
        int (*k3d_instance_set_rotation)(uint32_t scene_handle, uint32_t instance_handle,
                                         float pitch_deg, float yaw_deg, float roll_deg);
        int (*k3d_instance_set_scale)(uint32_t scene_handle, uint32_t instance_handle, float sx, float sy, float sz);
        int (*k3d_instance_set_visible)(uint32_t scene_handle, uint32_t instance_handle, uint32_t visible);
        int (*k3d_player_create)(uint32_t scene_handle, uint32_t window_handle,
                                 const sacx3d_player_desc *desc, uint32_t *out_player_handle);
        int (*k3d_player_destroy)(uint32_t player_handle);
        int (*k3d_player_set_free_mode)(uint32_t player_handle, uint32_t free_mode);
        int (*k3d_player_set_camera)(uint32_t player_handle, const sacx3d_camera *camera);
        int (*k3d_player_get_camera)(uint32_t player_handle, sacx3d_camera *out_camera);
        int (*k3d_player_clear_colliders)(uint32_t player_handle);
        int (*k3d_player_add_collider)(uint32_t player_handle, const sacx3d_box *box);
        int (*k3d_player_update)(uint32_t player_handle, float dt, uint32_t render_scene);
        int (*k3d_scene_apply_default_world)(uint32_t scene_handle);
        int (*k3d_scene_add_room)(uint32_t scene_handle, uint32_t player_handle, const sacx3d_room_desc *desc);
        int (*k3d_scene_add_obstacle_cube)(uint32_t scene_handle, uint32_t player_handle,
                                           float w, float h, float d,
                                           float x, float y, float z,
                                           sacx_color color, uint32_t *out_instance);
        int (*k3d_scene_add_image_surface)(uint32_t scene_handle, uint32_t image_handle, uint32_t face,
                                           float x, float y, float z, float w, float h,
                                           uint32_t *out_instance);
        int (*k3d_scene_add_text_surface)(uint32_t scene_handle, const char *text, uint32_t face,
                                          float x, float y, float z, float w, float h,
                                          sacx_color text_color, uint8_t text_alpha,
                                          sacx_color bg_color, uint8_t bg_alpha,
                                          uint32_t scale, uint32_t *out_instance);
        int (*k3d_instance_set_casts_shadow)(uint32_t scene_handle, uint32_t instance_handle, uint32_t casts_shadow);

        /* ABI v1 append-only image editing extension. Check struct_size before use. */
        int (*img_create)(uint32_t w, uint32_t h, uint32_t argb, uint32_t *out_image_handle);
        int (*img_clone)(uint32_t image_handle, uint32_t *out_image_handle);
        int (*img_pixels)(uint32_t image_handle, uint32_t **out_argb, uint32_t *out_stride_px);
        int (*img_touch)(uint32_t image_handle);
        int (*img_save)(uint32_t image_handle, const char *path, uint32_t format, uint32_t quality);
        int (*img_draw_text)(uint32_t image_handle, int32_t x, int32_t y, const char *text,
                             sacx_color color, uint8_t alpha, uint32_t scale);
        int (*img_clipboard_set)(uint32_t image_handle);
        int (*img_clipboard_get)(uint32_t *out_image_handle);
        uint32_t (*app_arg_image)(void);
        int (*dialog_save_file)(const char *initial_dir, const char *suggested_name,
                                sacx_file_dialog_fn on_result, void *user);
        int (*window_set_close_deferred)(uint32_t window_handle, uint32_t deferred);
        int (*window_close_requested)(uint32_t window_handle);
        int (*window_close_accept)(uint32_t window_handle);
        int (*window_close_cancel)(uint32_t window_handle);
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
