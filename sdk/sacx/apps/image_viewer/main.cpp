#include "sacx_api.h"

static const sacx_api *g_api = 0;

static uint32_t g_window = 0;
static uint32_t g_root = 0;
static uint32_t g_viewport = 0;
static uint32_t g_status_text = 0;
static uint32_t g_hint_text = 0;

static uint32_t g_open_button = 0;
static uint32_t g_open_button_root = 0;
static uint32_t g_open_label = 0;
static uint32_t g_zoom_out_button = 0;
static uint32_t g_zoom_out_button_root = 0;
static uint32_t g_zoom_out_label = 0;
static uint32_t g_zoom_in_button = 0;
static uint32_t g_zoom_in_button_root = 0;
static uint32_t g_zoom_in_label = 0;
static uint32_t g_fit_button = 0;
static uint32_t g_fit_button_root = 0;
static uint32_t g_fit_label = 0;

static uint32_t g_image = 0;
static uint32_t g_image_obj = 0;
static uint32_t g_image_w = 0;
static uint32_t g_image_h = 0;

static uint32_t g_view_w = 1;
static uint32_t g_view_h = 1;
static int32_t g_view_screen_x = 0;
static int32_t g_view_screen_y = 0;
static int32_t g_image_x = 0;
static int32_t g_image_y = 0;
static uint32_t g_scaled_w = 1;
static uint32_t g_scaled_h = 1;
static uint32_t g_zoom_pct = 100;
static uint8_t g_fit_mode = 1;
static uint8_t g_dragging = 0;
static int32_t g_last_mouse_x = 0;
static int32_t g_last_mouse_y = 0;
static uint8_t g_dialog_started = 0;
static uint8_t g_pending_load = 0;

static char g_pending_raw[256];
static char g_pending_friendly[256];
static char g_status[192];
static char g_title[160];
static char g_zoom_text[32];

static void image_dialog_result(int accepted, const char *raw_path, const char *friendly_path, void *user);

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    sacx_color c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    while (src[i] && i + 1u < cap)
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0;
    if (!dst || cap == 0)
        return;
    while (len < cap && dst[len])
        ++len;
    if (len >= cap)
        return;
    copy_text(dst + len, cap - len, src);
}

static void append_uint(char *dst, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0)
    {
        append_text(dst, cap, "0");
        return;
    }
    while (value && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n)
    {
        char one[2];
        one[0] = tmp[--n];
        one[1] = 0;
        append_text(dst, cap, one);
    }
}

static void set_status(const char *text)
{
    copy_text(g_status, sizeof(g_status), text);
    if (g_api && g_status_text)
        (void)g_api->gfx_text_set(g_status_text, g_status);
}

static void set_title_for_path(const char *path)
{
    copy_text(g_title, sizeof(g_title), "Image Viewer");
    if (path && path[0])
    {
        append_text(g_title, sizeof(g_title), " - ");
        append_text(g_title, sizeof(g_title), path);
    }
    if (g_api && g_window)
        (void)g_api->window_set_title(g_window, g_title);
}

static uint32_t clamp_zoom(uint32_t zoom)
{
    if (zoom < 10u)
        return 10u;
    if (zoom > 800u)
        return 800u;
    return zoom;
}

static uint32_t fit_zoom(void)
{
    uint32_t by_w = 100u;
    uint32_t by_h = 100u;
    uint32_t fit = 100u;

    if (!g_image_w || !g_image_h)
        return 100u;

    by_w = (g_view_w * 100u) / g_image_w;
    by_h = (g_view_h * 100u) / g_image_h;
    fit = by_w < by_h ? by_w : by_h;
    if (fit == 0u)
        fit = 1u;
    return clamp_zoom(fit);
}

static void update_zoom_status(void)
{
    copy_text(g_zoom_text, sizeof(g_zoom_text), "zoom ");
    append_uint(g_zoom_text, sizeof(g_zoom_text), g_zoom_pct);
    append_text(g_zoom_text, sizeof(g_zoom_text), "%");
    set_status(g_zoom_text);
}

static uint32_t text_line_h(void)
{
    if (g_api && g_api->text_line_height)
        return g_api->text_line_height(1u, 0);
    return 8u;
}

static int32_t centered_text_y(uint32_t h)
{
    uint32_t line_h = text_line_h();
    if (h > line_h)
        return (int32_t)((h - line_h) / 2u);
    return 0;
}

static void compute_scaled_size(void)
{
    if (!g_image_w || !g_image_h)
    {
        g_scaled_w = 1u;
        g_scaled_h = 1u;
        return;
    }

    if (g_fit_mode)
        g_zoom_pct = fit_zoom();

    g_scaled_w = (uint32_t)(((uint64_t)g_image_w * (uint64_t)g_zoom_pct) / 100u);
    g_scaled_h = (uint32_t)(((uint64_t)g_image_h * (uint64_t)g_zoom_pct) / 100u);
    if (g_scaled_w == 0u)
        g_scaled_w = 1u;
    if (g_scaled_h == 0u)
        g_scaled_h = 1u;
}

static void clamp_pan(void)
{
    if (g_scaled_w <= g_view_w)
    {
        g_image_x = (int32_t)((g_view_w - g_scaled_w) / 2u);
    }
    else
    {
        int32_t min_x = (int32_t)g_view_w - (int32_t)g_scaled_w;
        if (g_image_x > 0)
            g_image_x = 0;
        if (g_image_x < min_x)
            g_image_x = min_x;
    }

    if (g_scaled_h <= g_view_h)
    {
        g_image_y = (int32_t)((g_view_h - g_scaled_h) / 2u);
    }
    else
    {
        int32_t min_y = (int32_t)g_view_h - (int32_t)g_scaled_h;
        if (g_image_y > 0)
            g_image_y = 0;
        if (g_image_y < min_y)
            g_image_y = min_y;
    }
}

static void sync_image_object(void)
{
    if (!g_api || !g_image_obj)
        return;
    (void)g_api->gfx_image_set_size(g_image_obj, g_scaled_w, g_scaled_h);
    (void)g_api->gfx_image_set_pos(g_image_obj, g_image_x, g_image_y);
}

static void apply_image_layout(void)
{
    if (!g_api || !g_image_obj || !g_image_w || !g_image_h)
        return;

    compute_scaled_size();
    if (g_fit_mode)
    {
        g_image_x = (g_scaled_w < g_view_w) ? (int32_t)((g_view_w - g_scaled_w) / 2u) : 0;
        g_image_y = (g_scaled_h < g_view_h) ? (int32_t)((g_view_h - g_scaled_h) / 2u) : 0;
    }
    clamp_pan();
    sync_image_object();
}

static void zoom_about(uint32_t local_x, uint32_t local_y, uint32_t new_zoom)
{
    int64_t rel_x = 0;
    int64_t rel_y = 0;
    uint32_t old_w = g_scaled_w;
    uint32_t old_h = g_scaled_h;

    if (!g_image_obj || !old_w || !old_h)
        return;

    rel_x = (int64_t)(int32_t)local_x - (int64_t)g_image_x;
    rel_y = (int64_t)(int32_t)local_y - (int64_t)g_image_y;
    g_fit_mode = 0u;
    g_zoom_pct = clamp_zoom(new_zoom);
    compute_scaled_size();

    g_image_x = (int32_t)((int64_t)(int32_t)local_x - (rel_x * (int64_t)g_scaled_w) / (int64_t)old_w);
    g_image_y = (int32_t)((int64_t)(int32_t)local_y - (rel_y * (int64_t)g_scaled_h) / (int64_t)old_h);
    clamp_pan();
    sync_image_object();
}

static void layout_button(uint32_t root, uint32_t label, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (!g_api || !root)
        return;
    (void)g_api->gfx_obj_set_rect(root, x, y, w, h);
    if (label)
    {
        (void)g_api->gfx_text_set_pos(label, (int32_t)(w / 2u), centered_text_y(h));
    }
}

static void layout(void)
{
    int32_t rx = 0;
    int32_t ry = 0;
    uint32_t rw = 0;
    uint32_t rh = 0;
    uint32_t toolbar_y = 46u;
    uint32_t button_h = 30u;
    uint32_t gap = 8u;
    uint32_t status_h = 24u;
    uint32_t content_y = toolbar_y + button_h + 10u;
    uint32_t content_h = 1u;

    if (!g_api || !g_root)
        return;
    if (g_api->gfx_obj_get_rect(g_root, &rx, &ry, &rw, &rh) != 0)
        return;
    if (rw < 180u)
        rw = 180u;
    if (rh < 140u)
        rh = 140u;

    layout_button(g_open_button_root, g_open_label, 14, (int32_t)toolbar_y, 86, button_h);
    layout_button(g_zoom_out_button_root, g_zoom_out_label, 14 + 86 + (int32_t)gap, (int32_t)toolbar_y, 42, button_h);
    layout_button(g_zoom_in_button_root, g_zoom_in_label, 14 + 86 + (int32_t)gap + 42 + (int32_t)gap, (int32_t)toolbar_y, 42, button_h);
    layout_button(g_fit_button_root, g_fit_label, 14 + 86 + (int32_t)gap + 42 + (int32_t)gap + 42 + (int32_t)gap, (int32_t)toolbar_y, 60, button_h);

    if (content_y + status_h + 16u < rh)
        content_h = rh - content_y - status_h - 16u;

    g_view_w = rw > 28u ? rw - 28u : 1u;
    g_view_h = content_h;
    if (g_view_w == 0u)
        g_view_w = 1u;
    if (g_view_h == 0u)
        g_view_h = 1u;

    if (g_viewport)
        (void)g_api->gfx_obj_set_rect(g_viewport, 14, (int32_t)content_y, g_view_w, g_view_h);
    g_view_screen_x = rx + 14;
    g_view_screen_y = ry + (int32_t)content_y;
    if (g_status_text)
    {
        uint32_t status_y = content_y + content_h + 10u;
        uint32_t line_h = text_line_h();
        if (status_y + line_h + 6u > rh)
            status_y = (rh > line_h + 6u) ? rh - line_h - 6u : 0u;
        (void)g_api->gfx_text_set_pos(g_status_text, 16, (int32_t)status_y);
    }
    if (g_hint_text)
        (void)g_api->gfx_text_set_pos(g_hint_text, (int32_t)(rw > 420u ? rw - 400u : 16u),
                                      (int32_t)toolbar_y + centered_text_y(button_h));

    apply_image_layout();
}

static void open_dialog(void)
{
    if (!g_api || !g_api->dialog_open_file || !g_api->dialog_active || g_api->dialog_active())
        return;
    if (g_api->dialog_open_file("/", 0, image_dialog_result, 0) == 0)
    {
        g_dialog_started = 1u;
        set_status("choose an image file");
    }
    else
    {
        set_status("file dialog is busy");
    }
}

static void destroy_loaded_image(void)
{
    if (!g_api)
        return;
    if (g_image_obj)
    {
        (void)g_api->gfx_obj_destroy(g_image_obj);
        g_image_obj = 0;
    }
    if (g_image)
    {
        (void)g_api->img_destroy(g_image);
        g_image = 0;
    }
    g_image_w = 0;
    g_image_h = 0;
}

static void load_pending_image(void)
{
    uint32_t image = 0;
    uint32_t obj = 0;
    uint32_t w = 0;
    uint32_t h = 0;

    if (!g_api || !g_pending_load)
        return;
    g_pending_load = 0;

    destroy_loaded_image();
    if (g_api->img_load(g_pending_raw, &image) != 0 || !image)
    {
        set_status("could not load image");
        set_title_for_path(0);
        return;
    }
    if (g_api->img_size(image, &w, &h) != 0 || !w || !h)
    {
        (void)g_api->img_destroy(image);
        set_status("image has invalid dimensions");
        set_title_for_path(0);
        return;
    }
    if (g_api->gfx_obj_add_image_from_img(image, 0, 0, &obj) != 0 || !obj)
    {
        (void)g_api->img_destroy(image);
        set_status("could not create image view");
        set_title_for_path(0);
        return;
    }

    g_image = image;
    g_image_obj = obj;
    g_image_w = w;
    g_image_h = h;
    g_fit_mode = 1u;
    g_dragging = 0u;
    g_zoom_pct = fit_zoom();

    (void)g_api->gfx_obj_set_parent(g_image_obj, g_viewport);
    (void)g_api->gfx_obj_set_clip_to_parent(g_image_obj, 1u);
    (void)g_api->gfx_image_set_sample_mode(g_image_obj, SACX_GFX_IMAGE_SAMPLE_BILINEAR);
    apply_image_layout();

    copy_text(g_status, sizeof(g_status), "loaded ");
    append_text(g_status, sizeof(g_status), g_pending_friendly[0] ? g_pending_friendly : g_pending_raw);
    if (g_status_text)
        (void)g_api->gfx_text_set(g_status_text, g_status);
    set_title_for_path(g_pending_friendly[0] ? g_pending_friendly : g_pending_raw);
}

static void image_dialog_result(int accepted, const char *raw_path, const char *friendly_path, void *user)
{
    (void)user;
    if (!accepted || !raw_path || !raw_path[0])
    {
        set_status("open cancelled");
        return;
    }

    copy_text(g_pending_raw, sizeof(g_pending_raw), raw_path);
    copy_text(g_pending_friendly, sizeof(g_pending_friendly), friendly_path ? friendly_path : raw_path);
    g_pending_load = 1u;
}

static void open_click(uint32_t button_handle, void *user)
{
    (void)button_handle;
    (void)user;
    open_dialog();
}

static void zoom_out_click(uint32_t button_handle, void *user)
{
    (void)button_handle;
    (void)user;
    if (!g_image_obj)
        return;
    g_fit_mode = 0u;
    g_zoom_pct = clamp_zoom((g_zoom_pct * 80u) / 100u);
    apply_image_layout();
    update_zoom_status();
}

static void zoom_in_click(uint32_t button_handle, void *user)
{
    (void)button_handle;
    (void)user;
    if (!g_image_obj)
        return;
    g_fit_mode = 0u;
    g_zoom_pct = clamp_zoom((g_zoom_pct * 125u) / 100u);
    apply_image_layout();
    update_zoom_status();
}

static void fit_click(uint32_t button_handle, void *user)
{
    (void)button_handle;
    (void)user;
    if (!g_image_obj)
        return;
    g_fit_mode = 1u;
    apply_image_layout();
    update_zoom_status();
}

static int point_in_viewport(int32_t x, int32_t y)
{
    if (x < g_view_screen_x || y < g_view_screen_y)
        return 0;
    if (x >= g_view_screen_x + (int32_t)g_view_w)
        return 0;
    if (y >= g_view_screen_y + (int32_t)g_view_h)
        return 0;
    if (g_api && g_window && g_api->window_point_can_receive_input)
        return g_api->window_point_can_receive_input(g_window, x, y) ? 1 : 0;
    return 1;
}

static void handle_mouse_view(void)
{
    sacx_mouse_state mouse;
    uint8_t left_down = 0u;
    int inside = 0;

    if (!g_api || !g_image_obj || !g_api->mouse_get_state)
        return;
    if (g_api->dialog_active && g_api->dialog_active())
        return;
    if (g_api->mouse_get_state(&mouse) != 0)
        return;

    left_down = (mouse.buttons & 0x01u) ? 1u : 0u;
    inside = point_in_viewport(mouse.x, mouse.y);

    if (mouse.wheel != 0 && inside)
    {
        uint32_t zoom = g_zoom_pct;
        int32_t local_x = mouse.x - g_view_screen_x;
        int32_t local_y = mouse.y - g_view_screen_y;
        if (local_x < 0)
            local_x = 0;
        if (local_y < 0)
            local_y = 0;

        if (mouse.wheel > 0)
            zoom = clamp_zoom((zoom * 80u) / 100u);
        else
            zoom = clamp_zoom((zoom * 125u) / 100u);
        zoom_about((uint32_t)local_x, (uint32_t)local_y, zoom);
        update_zoom_status();
    }

    if (left_down && (inside || g_dragging))
    {
        if (!g_dragging)
        {
            g_dragging = 1u;
            g_last_mouse_x = mouse.x;
            g_last_mouse_y = mouse.y;
            if (g_api->mouse_set_cursor)
                (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_MOVE);
            return;
        }

        g_fit_mode = 0u;
        g_image_x += mouse.x - g_last_mouse_x;
        g_image_y += mouse.y - g_last_mouse_y;
        g_last_mouse_x = mouse.x;
        g_last_mouse_y = mouse.y;
        clamp_pan();
        sync_image_object();
        return;
    }

    if (g_dragging)
    {
        g_dragging = 0u;
        if (g_api->mouse_set_cursor)
            (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_ARROW);
    }
}

static int create_button(const char *label, sacx_button_on_click_fn callback, uint32_t *out_button, uint32_t *out_root, uint32_t *out_label)
{
    sacx_button_style style = sacx_button_style_default();
    uint32_t button = 0;
    uint32_t root = 0;
    uint32_t text = 0;

    style.fill = rgb(36, 48, 58);
    style.hover_fill = rgb(58, 78, 92);
    style.pressed_fill = rgb(82, 108, 126);
    style.outline = rgb(162, 195, 214);
    style.outline_width = 1;

    if (!g_api || !out_button || !out_root || !out_label)
        return -1;
    if (g_api->button_add_rect(0, 0, 40, 28, 3, &style, callback, 0, &button) != 0)
        return -1;
    if (g_api->button_root(button, &root) != 0)
        return -1;
    if (g_api->gfx_obj_add_text(label, 20, 7, 1, rgb(238, 246, 250), 255, 1, 0, 0, SACX_TEXT_ALIGN_CENTER, 1, &text) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(root, g_root);
    (void)g_api->gfx_obj_set_parent(text, root);
    (void)g_api->gfx_obj_set_clip_to_parent(root, 1u);

    *out_button = button;
    *out_root = root;
    *out_label = text;
    return 0;
}

static int create_ui(void)
{
    sacx_window_style style = sacx_window_style_default();

    style.body_fill = rgb(10, 14, 18);
    style.body_outline = rgb(71, 111, 132);
    style.titlebar_fill = rgb(20, 31, 40);
    style.title_color = rgb(232, 243, 248);
    style.close_button_style.fill = rgb(132, 46, 42);
    style.close_button_style.hover_fill = rgb(178, 64, 56);
    style.close_button_style.pressed_fill = rgb(94, 28, 26);
    style.fullscreen_button_style.fill = rgb(58, 86, 104);
    style.fullscreen_button_style.hover_fill = rgb(76, 111, 132);
    style.fullscreen_button_style.pressed_fill = rgb(42, 64, 80);
    style.titlebar_height = 38u;
    style.title_scale = 2u;
    style.close_glyph_scale = 2u;
    style.fullscreen_glyph_scale = 2u;

    if (g_api->window_create_ex(110, 84, 780, 540, 24, "Image Viewer", &style, &g_window) != 0)
        return -1;
    if (g_api->window_root(g_window, &g_root) != 0)
        return -1;

    if (g_api->gfx_obj_add_rect(14, 88, 640, 360, 1, rgb(6, 8, 10), 1, &g_viewport) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(g_viewport, g_root);
    (void)g_api->gfx_obj_set_outline_rgb(g_viewport, 56, 86, 102);
    (void)g_api->gfx_obj_set_outline_width(g_viewport, 1);
    (void)g_api->gfx_obj_set_clip_to_parent(g_viewport, 1u);

    if (g_api->gfx_obj_add_text("open an image to begin", 16, 504, 2, rgb(185, 208, 220), 255, 1, 0, 0, SACX_TEXT_ALIGN_LEFT, 1, &g_status_text) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(g_status_text, g_root);

    if (g_api->gfx_obj_add_text("O=open  +/-=zoom  F=fit  X=quit", 430, 54, 2, rgb(136, 162, 176), 255, 1, 0, 0, SACX_TEXT_ALIGN_LEFT, 1, &g_hint_text) == 0)
        (void)g_api->gfx_obj_set_parent(g_hint_text, g_root);

    if (create_button("Open", open_click, &g_open_button, &g_open_button_root, &g_open_label) != 0)
        return -1;
    if (create_button("-", zoom_out_click, &g_zoom_out_button, &g_zoom_out_button_root, &g_zoom_out_label) != 0)
        return -1;
    if (create_button("+", zoom_in_click, &g_zoom_in_button, &g_zoom_in_button_root, &g_zoom_in_label) != 0)
        return -1;
    if (create_button("Fit", fit_click, &g_fit_button, &g_fit_button_root, &g_fit_label) != 0)
        return -1;

    (void)g_api->window_set_visible(g_window, 1u);
    layout();
    return 0;
}

static int image_viewer_update(const sacx_api *api)
{
    g_api = api;

    if (!api || !g_window)
        return -1;
    if (!api->window_visible(g_window))
    {
        destroy_loaded_image();
        return api->app_exit(0, "image viewer closed");
    }

    layout();
    handle_mouse_view();

    if (!g_dialog_started)
        open_dialog();

    if (api->input_key_pressed(SACX_KEY_O))
        open_dialog();
    if (api->input_key_pressed(SACX_KEY_EQUAL) || api->input_key_pressed(SACX_KEY_KP_PLUS))
        zoom_in_click(0, 0);
    if (api->input_key_pressed(SACX_KEY_MINUS) || api->input_key_pressed(SACX_KEY_KP_MINUS))
        zoom_out_click(0, 0);
    if (api->input_key_pressed(SACX_KEY_F))
        fit_click(0, 0);

    load_pending_image();
    return api->app_yield();
}

static void load_startup_image_arg(const sacx_api *api)
{
    const char *raw = 0;
    const char *friendly = 0;

    if (!api)
        return;

    raw = sacx_app_arg_raw_path(api);
    if (!raw || !raw[0])
        return;

    friendly = sacx_app_arg_friendly_path(api);
    copy_text(g_pending_raw, sizeof(g_pending_raw), raw);
    copy_text(g_pending_friendly, sizeof(g_pending_friendly), (friendly && friendly[0]) ? friendly : raw);
    g_pending_load = 1u;
    g_dialog_started = 1u;
    set_status("loading image");
    set_title_for_path(g_pending_friendly);
}

extern "C" int sacx_main(const sacx_api *api)
{
    if (!api || api->abi_version != SACX_API_ABI_VERSION)
        return -1;

    g_api = api;
    (void)SACX_APP_NO_CONSOLE(api);
    if (create_ui() != 0)
        return api->app_exit(-1, "image viewer UI failed");
    load_startup_image_arg(api);
    if (api->app_set_update(image_viewer_update) != 0)
        return api->app_exit(-1, "image viewer update failed");
    return 0;
}
