#include "editor.h"

typedef struct ui_button
{
    uint32_t button;
    uint32_t root;
    uint32_t label;
    char text[32];
} ui_button;

enum action_id
{
    ACTION_OPEN = 1,
    ACTION_SAVE,
    ACTION_SAVE_AS,
    ACTION_EXPORT,
    ACTION_COPY,
    ACTION_UNDO,
    ACTION_REDO,
    ACTION_ZOOM_OUT,
    ACTION_ZOOM_IN,
    ACTION_FIT,
    ACTION_ROTATE,
    ACTION_FLIP_X,
    ACTION_FLIP_Y,
    ACTION_RESIZE,
    ACTION_LAYER_UP,
    ACTION_LAYER_DOWN,
    ACTION_LAYER_VISIBLE,
    ACTION_LAYER_LOCK,
    ACTION_LAYER_DELETE,
    ACTION_OPACITY_DOWN,
    ACTION_OPACITY_UP,
    ACTION_STROKE_DOWN,
    ACTION_STROKE_UP,
    ACTION_BRIGHTNESS_DOWN,
    ACTION_BRIGHTNESS_UP,
    ACTION_CONTRAST_DOWN,
    ACTION_CONTRAST_UP,
    ACTION_SATURATION_DOWN,
    ACTION_SATURATION_UP,
    ACTION_GRAYSCALE,
    ACTION_COLOR_RED,
    ACTION_COLOR_YELLOW,
    ACTION_COLOR_WHITE,
    ACTION_COLOR_BLACK,
    ACTION_COLOR_CYAN,
    ACTION_MODAL_PRIMARY,
    ACTION_MODAL_SECONDARY,
    ACTION_MODAL_CANCEL,
    ACTION_TOOL_BASE = 100,
    ACTION_LAYER_ROW_BASE = 200,
};

enum dialog_purpose
{
    DIALOG_NONE = 0,
    DIALOG_OPEN,
    DIALOG_SAVE_PROJECT,
    DIALOG_EXPORT,
};

enum modal_mode
{
    MODAL_NONE = 0,
    MODAL_CLOSE,
    MODAL_OVERWRITE_PROJECT,
    MODAL_OVERWRITE_EXPORT,
    MODAL_RESIZE,
};

enum resize_handle
{
    RESIZE_HANDLE_NONE = -1,
    RESIZE_HANDLE_TOP_LEFT = 0,
    RESIZE_HANDLE_TOP_RIGHT,
    RESIZE_HANDLE_BOTTOM_LEFT,
    RESIZE_HANDLE_BOTTOM_RIGHT,
    RESIZE_HANDLE_ARROW_START,
    RESIZE_HANDLE_ARROW_END,
};

static uint32_t g_window = 0u;
static uint32_t g_root = 0u;
static uint32_t g_canvas = 0u;
static uint32_t g_image_obj = 0u;
static uint32_t g_checker_image = 0u;
static uint32_t g_checker_obj = 0u;
static uint32_t g_selection = 0u;
static uint32_t g_selection_handles[4];
static uint32_t g_crop_dim[4];
static uint32_t g_status_text = 0u;
static uint32_t g_zoom_text_obj = 0u;
static uint32_t g_layer_title = 0u;
static uint32_t g_property_text = 0u;
static uint32_t g_textbox = 0u;
static uint32_t g_textbox_root = 0u;

static ui_button g_top[15];
static ui_button g_tools[EDITOR_TOOL_COUNT];
static ui_button g_layer_rows[8];
static ui_button g_properties[17];
static ui_button g_colors[5];
static ui_button g_modal_buttons[3];

static uint32_t g_modal_panel = 0u;
static uint32_t g_modal_title = 0u;
static uint32_t g_modal_body = 0u;
static char g_modal_title_buf[64];
static char g_modal_body_buf[192];

static char g_status[192];
static char g_zoom_text[32];
static char g_property_buf[192];
static char g_title[192];
static char g_pending_raw[EDITOR_PATH_CAP];
static char g_pending_friendly[EDITOR_PATH_CAP];
static char g_pending_name[128];

static uint32_t g_pending_action = 0u;
static uint32_t g_dialog_purpose = DIALOG_NONE;
static uint32_t g_modal_mode = MODAL_NONE;
static uint32_t g_tool = EDITOR_TOOL_SELECT;
static uint32_t g_zoom_pct = 100u;
static uint32_t g_scaled_w = 1u;
static uint32_t g_scaled_h = 1u;
static uint32_t g_view_w = 1u;
static uint32_t g_view_h = 1u;
static int32_t g_view_screen_x = 0;
static int32_t g_view_screen_y = 0;
static int32_t g_image_x = 0;
static int32_t g_image_y = 0;
static uint8_t g_fit_mode = 1u;
static uint8_t g_dragging = 0u;
static uint8_t g_resizing = 0u;
static uint8_t g_drag_changed = 0u;
static uint8_t g_creating_layer = 0u;
static int32_t g_resize_handle = RESIZE_HANDLE_NONE;
static editor_layer g_drag_original_layer;
static uint8_t g_panning = 0u;
static uint8_t g_pending_close_after_save = 0u;
static uint8_t g_export_busy = 0u;
static uint32_t g_export_progress = 0u;
static uint32_t g_busy_saved_cursor = SACX_MOUSE_CURSOR_ARROW;
static int32_t g_drag_start_doc_x = 0;
static int32_t g_drag_start_doc_y = 0;
static int32_t g_last_doc_x = 0;
static int32_t g_last_doc_y = 0;
static int32_t g_last_mouse_x = 0;
static int32_t g_last_mouse_y = 0;
static uint8_t g_previous_buttons = 0u;
static uint32_t g_layer_row_base = 0u;

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    sacx_color color = {r, g, b};
    return color;
}

static int text_equal(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a && *b)
    {
        if (*a++ != *b++)
            return 0;
    }
    return *a == *b;
}

static int parse_dimensions(const char *text, uint32_t *out_w, uint32_t *out_h)
{
    uint64_t w = 0u;
    uint64_t h = 0u;
    if (!text || !out_w || !out_h)
        return -1;
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text < '0' || *text > '9')
        return -1;
    while (*text >= '0' && *text <= '9')
    {
        w = w * 10u + (uint32_t)(*text++ - '0');
        if (w > 8192u)
            return -1;
    }
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text != 'x' && *text != 'X')
        return -1;
    ++text;
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text < '0' || *text > '9')
        return -1;
    while (*text >= '0' && *text <= '9')
    {
        h = h * 10u + (uint32_t)(*text++ - '0');
        if (h > 8192u)
            return -1;
    }
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text || !w || !h || w * h * 4ull > 256ull * 1024ull * 1024ull)
        return -1;
    *out_w = (uint32_t)w;
    *out_h = (uint32_t)h;
    return 0;
}

static uint32_t line_height(void)
{
    return g_api && g_api->text_line_height ? g_api->text_line_height(1u, 0) : 8u;
}

static int32_t text_center_y(uint32_t h)
{
    uint32_t text_h = line_height();
    return h > text_h ? (int32_t)((h - text_h) / 2u) : 0;
}

static void set_status(const char *text)
{
    editor_copy_text(g_status, sizeof(g_status), text);
    if (g_status_text)
        (void)g_api->gfx_text_set(g_status_text, g_status);
}

static void update_title(void)
{
    const char *path = g_doc.project_friendly[0] ? g_doc.project_friendly : g_doc.source_friendly;
    editor_copy_text(g_title, sizeof(g_title), "Image Editor");
    if (path && path[0])
    {
        editor_append_text(g_title, sizeof(g_title), " - ");
        editor_append_text(g_title, sizeof(g_title), path);
    }
    if (g_doc.dirty)
        editor_append_text(g_title, sizeof(g_title), " *");
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
    if (!g_doc.canvas_w || !g_doc.canvas_h)
        return 100u;
    by_w = g_view_w * 100u / g_doc.canvas_w;
    by_h = g_view_h * 100u / g_doc.canvas_h;
    fit = by_w < by_h ? by_w : by_h;
    if (!fit)
        fit = 1u;
    return clamp_zoom(fit);
}

static void clamp_pan(void)
{
    if (g_scaled_w <= g_view_w)
        g_image_x = (int32_t)((g_view_w - g_scaled_w) / 2u);
    else
    {
        int32_t minimum = (int32_t)g_view_w - (int32_t)g_scaled_w;
        if (g_image_x > 0)
            g_image_x = 0;
        if (g_image_x < minimum)
            g_image_x = minimum;
    }
    if (g_scaled_h <= g_view_h)
        g_image_y = (int32_t)((g_view_h - g_scaled_h) / 2u);
    else
    {
        int32_t minimum = (int32_t)g_view_h - (int32_t)g_scaled_h;
        if (g_image_y > 0)
            g_image_y = 0;
        if (g_image_y < minimum)
            g_image_y = minimum;
    }
}

static void sync_image_layout(void)
{
    if (!g_image_obj || !g_doc.canvas_w || !g_doc.canvas_h)
        return;
    if (g_fit_mode)
        g_zoom_pct = fit_zoom();
    g_scaled_w = (uint32_t)((uint64_t)g_doc.canvas_w * g_zoom_pct / 100u);
    g_scaled_h = (uint32_t)((uint64_t)g_doc.canvas_h * g_zoom_pct / 100u);
    if (!g_scaled_w)
        g_scaled_w = 1u;
    if (!g_scaled_h)
        g_scaled_h = 1u;
    if (g_fit_mode)
    {
        g_image_x = g_scaled_w < g_view_w ? (int32_t)((g_view_w - g_scaled_w) / 2u) : 0;
        g_image_y = g_scaled_h < g_view_h ? (int32_t)((g_view_h - g_scaled_h) / 2u) : 0;
    }
    clamp_pan();
    (void)g_api->gfx_image_set_size(g_image_obj, g_scaled_w, g_scaled_h);
    (void)g_api->gfx_image_set_pos(g_image_obj, g_image_x, g_image_y);
    g_zoom_text[0] = 0;
    editor_append_uint(g_zoom_text, sizeof(g_zoom_text), g_zoom_pct);
    editor_append_text(g_zoom_text, sizeof(g_zoom_text), "%");
    if (g_zoom_text_obj)
        (void)g_api->gfx_text_set(g_zoom_text_obj, g_zoom_text);
}

static int create_button(ui_button *item, const char *text, uint32_t action)
{
    sacx_button_style style = sacx_button_style_default();
    if (!item)
        return -1;
    *item = (ui_button){0};
    editor_copy_text(item->text, sizeof(item->text), text);
    style.fill = rgb(35, 45, 55);
    style.hover_fill = rgb(55, 74, 88);
    style.pressed_fill = rgb(75, 103, 120);
    style.outline = rgb(112, 145, 162);
    style.outline_width = 1u;
    if (g_api->button_add_rect(0, 0, 40, 28, 4, &style,
                               [](uint32_t button, void *user)
                               {
                                   (void)button;
                                   g_pending_action = (uint32_t)(uintptr_t)user;
                               },
                               (void *)(uintptr_t)action, &item->button) != 0 ||
        g_api->button_root(item->button, &item->root) != 0 ||
        g_api->gfx_obj_add_text(item->text, 0, 0, 1, rgb(235, 242, 246), 255u, 1u,
                                0, 0, SACX_TEXT_ALIGN_CENTER, 1u, &item->label) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(item->root, g_root);
    (void)g_api->gfx_obj_set_parent(item->label, item->root);
    return 0;
}

static void layout_button(ui_button *item, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (!item || !item->root)
        return;
    (void)g_api->gfx_obj_set_rect(item->root, x, y, w, h);
    (void)g_api->gfx_text_set_pos(item->label, (int32_t)(w / 2u), text_center_y(h));
}

static void set_button_text(ui_button *item, const char *text)
{
    if (!item)
        return;
    editor_copy_text(item->text, sizeof(item->text), text);
    (void)g_api->gfx_text_set(item->label, item->text);
}

static void show_button(ui_button *item, uint8_t visible)
{
    if (!item)
        return;
    (void)g_api->gfx_obj_set_visible(item->root, visible);
    (void)g_api->gfx_obj_set_visible(item->label, visible);
    (void)g_api->button_set_enabled(item->button, visible);
}

static void set_editor_controls_enabled(uint32_t enabled)
{
    for (uint32_t i = 0u; i < 15u; ++i)
        (void)g_api->button_set_enabled(g_top[i].button, enabled);
    for (uint32_t i = 0u; i < EDITOR_TOOL_COUNT; ++i)
        (void)g_api->button_set_enabled(g_tools[i].button, enabled);
    for (uint32_t i = 0u; i < 8u; ++i)
        (void)g_api->button_set_enabled(g_layer_rows[i].button, enabled);
    for (uint32_t i = 0u; i < 17u; ++i)
        (void)g_api->button_set_enabled(g_properties[i].button, enabled);
    for (uint32_t i = 0u; i < 5u; ++i)
        (void)g_api->button_set_enabled(g_colors[i].button, enabled);
    for (uint32_t i = 0u; i < 3u; ++i)
        (void)g_api->button_set_enabled(g_modal_buttons[i].button, enabled);
    (void)g_api->textbox_set_enabled(g_textbox, enabled);
}

static void refresh_tool_styles(void)
{
    for (uint32_t i = 0u; i < EDITOR_TOOL_COUNT; ++i)
    {
        sacx_button_style style = sacx_button_style_default();
        uint8_t active = i == g_tool;
        style.fill = active ? rgb(22, 103, 132) : rgb(35, 45, 55);
        style.hover_fill = active ? rgb(31, 126, 158) : rgb(55, 74, 88);
        style.pressed_fill = rgb(50, 151, 184);
        style.outline = active ? rgb(72, 218, 255) : rgb(112, 145, 162);
        style.outline_width = active ? 2u : 1u;
        (void)g_api->button_set_style(g_tools[i].button, &style);
    }
}

static int layer_uses_color(uint32_t type)
{
    return type == EDITOR_LAYER_TEXT || type == EDITOR_LAYER_ARROW ||
           type == EDITOR_LAYER_RECT || type == EDITOR_LAYER_ELLIPSE ||
           type == EDITOR_LAYER_PEN || type == EDITOR_LAYER_HIGHLIGHTER;
}

static int layer_uses_size(uint32_t type)
{
    return layer_uses_color(type) || type == EDITOR_LAYER_BLUR ||
           type == EDITOR_LAYER_PIXELATE;
}

static int layer_previews_live(uint32_t type)
{
    return type == EDITOR_LAYER_ARROW || type == EDITOR_LAYER_RECT ||
           type == EDITOR_LAYER_ELLIPSE || type == EDITOR_LAYER_PEN ||
           type == EDITOR_LAYER_HIGHLIGHTER;
}

static void layout(void)
{
    int32_t root_x = 0;
    int32_t root_y = 0;
    uint32_t root_w = 0u;
    uint32_t root_h = 0u;
    const uint32_t top_y = 44u;
    const uint32_t top_h = 30u;
    const uint32_t left_w = 118u;
    const uint32_t right_w = 276u;
    const uint32_t status_h = 26u;
    uint32_t canvas_y = 84u;
    uint32_t canvas_h = 1u;
    int32_t x = 12;

    if (g_api->gfx_obj_get_rect(g_root, &root_x, &root_y, &root_w, &root_h) != 0)
        return;
    if (root_w < 760u)
        root_w = 760u;
    if (root_h < 480u)
        root_h = 480u;

    const uint32_t top_widths[15] = {68, 64, 72, 58, 56, 52, 36, 36, 48, 58, 60, 60, 62, 68, 56};
    for (uint32_t i = 0u; i < 15u; ++i)
    {
        layout_button(&g_top[i], x, (int32_t)top_y, top_widths[i], top_h);
        x += (int32_t)top_widths[i] + 4;
    }

    canvas_h = root_h > canvas_y + status_h + 14u ? root_h - canvas_y - status_h - 14u : 1u;
    g_view_w = root_w > left_w + right_w + 28u ? root_w - left_w - right_w - 28u : 1u;
    g_view_h = canvas_h;
    (void)g_api->gfx_obj_set_rect(g_canvas, (int32_t)left_w + 10, (int32_t)canvas_y, g_view_w, g_view_h);
    g_view_screen_x = root_x + (int32_t)left_w + 10;
    g_view_screen_y = root_y + (int32_t)canvas_y;
    (void)g_api->gfx_image_set_pos(g_checker_obj, 0, 0);
    (void)g_api->gfx_image_set_size(g_checker_obj, g_view_w, g_view_h);
    (void)g_api->gfx_obj_set_visible(g_checker_obj, 1u);

    for (uint32_t i = 0u; i < EDITOR_TOOL_COUNT; ++i)
        layout_button(&g_tools[i], 10, (int32_t)canvas_y + (int32_t)i * 34, left_w - 20u, 29u);

    int32_t panel_x = (int32_t)root_w - (int32_t)right_w + 8;
    (void)g_api->gfx_text_set_pos(g_layer_title, panel_x, (int32_t)canvas_y);
    uint32_t visible_layer_rows = g_doc.layer_count < 8u ? g_doc.layer_count : 8u;
    int32_t layer_rows_y = (int32_t)canvas_y + 38;
    for (uint32_t i = 0u; i < 8u; ++i)
        layout_button(&g_layer_rows[i], panel_x, layer_rows_y + (int32_t)i * 31, right_w - 18u, 27u);

    int32_t prop_y = layer_rows_y + (int32_t)visible_layer_rows * 31 + 18;
    (void)g_api->gfx_text_set_pos(g_property_text, panel_x, prop_y);
    prop_y += 70;
    uint32_t control_w = right_w - 18u;
    uint32_t half_w = (control_w - 6u) / 2u;
    layout_button(&g_properties[0], panel_x, prop_y, half_w, 27u);
    layout_button(&g_properties[1], panel_x + (int32_t)half_w + 6, prop_y, half_w, 27u);
    prop_y += 31;
    layout_button(&g_properties[2], panel_x, prop_y, half_w, 27u);
    layout_button(&g_properties[3], panel_x + (int32_t)half_w + 6, prop_y, half_w, 27u);
    prop_y += 31;
    layout_button(&g_properties[4], panel_x, prop_y, control_w, 27u);
    prop_y += 31;
    layout_button(&g_properties[5], panel_x, prop_y, half_w, 27u);
    layout_button(&g_properties[6], panel_x + (int32_t)half_w + 6, prop_y, half_w, 27u);
    prop_y += 31;

    uint32_t selected_type = 0u;
    if (g_doc.selected_layer >= 0 && (uint32_t)g_doc.selected_layer < g_doc.layer_count)
        selected_type = g_doc.layers[g_doc.selected_layer].type;
    if (layer_uses_size(selected_type))
    {
        layout_button(&g_properties[7], panel_x, prop_y, half_w, 27u);
        layout_button(&g_properties[8], panel_x + (int32_t)half_w + 6, prop_y, half_w, 27u);
        prop_y += 31;
    }
    if (layer_uses_color(selected_type))
    {
        uint32_t color_w = (control_w - 16u) / 5u;
        for (uint32_t i = 0u; i < 5u; ++i)
            layout_button(&g_colors[i], panel_x + (int32_t)i * ((int32_t)color_w + 4),
                          prop_y, color_w, 27u);
        prop_y += 31;
    }
    if (selected_type == EDITOR_LAYER_ADJUSTMENT)
    {
        for (uint32_t i = 9u; i <= 14u; i += 2u)
        {
            layout_button(&g_properties[i], panel_x, prop_y, half_w, 27u);
            layout_button(&g_properties[i + 1u], panel_x + (int32_t)half_w + 6, prop_y, half_w, 27u);
            prop_y += 31;
        }
        layout_button(&g_properties[15], panel_x, prop_y, control_w, 27u);
        prop_y += 31;
    }

    if (g_textbox_root && g_modal_mode != MODAL_RESIZE)
        (void)g_api->textbox_set_bounds(g_textbox, panel_x, prop_y, control_w, 30u);
    (void)g_api->gfx_text_set_pos(g_status_text, 14, (int32_t)(root_h - status_h));
    sync_image_layout();

    if (g_modal_mode != MODAL_NONE)
    {
        uint32_t modal_w = 430u;
        uint32_t modal_h = 190u;
        int32_t modal_x = (int32_t)(root_w - modal_w) / 2;
        int32_t modal_y = (int32_t)(root_h - modal_h) / 2;
        (void)g_api->gfx_obj_set_rect(g_modal_panel, modal_x, modal_y, modal_w, modal_h);
        (void)g_api->gfx_text_set_pos(g_modal_title, modal_x + 18, modal_y + 18);
        (void)g_api->gfx_text_set_pos(g_modal_body, modal_x + 18, modal_y + 54);
        layout_button(&g_modal_buttons[0], modal_x + 18, modal_y + 140, 118u, 30u);
        layout_button(&g_modal_buttons[1], modal_x + 154, modal_y + 140, 118u, 30u);
        layout_button(&g_modal_buttons[2], modal_x + 290, modal_y + 140, 118u, 30u);
        if (g_modal_mode == MODAL_RESIZE)
            (void)g_api->textbox_set_bounds(g_textbox, modal_x + 18, modal_y + 92, modal_w - 36u, 30u);
    }
}

static int attach_preview(void)
{
    if (g_image_obj)
    {
        (void)g_api->gfx_obj_destroy(g_image_obj);
        g_image_obj = 0u;
    }
    if (!g_doc.preview ||
        g_api->gfx_obj_add_image_from_img(g_doc.preview, 0, 0, &g_image_obj) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(g_image_obj, g_canvas);
    (void)g_api->gfx_obj_set_clip_to_parent(g_image_obj, 1u);
    (void)g_api->gfx_obj_set_z(g_image_obj, 1);
    (void)g_api->gfx_image_set_sample_mode(g_image_obj, SACX_GFX_IMAGE_SAMPLE_BILINEAR);
    g_fit_mode = 1u;
    sync_image_layout();
    return 0;
}

static void refresh_layer_rows(void)
{
    g_layer_row_base = g_doc.layer_count > 8u ? g_doc.layer_count - 8u : 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
    {
        uint32_t index = g_layer_row_base + i;
        if (index < g_doc.layer_count)
        {
            char text[32];
            text[0] = 0;
            editor_append_text(text, sizeof(text), (int32_t)index == g_doc.selected_layer ? "> " : "  ");
            editor_append_uint(text, sizeof(text), index + 1u);
            editor_append_text(text, sizeof(text), " ");
            editor_append_text(text, sizeof(text), document_layer_name(g_doc.layers[index].type));
            if (!g_doc.layers[index].visible)
                editor_append_text(text, sizeof(text), " [off]");
            set_button_text(&g_layer_rows[i], text);
            show_button(&g_layer_rows[i], 1u);
        }
        else
        {
            show_button(&g_layer_rows[i], 0u);
        }
    }
}

static void refresh_properties(void)
{
    for (uint32_t i = 0u; i < 17u; ++i)
        show_button(&g_properties[i], 0u);
    for (uint32_t i = 0u; i < 5u; ++i)
        show_button(&g_colors[i], 0u);
    if (g_textbox_root && g_modal_mode != MODAL_RESIZE)
        (void)g_api->gfx_obj_set_visible(g_textbox_root, 0u);

    g_property_buf[0] = 0;
    if (g_doc.selected_layer >= 0 && (uint32_t)g_doc.selected_layer < g_doc.layer_count)
    {
        editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
        for (uint32_t i = 0u; i <= 6u; ++i)
            show_button(&g_properties[i], 1u);
        editor_append_text(g_property_buf, sizeof(g_property_buf), document_layer_name(layer->type));
        editor_append_text(g_property_buf, sizeof(g_property_buf), "\nopacity ");
        editor_append_uint(g_property_buf, sizeof(g_property_buf), layer->opacity * 100u / 255u);
        editor_append_text(g_property_buf, sizeof(g_property_buf), "%");
        if (layer->type == EDITOR_LAYER_BLUR)
        {
            editor_append_text(g_property_buf, sizeof(g_property_buf), "  radius ");
            editor_append_uint(g_property_buf, sizeof(g_property_buf), layer->effect_strength);
            set_button_text(&g_properties[7], "Radius -");
            set_button_text(&g_properties[8], "Radius +");
        }
        else if (layer->type == EDITOR_LAYER_PIXELATE)
        {
            editor_append_text(g_property_buf, sizeof(g_property_buf), "  block ");
            editor_append_uint(g_property_buf, sizeof(g_property_buf), layer->effect_strength);
            set_button_text(&g_properties[7], "Block -");
            set_button_text(&g_properties[8], "Block +");
        }
        else if (layer_uses_size(layer->type))
        {
            editor_append_text(g_property_buf, sizeof(g_property_buf), "  size ");
            editor_append_uint(g_property_buf, sizeof(g_property_buf), layer->stroke);
            set_button_text(&g_properties[7], "Size -");
            set_button_text(&g_properties[8], "Size +");
        }
        if (layer_uses_size(layer->type))
        {
            show_button(&g_properties[7], 1u);
            show_button(&g_properties[8], 1u);
        }
        if (layer_uses_color(layer->type))
            for (uint32_t i = 0u; i < 5u; ++i)
                show_button(&g_colors[i], 1u);
        if (layer->type == EDITOR_LAYER_ADJUSTMENT)
        {
            for (uint32_t i = 9u; i <= 15u; ++i)
                show_button(&g_properties[i], 1u);
            editor_append_text(g_property_buf, sizeof(g_property_buf), "\nB ");
            if (layer->brightness < 0)
                editor_append_text(g_property_buf, sizeof(g_property_buf), "-");
            editor_append_uint(g_property_buf, sizeof(g_property_buf),
                               layer->brightness < 0 ? (uint32_t)-layer->brightness : (uint32_t)layer->brightness);
            editor_append_text(g_property_buf, sizeof(g_property_buf), " C ");
            if (layer->contrast < 0)
                editor_append_text(g_property_buf, sizeof(g_property_buf), "-");
            editor_append_uint(g_property_buf, sizeof(g_property_buf),
                               layer->contrast < 0 ? (uint32_t)-layer->contrast : (uint32_t)layer->contrast);
            editor_append_text(g_property_buf, sizeof(g_property_buf), " S ");
            if (layer->saturation < 0)
                editor_append_text(g_property_buf, sizeof(g_property_buf), "-");
            editor_append_uint(g_property_buf, sizeof(g_property_buf),
                               layer->saturation < 0 ? (uint32_t)-layer->saturation : (uint32_t)layer->saturation);
        }
        if (layer->type == EDITOR_LAYER_TEXT)
        {
            (void)g_api->textbox_set_text(g_textbox, layer->text);
            (void)g_api->textbox_set_enabled(g_textbox, 1u);
            (void)g_api->gfx_obj_set_visible(g_textbox_root, 1u);
        }
        else
        {
            (void)g_api->textbox_set_enabled(g_textbox, 0u);
        }
    }
    else
    {
        editor_append_text(g_property_buf, sizeof(g_property_buf), "No layer selected");
        (void)g_api->textbox_set_enabled(g_textbox, 0u);
    }
    (void)g_api->gfx_text_set(g_property_text, g_property_buf);
    refresh_layer_rows();
    update_title();
    layout();
}

static void hide_selection_overlay(void)
{
    (void)g_api->gfx_obj_set_visible(g_selection, 0u);
    for (uint32_t i = 0u; i < 4u; ++i)
        (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 0u);
}

static void hide_crop_dim(void)
{
    for (uint32_t i = 0u; i < 4u; ++i)
        (void)g_api->gfx_obj_set_visible(g_crop_dim[i], 0u);
}

static void set_crop_dim_rect(uint32_t index, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (index >= 4u || !w || !h)
    {
        if (index < 4u)
            (void)g_api->gfx_obj_set_visible(g_crop_dim[index], 0u);
        return;
    }
    (void)g_api->gfx_obj_set_rect(g_crop_dim[index], x, y, w, h);
    (void)g_api->gfx_obj_set_visible(g_crop_dim[index], 1u);
}

static int32_t document_to_canvas_x(int32_t x)
{
    return g_image_x + (int32_t)((int64_t)x * g_scaled_w / g_doc.canvas_w);
}

static int32_t document_to_canvas_y(int32_t y)
{
    return g_image_y + (int32_t)((int64_t)y * g_scaled_h / g_doc.canvas_h);
}

static void update_crop_overlay(void)
{
    int32_t x0 = g_doc.crop_x;
    int32_t y0 = g_doc.crop_y;
    int32_t x1 = g_doc.crop_x + (int32_t)g_doc.crop_w - 1;
    int32_t y1 = g_doc.crop_y + (int32_t)g_doc.crop_h - 1;
    if (g_dragging)
    {
        x0 = g_drag_start_doc_x < g_last_doc_x ? g_drag_start_doc_x : g_last_doc_x;
        y0 = g_drag_start_doc_y < g_last_doc_y ? g_drag_start_doc_y : g_last_doc_y;
        x1 = g_drag_start_doc_x > g_last_doc_x ? g_drag_start_doc_x : g_last_doc_x;
        y1 = g_drag_start_doc_y > g_last_doc_y ? g_drag_start_doc_y : g_last_doc_y;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)g_doc.canvas_w) x1 = (int32_t)g_doc.canvas_w - 1;
    if (y1 >= (int32_t)g_doc.canvas_h) y1 = (int32_t)g_doc.canvas_h - 1;
    if (x1 < x0 || y1 < y0)
    {
        hide_selection_overlay();
        hide_crop_dim();
        return;
    }

    int32_t sx = document_to_canvas_x(x0);
    int32_t sy = document_to_canvas_y(y0);
    int32_t ex = document_to_canvas_x(x1 + 1);
    int32_t ey = document_to_canvas_y(y1 + 1);
    uint32_t sw = ex > sx ? (uint32_t)(ex - sx) : 1u;
    uint32_t sh = ey > sy ? (uint32_t)(ey - sy) : 1u;
    (void)g_api->gfx_obj_set_rect(g_selection, sx, sy, sw, sh);
    (void)g_api->gfx_obj_set_visible(g_selection, 1u);
    for (uint32_t i = 0u; i < 4u; ++i)
        (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 0u);

    int32_t image_right = g_image_x + (int32_t)g_scaled_w;
    int32_t image_bottom = g_image_y + (int32_t)g_scaled_h;
    set_crop_dim_rect(0u, g_image_x, g_image_y, g_scaled_w,
                      sy > g_image_y ? (uint32_t)(sy - g_image_y) : 0u);
    set_crop_dim_rect(1u, g_image_x, ey, g_scaled_w,
                      image_bottom > ey ? (uint32_t)(image_bottom - ey) : 0u);
    set_crop_dim_rect(2u, g_image_x, sy,
                      sx > g_image_x ? (uint32_t)(sx - g_image_x) : 0u, sh);
    set_crop_dim_rect(3u, ex, sy,
                      image_right > ex ? (uint32_t)(image_right - ex) : 0u, sh);
}

static void update_selection_overlay(void)
{
    editor_layer *layer = 0;
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    if (!g_image_obj || g_modal_mode != MODAL_NONE)
    {
        hide_selection_overlay();
        hide_crop_dim();
        return;
    }
    if (g_tool == EDITOR_TOOL_CROP)
    {
        update_crop_overlay();
        return;
    }
    hide_crop_dim();
    if (g_doc.selected_layer < 0 || (uint32_t)g_doc.selected_layer >= g_doc.layer_count)
    {
        hide_selection_overlay();
        return;
    }
    layer = &g_doc.layers[g_doc.selected_layer];
    if (g_creating_layer && layer_previews_live(layer->type))
    {
        hide_selection_overlay();
        return;
    }
    if (layer->type == EDITOR_LAYER_ADJUSTMENT)
    {
        hide_selection_overlay();
        return;
    }
    x0 = layer->x0 < layer->x1 ? layer->x0 : layer->x1;
    y0 = layer->y0 < layer->y1 ? layer->y0 : layer->y1;
    x1 = layer->x0 > layer->x1 ? layer->x0 : layer->x1;
    y1 = layer->y0 > layer->y1 ? layer->y0 : layer->y1;
    if ((layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER) && layer->point_count)
    {
        x0 = x1 = g_doc.points[layer->point_start].x;
        y0 = y1 = g_doc.points[layer->point_start].y;
        for (uint32_t i = 1u; i < layer->point_count; ++i)
        {
            editor_point *point = &g_doc.points[layer->point_start + i];
            if (point->x < x0) x0 = point->x;
            if (point->x > x1) x1 = point->x;
            if (point->y < y0) y0 = point->y;
            if (point->y > y1) y1 = point->y;
        }
    }
    int32_t sx = document_to_canvas_x(x0);
    int32_t sy = document_to_canvas_y(y0);
    int32_t ex = document_to_canvas_x(x1 + 1);
    int32_t ey = document_to_canvas_y(y1 + 1);
    uint32_t sw = ex > sx ? (uint32_t)(ex - sx) : 1u;
    uint32_t sh = ey > sy ? (uint32_t)(ey - sy) : 1u;
    (void)g_api->gfx_obj_set_rect(g_selection, sx, sy, sw, sh);
    (void)g_api->gfx_obj_set_visible(g_selection, 1u);
    for (uint32_t i = 0u; i < 4u; ++i)
        (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 0u);

    if (layer->type == EDITOR_LAYER_ARROW)
    {
        int32_t hx[2] = {document_to_canvas_x(layer->x0), document_to_canvas_x(layer->x1)};
        int32_t hy[2] = {document_to_canvas_y(layer->y0), document_to_canvas_y(layer->y1)};
        for (uint32_t i = 0u; i < 2u; ++i)
        {
            (void)g_api->gfx_obj_set_rect(g_selection_handles[i], hx[i] - 5, hy[i] - 5, 10u, 10u);
            (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 1u);
        }
    }
    else if (layer->type != EDITOR_LAYER_PEN && layer->type != EDITOR_LAYER_HIGHLIGHTER &&
             layer->type != EDITOR_LAYER_TEXT)
    {
        const int32_t hx[4] = {sx - 5, sx + (int32_t)sw - 5, sx - 5, sx + (int32_t)sw - 5};
        const int32_t hy[4] = {sy - 5, sy - 5, sy + (int32_t)sh - 5, sy + (int32_t)sh - 5};
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            (void)g_api->gfx_obj_set_rect(g_selection_handles[i], hx[i], hy[i], 10u, 10u);
            (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 1u);
        }
    }
}

static int point_in_canvas(int32_t x, int32_t y)
{
    if (x < g_view_screen_x || y < g_view_screen_y ||
        x >= g_view_screen_x + (int32_t)g_view_w ||
        y >= g_view_screen_y + (int32_t)g_view_h)
        return 0;
    return g_api->window_point_can_receive_input(g_window, x, y) ? 1 : 0;
}

static int screen_to_document(int32_t x, int32_t y, int32_t *doc_x, int32_t *doc_y)
{
    int32_t local_x = x - g_view_screen_x - g_image_x;
    int32_t local_y = y - g_view_screen_y - g_image_y;
    if (!g_doc.canvas_w || !g_doc.canvas_h || local_x < 0 || local_y < 0 ||
        local_x >= (int32_t)g_scaled_w || local_y >= (int32_t)g_scaled_h)
        return 0;
    *doc_x = (int32_t)((int64_t)local_x * g_doc.canvas_w / g_scaled_w);
    *doc_y = (int32_t)((int64_t)local_y * g_doc.canvas_h / g_scaled_h);
    return 1;
}

static int screen_to_document_clamped(int32_t x, int32_t y, int32_t *doc_x, int32_t *doc_y)
{
    int32_t local_x = x - g_view_screen_x - g_image_x;
    int32_t local_y = y - g_view_screen_y - g_image_y;
    if (!g_doc.canvas_w || !g_doc.canvas_h || !g_scaled_w || !g_scaled_h)
        return 0;
    if (local_x < 0) local_x = 0;
    if (local_y < 0) local_y = 0;
    if (local_x >= (int32_t)g_scaled_w) local_x = (int32_t)g_scaled_w - 1;
    if (local_y >= (int32_t)g_scaled_h) local_y = (int32_t)g_scaled_h - 1;
    *doc_x = (int32_t)((int64_t)local_x * g_doc.canvas_w / g_scaled_w);
    *doc_y = (int32_t)((int64_t)local_y * g_doc.canvas_h / g_scaled_h);
    return 1;
}

static uint32_t selection_tolerance(void)
{
    uint32_t tx = g_scaled_w ? (10u * g_doc.canvas_w + g_scaled_w - 1u) / g_scaled_w : 6u;
    uint32_t ty = g_scaled_h ? (10u * g_doc.canvas_h + g_scaled_h - 1u) / g_scaled_h : 6u;
    uint32_t tolerance = tx > ty ? tx : ty;
    if (tolerance < 3u)
        tolerance = 3u;
    return tolerance;
}

static int point_near(int32_t x, int32_t y, int32_t target_x, int32_t target_y, uint32_t tolerance)
{
    return x >= target_x - (int32_t)tolerance && x <= target_x + (int32_t)tolerance &&
           y >= target_y - (int32_t)tolerance && y <= target_y + (int32_t)tolerance;
}

static int32_t selected_resize_handle_at(int32_t doc_x, int32_t doc_y)
{
    if (g_doc.selected_layer < 0 || (uint32_t)g_doc.selected_layer >= g_doc.layer_count)
        return RESIZE_HANDLE_NONE;
    editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
    if (layer->locked || layer->type == EDITOR_LAYER_ADJUSTMENT ||
        layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER ||
        layer->type == EDITOR_LAYER_TEXT)
        return RESIZE_HANDLE_NONE;
    uint32_t tolerance = selection_tolerance();
    if (layer->type == EDITOR_LAYER_ARROW)
    {
        if (point_near(doc_x, doc_y, layer->x0, layer->y0, tolerance))
            return RESIZE_HANDLE_ARROW_START;
        if (point_near(doc_x, doc_y, layer->x1, layer->y1, tolerance))
            return RESIZE_HANDLE_ARROW_END;
        return RESIZE_HANDLE_NONE;
    }
    int32_t x0 = layer->x0 < layer->x1 ? layer->x0 : layer->x1;
    int32_t y0 = layer->y0 < layer->y1 ? layer->y0 : layer->y1;
    int32_t x1 = layer->x0 > layer->x1 ? layer->x0 : layer->x1;
    int32_t y1 = layer->y0 > layer->y1 ? layer->y0 : layer->y1;
    if (point_near(doc_x, doc_y, x0, y0, tolerance))
        return RESIZE_HANDLE_TOP_LEFT;
    if (point_near(doc_x, doc_y, x1, y0, tolerance))
        return RESIZE_HANDLE_TOP_RIGHT;
    if (point_near(doc_x, doc_y, x0, y1, tolerance))
        return RESIZE_HANDLE_BOTTOM_LEFT;
    if (point_near(doc_x, doc_y, x1, y1, tolerance))
        return RESIZE_HANDLE_BOTTOM_RIGHT;
    return RESIZE_HANDLE_NONE;
}

static uint32_t cursor_for_resize_handle(int32_t handle)
{
    if (handle == RESIZE_HANDLE_TOP_LEFT || handle == RESIZE_HANDLE_BOTTOM_RIGHT)
        return SACX_MOUSE_CURSOR_SIZE1;
    if (handle == RESIZE_HANDLE_TOP_RIGHT || handle == RESIZE_HANDLE_BOTTOM_LEFT)
        return SACX_MOUSE_CURSOR_SIZE2;
    if (handle == RESIZE_HANDLE_ARROW_START || handle == RESIZE_HANDLE_ARROW_END)
        return SACX_MOUSE_CURSOR_CROSS;
    return SACX_MOUSE_CURSOR_ARROW;
}

static void zoom_about(int32_t screen_x, int32_t screen_y, uint32_t new_zoom)
{
    int32_t local_x = screen_x - g_view_screen_x;
    int32_t local_y = screen_y - g_view_screen_y;
    int64_t rel_x = local_x - g_image_x;
    int64_t rel_y = local_y - g_image_y;
    uint32_t old_w = g_scaled_w;
    uint32_t old_h = g_scaled_h;
    if (!old_w || !old_h)
        return;
    g_fit_mode = 0u;
    g_zoom_pct = clamp_zoom(new_zoom);
    g_scaled_w = (uint32_t)((uint64_t)g_doc.canvas_w * g_zoom_pct / 100u);
    g_scaled_h = (uint32_t)((uint64_t)g_doc.canvas_h * g_zoom_pct / 100u);
    g_image_x = local_x - (int32_t)(rel_x * g_scaled_w / old_w);
    g_image_y = local_y - (int32_t)(rel_y * g_scaled_h / old_h);
    clamp_pan();
    sync_image_layout();
}

static int ctrl_down(void)
{
    return g_api->input_key_down(SACX_KEY_LCTRL) || g_api->input_key_down(SACX_KEY_RCTRL);
}

static int shift_down(void)
{
    return g_api->input_key_down(SACX_KEY_LSHIFT) || g_api->input_key_down(SACX_KEY_RSHIFT);
}

static int space_down(void)
{
    return g_api->input_key_down(SACX_KEY_SPACE);
}

static int path_exists(const char *path)
{
    uint32_t file = 0u;
    if (!path || !path[0])
        return 0;
    if (g_api->file_open(path, SACX_FILE_READ, &file) != 0)
        return 0;
    g_api->file_close(file);
    return 1;
}

static void show_modal(uint32_t mode, const char *title, const char *body,
                       const char *primary, const char *secondary, const char *cancel)
{
    g_modal_mode = mode;
    editor_copy_text(g_modal_title_buf, sizeof(g_modal_title_buf), title);
    editor_copy_text(g_modal_body_buf, sizeof(g_modal_body_buf), body);
    (void)g_api->gfx_text_set(g_modal_title, g_modal_title_buf);
    (void)g_api->gfx_text_set(g_modal_body, g_modal_body_buf);
    set_button_text(&g_modal_buttons[0], primary);
    set_button_text(&g_modal_buttons[1], secondary);
    set_button_text(&g_modal_buttons[2], cancel);
    (void)g_api->gfx_obj_set_visible(g_modal_panel, 1u);
    (void)g_api->gfx_obj_set_visible(g_modal_title, 1u);
    (void)g_api->gfx_obj_set_visible(g_modal_body, 1u);
    for (uint32_t i = 0u; i < 3u; ++i)
        show_button(&g_modal_buttons[i], 1u);
    if (g_textbox_root && mode != MODAL_RESIZE)
        (void)g_api->gfx_obj_set_visible(g_textbox_root, 0u);
    (void)g_api->textbox_clear_focus();
    layout();
}

static void hide_modal(void)
{
    g_modal_mode = MODAL_NONE;
    (void)g_api->gfx_obj_set_visible(g_modal_panel, 0u);
    (void)g_api->gfx_obj_set_visible(g_modal_title, 0u);
    (void)g_api->gfx_obj_set_visible(g_modal_body, 0u);
    for (uint32_t i = 0u; i < 3u; ++i)
        show_button(&g_modal_buttons[i], 0u);
}

static void show_resize_modal(void)
{
    char dimensions[32];
    uint32_t width = g_doc.resize_w;
    uint32_t height = g_doc.resize_h;
    if (!width || !height)
    {
        width = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_h : g_doc.crop_w;
        height = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_w : g_doc.crop_h;
    }
    dimensions[0] = 0;
    editor_append_uint(dimensions, sizeof(dimensions), width);
    editor_append_text(dimensions, sizeof(dimensions), " x ");
    editor_append_uint(dimensions, sizeof(dimensions), height);
    show_modal(MODAL_RESIZE, "Resize output",
               "Enter Width x Height. This remains editable until export.",
               "Apply", "Native", "Cancel");
    (void)g_api->gfx_obj_set_visible(g_textbox_root, 1u);
    (void)g_api->textbox_set_enabled(g_textbox, 1u);
    (void)g_api->textbox_set_text(g_textbox, dimensions);
    (void)g_api->textbox_set_focus(g_textbox, 1u);
}

static int apply_resize_text(void)
{
    char text[64];
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (g_api->textbox_text_copy(g_textbox, text, sizeof(text)) != 0 ||
        parse_dimensions(text, &width, &height) != 0)
    {
        set_status("resize must be Width x Height, up to 8192");
        return -1;
    }
    g_doc.resize_w = width;
    g_doc.resize_h = height;
    document_commit();
    hide_modal();
    layout();
    refresh_properties();
    set_status("document resize updated");
    return 0;
}

static void open_dialog_callback(int accepted, const char *raw_path, const char *friendly_path, void *user)
{
    (void)user;
    if (!accepted || !raw_path || !raw_path[0])
    {
        g_dialog_purpose = DIALOG_NONE;
        set_status("dialog cancelled");
        return;
    }
    editor_copy_text(g_pending_raw, sizeof(g_pending_raw), raw_path);
    editor_copy_text(g_pending_friendly, sizeof(g_pending_friendly), friendly_path ? friendly_path : raw_path);
}

static void begin_dialog(uint32_t purpose)
{
    char dir[EDITOR_PATH_CAP];
    char name[128];
    const char *current = g_doc.project_raw[0] ? g_doc.project_raw : g_doc.source_raw;
    editor_split_path(current[0] ? current : "/", dir, sizeof(dir), name, sizeof(name));
    g_dialog_purpose = purpose;
    g_pending_raw[0] = 0;
    g_pending_friendly[0] = 0;
    if (purpose == DIALOG_OPEN)
    {
        if (g_api->dialog_open_file(dir, name[0] ? name : 0, open_dialog_callback, 0) != 0)
            g_dialog_purpose = DIALOG_NONE;
    }
    else
    {
        if (purpose == DIALOG_SAVE_PROJECT)
        {
            editor_copy_text(g_pending_name, sizeof(g_pending_name), name[0] ? name : "untitled.dimg");
            if (!editor_path_has_extension(g_pending_name, ".dimg"))
                editor_append_text(g_pending_name, sizeof(g_pending_name), ".dimg");
        }
        else
        {
            editor_copy_text(g_pending_name, sizeof(g_pending_name), "export.png");
        }
        if (g_api->dialog_save_file(dir, g_pending_name, open_dialog_callback, 0) != 0)
            g_dialog_purpose = DIALOG_NONE;
    }
}

static int load_path(const char *raw, const char *friendly)
{
    uint32_t image = 0u;
    int rc = -1;
    if (editor_path_has_extension(raw, ".dimg"))
        rc = project_load(raw, friendly);
    else if (g_api->img_load(raw, &image) == 0)
        rc = document_open_image(image, raw, friendly);
    if (rc == 0 && attach_preview() == 0)
    {
        refresh_properties();
        set_status("opened");
        return 0;
    }
    set_status("unable to open image or project");
    return -1;
}

static uint32_t export_format_for_path(const char *path)
{
    if (editor_path_has_extension(path, ".jpg") || editor_path_has_extension(path, ".jpeg"))
        return SACX_IMG_FORMAT_JPEG;
    if (editor_path_has_extension(path, ".bmp"))
        return SACX_IMG_FORMAT_BMP;
    return SACX_IMG_FORMAT_PNG;
}

static void execute_project_save(const char *raw, const char *friendly)
{
    if (project_save(raw, friendly) == 0)
    {
        set_status("project saved");
        update_title();
        if (g_pending_close_after_save)
        {
            g_pending_close_after_save = 0u;
            (void)g_api->window_close_accept(g_window);
        }
    }
    else
    {
        g_pending_close_after_save = 0u;
        set_status("project save failed");
    }
}

static void execute_export(const char *raw)
{
    if (project_export_begin_async(raw, export_format_for_path(raw), 90u) != 0)
    {
        set_status("export failed");
        return;
    }
    g_export_busy = 1u;
    g_export_progress = 0u;
    g_busy_saved_cursor = g_api->mouse_current_cursor();
    set_editor_controls_enabled(0u);
    hide_selection_overlay();
    set_status("Exporting... 0%");
}

static void update_export_job(void)
{
    sacx_mouse_state mouse;
    uint32_t progress = g_export_progress;
    int rc = project_export_step_async(16u, &progress);

    g_pending_action = 0u;
    if (g_api->mouse_get_state(&mouse) == 0 &&
        g_api->window_point_can_receive_input(g_window, mouse.x, mouse.y))
        (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_WAIT);
    else if (g_api->mouse_current_cursor() == SACX_MOUSE_CURSOR_WAIT)
        (void)g_api->mouse_set_cursor(g_busy_saved_cursor);

    if (progress != g_export_progress)
    {
        char status[64];
        status[0] = 0;
        editor_append_text(status, sizeof(status), "Exporting... ");
        editor_append_uint(status, sizeof(status), progress);
        editor_append_text(status, sizeof(status), "%");
        set_status(status);
        g_export_progress = progress;
    }
    if (rc > 0)
        return;

    g_export_busy = 0u;
    set_editor_controls_enabled(1u);
    (void)g_api->mouse_set_cursor(g_busy_saved_cursor);
    refresh_properties();
    set_status(rc == 0 ? "image exported" : "export failed");
}

static void process_dialog_result(void)
{
    if (!g_dialog_purpose || !g_pending_raw[0])
        return;
    uint32_t purpose = g_dialog_purpose;
    g_dialog_purpose = DIALOG_NONE;
    if (purpose == DIALOG_OPEN)
    {
        (void)load_path(g_pending_raw, g_pending_friendly);
    }
    else if (purpose == DIALOG_SAVE_PROJECT)
    {
        if (path_exists(g_pending_raw) &&
            (!g_doc.project_raw[0] || !text_equal(g_doc.project_raw, g_pending_raw)))
            show_modal(MODAL_OVERWRITE_PROJECT, "Replace file?",
                       "A file already exists at this path.", "Replace", "Cancel", "Cancel");
        else
            execute_project_save(g_pending_raw, g_pending_friendly);
    }
    else if (purpose == DIALOG_EXPORT)
    {
        if (path_exists(g_pending_raw))
            show_modal(MODAL_OVERWRITE_EXPORT, "Replace image?",
                       "An image already exists at this path.", "Replace", "Cancel", "Cancel");
        else
            execute_export(g_pending_raw);
    }
}

static void copy_flattened(void)
{
    uint32_t image = 0u;
    if (document_export_flattened(&image) == 0)
    {
        if (g_api->img_clipboard_set(image) == 0)
            set_status("copied flattened image");
        else
            set_status("image clipboard failed");
        g_api->img_destroy(image);
    }
}

static void paste_raster(void)
{
    uint32_t image = 0u;
    if (!SACX_API_HAS(g_api, img_clipboard_get) || g_api->img_clipboard_get(&image) != 0)
    {
        set_status("image clipboard is empty");
        return;
    }
    if (document_add_raster(image, 0, 0) != 0)
    {
        g_api->img_destroy(image);
        set_status("cannot add another raster layer");
        return;
    }
    document_commit();
    document_render_now();
    refresh_properties();
    set_status("pasted image layer");
}

static void apply_property_delta(uint32_t action)
{
    if (g_doc.selected_layer < 0 || (uint32_t)g_doc.selected_layer >= g_doc.layer_count)
        return;
    editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
    switch (action)
    {
    case ACTION_LAYER_UP:
        document_move_selected(1);
        document_render_now();
        refresh_properties();
        return;
    case ACTION_LAYER_DOWN:
        document_move_selected(-1);
        document_render_now();
        refresh_properties();
        return;
    case ACTION_LAYER_DELETE:
        document_delete_selected();
        document_render_now();
        refresh_properties();
        return;
    case ACTION_LAYER_VISIBLE: layer->visible = !layer->visible; break;
    case ACTION_LAYER_LOCK: layer->locked = !layer->locked; break;
    case ACTION_OPACITY_DOWN: layer->opacity = layer->opacity > 16u ? (uint8_t)(layer->opacity - 16u) : 0u; break;
    case ACTION_OPACITY_UP: layer->opacity = layer->opacity < 239u ? (uint8_t)(layer->opacity + 16u) : 255u; break;
    case ACTION_STROKE_DOWN:
        if (layer->type == EDITOR_LAYER_BLUR)
        {
            if (layer->effect_strength > 1u) --layer->effect_strength;
        }
        else if (layer->type == EDITOR_LAYER_PIXELATE)
        {
            if (layer->effect_strength > 2u) --layer->effect_strength;
        }
        else if (layer->stroke > 1u)
            --layer->stroke;
        break;
    case ACTION_STROKE_UP:
        if (layer->type == EDITOR_LAYER_BLUR)
        {
            if (layer->effect_strength < 32u) ++layer->effect_strength;
        }
        else if (layer->type == EDITOR_LAYER_PIXELATE)
        {
            if (layer->effect_strength < 64u) ++layer->effect_strength;
        }
        else if (layer->stroke < 64u)
            ++layer->stroke;
        break;
    case ACTION_BRIGHTNESS_DOWN: if (layer->brightness > -100) layer->brightness -= 10; break;
    case ACTION_BRIGHTNESS_UP: if (layer->brightness < 100) layer->brightness += 10; break;
    case ACTION_CONTRAST_DOWN: if (layer->contrast > -100) layer->contrast -= 10; break;
    case ACTION_CONTRAST_UP: if (layer->contrast < 100) layer->contrast += 10; break;
    case ACTION_SATURATION_DOWN: if (layer->saturation > -100) layer->saturation -= 10; break;
    case ACTION_SATURATION_UP: if (layer->saturation < 100) layer->saturation += 10; break;
    case ACTION_GRAYSCALE: layer->grayscale = !layer->grayscale; break;
    case ACTION_COLOR_RED: layer->color = 0xFFFF3B30u; break;
    case ACTION_COLOR_YELLOW: layer->color = 0xFFFFD60Au; break;
    case ACTION_COLOR_WHITE: layer->color = 0xFFFFFFFFu; break;
    case ACTION_COLOR_BLACK: layer->color = 0xFF000000u; break;
    case ACTION_COLOR_CYAN: layer->color = 0xFF32D6FFu; break;
    default: return;
    }
    document_commit();
    document_render_now();
    refresh_properties();
}

static void process_modal_action(uint32_t action)
{
    if (action == ACTION_MODAL_CANCEL ||
        ((g_modal_mode == MODAL_OVERWRITE_PROJECT || g_modal_mode == MODAL_OVERWRITE_EXPORT) &&
         action == ACTION_MODAL_SECONDARY))
    {
        uint32_t closed_mode = g_modal_mode;
        g_pending_close_after_save = 0u;
        hide_modal();
        if (closed_mode == MODAL_RESIZE)
        {
            layout();
            refresh_properties();
        }
        if (g_api->window_close_requested(g_window))
            (void)g_api->window_close_cancel(g_window);
        return;
    }
    if (g_modal_mode == MODAL_RESIZE)
    {
        if (action == ACTION_MODAL_PRIMARY)
            (void)apply_resize_text();
        else if (action == ACTION_MODAL_SECONDARY)
        {
            g_doc.resize_w = 0u;
            g_doc.resize_h = 0u;
            document_commit();
            hide_modal();
            layout();
            refresh_properties();
            set_status("native export size restored");
        }
        return;
    }
    if (g_modal_mode == MODAL_CLOSE)
    {
        if (action == ACTION_MODAL_PRIMARY)
        {
            hide_modal();
            g_pending_close_after_save = 1u;
            if (g_doc.project_raw[0])
                execute_project_save(g_doc.project_raw, g_doc.project_friendly);
            else
                begin_dialog(DIALOG_SAVE_PROJECT);
        }
        else if (action == ACTION_MODAL_SECONDARY)
        {
            hide_modal();
            (void)g_api->window_close_accept(g_window);
        }
    }
    else if (g_modal_mode == MODAL_OVERWRITE_PROJECT && action == ACTION_MODAL_PRIMARY)
    {
        hide_modal();
        execute_project_save(g_pending_raw, g_pending_friendly);
    }
    else if (g_modal_mode == MODAL_OVERWRITE_EXPORT && action == ACTION_MODAL_PRIMARY)
    {
        hide_modal();
        execute_export(g_pending_raw);
    }
}

static void process_action(uint32_t action)
{
    if (!action)
        return;
    if (g_modal_mode != MODAL_NONE)
    {
        process_modal_action(action);
        return;
    }
    if (action >= ACTION_TOOL_BASE && action < ACTION_TOOL_BASE + EDITOR_TOOL_COUNT)
    {
        g_tool = action - ACTION_TOOL_BASE;
        refresh_tool_styles();
        if (g_tool == EDITOR_TOOL_SELECT)
            set_status("Select: drag a layer or one of its handles");
        else if (g_tool == EDITOR_TOOL_CROP)
            set_status("Crop: drag the area to keep");
        else if (g_tool == EDITOR_TOOL_ADJUSTMENT)
            set_status("Adjust: click the image, then use the controls");
        else
            set_status("Drag on the image to create the layer");
        update_selection_overlay();
        return;
    }
    if (action >= ACTION_LAYER_ROW_BASE && action < ACTION_LAYER_ROW_BASE + 8u)
    {
        uint32_t index = g_layer_row_base + action - ACTION_LAYER_ROW_BASE;
        if (index < g_doc.layer_count)
        {
            g_doc.selected_layer = (int32_t)index;
            refresh_properties();
        }
        return;
    }
    switch (action)
    {
    case ACTION_OPEN: begin_dialog(DIALOG_OPEN); break;
    case ACTION_SAVE:
        if (g_doc.project_raw[0])
            execute_project_save(g_doc.project_raw, g_doc.project_friendly);
        else
            begin_dialog(DIALOG_SAVE_PROJECT);
        break;
    case ACTION_SAVE_AS: begin_dialog(DIALOG_SAVE_PROJECT); break;
    case ACTION_EXPORT: begin_dialog(DIALOG_EXPORT); break;
    case ACTION_COPY: copy_flattened(); break;
    case ACTION_UNDO:
        (void)document_undo();
        document_render_now();
        refresh_properties();
        break;
    case ACTION_REDO:
        (void)document_redo();
        document_render_now();
        refresh_properties();
        break;
    case ACTION_ZOOM_OUT:
        g_fit_mode = 0u; g_zoom_pct = clamp_zoom(g_zoom_pct * 80u / 100u); sync_image_layout(); break;
    case ACTION_ZOOM_IN:
        g_fit_mode = 0u; g_zoom_pct = clamp_zoom(g_zoom_pct * 125u / 100u); sync_image_layout(); break;
    case ACTION_FIT: g_fit_mode = 1u; sync_image_layout(); break;
    case ACTION_ROTATE:
        g_doc.rotation = (g_doc.rotation + 90u) % 360u; document_commit(); break;
    case ACTION_FLIP_X:
        g_doc.flip_x = !g_doc.flip_x; document_commit(); break;
    case ACTION_FLIP_Y:
        g_doc.flip_y = !g_doc.flip_y; document_commit(); break;
    case ACTION_RESIZE:
        show_resize_modal(); break;
    default:
        apply_property_delta(action);
        break;
    }
}

static void text_submit(uint32_t textbox, const char *text, void *user)
{
    (void)textbox;
    (void)user;
    if (g_modal_mode == MODAL_RESIZE)
    {
        (void)apply_resize_text();
        return;
    }
    if (g_doc.selected_layer >= 0 && (uint32_t)g_doc.selected_layer < g_doc.layer_count &&
        g_doc.layers[g_doc.selected_layer].type == EDITOR_LAYER_TEXT)
    {
        editor_copy_text(g_doc.layers[g_doc.selected_layer].text,
                         sizeof(g_doc.layers[g_doc.selected_layer].text), text);
        document_commit();
        document_render_now();
        refresh_properties();
    }
}

static void handle_shortcuts(void)
{
    if (g_modal_mode != MODAL_NONE || (g_api->dialog_active && g_api->dialog_active()))
        return;
    if (ctrl_down())
    {
        if (g_api->input_key_pressed(SACX_KEY_O)) g_pending_action = ACTION_OPEN;
        else if (g_api->input_key_pressed(SACX_KEY_S)) g_pending_action = shift_down() ? ACTION_SAVE_AS : ACTION_SAVE;
        else if (g_api->input_key_pressed(SACX_KEY_E)) g_pending_action = ACTION_EXPORT;
        else if (g_api->input_key_pressed(SACX_KEY_Z)) g_pending_action = ACTION_UNDO;
        else if (g_api->input_key_pressed(SACX_KEY_Y)) g_pending_action = ACTION_REDO;
        else if (g_api->input_key_pressed(SACX_KEY_C)) g_pending_action = ACTION_COPY;
        else if (g_api->input_key_pressed(SACX_KEY_V)) paste_raster();
        return;
    }
    if (g_api->input_key_pressed(SACX_KEY_DELETE))
        g_pending_action = ACTION_LAYER_DELETE;
    if (g_api->input_key_pressed(SACX_KEY_F))
        g_pending_action = ACTION_FIT;
    if (g_api->input_key_pressed(SACX_KEY_EQUAL) || g_api->input_key_pressed(SACX_KEY_KP_PLUS))
        g_pending_action = ACTION_ZOOM_IN;
    if (g_api->input_key_pressed(SACX_KEY_MINUS) || g_api->input_key_pressed(SACX_KEY_KP_MINUS))
        g_pending_action = ACTION_ZOOM_OUT;
}

static void begin_tool_drag(int32_t doc_x, int32_t doc_y)
{
    g_drag_start_doc_x = g_last_doc_x = doc_x;
    g_drag_start_doc_y = g_last_doc_y = doc_y;
    g_resizing = 0u;
    g_drag_changed = 0u;
    g_creating_layer = 0u;
    g_resize_handle = RESIZE_HANDLE_NONE;
    if (g_tool == EDITOR_TOOL_SELECT)
    {
        int32_t handle = selected_resize_handle_at(doc_x, doc_y);
        if (handle != RESIZE_HANDLE_NONE)
        {
            g_dragging = 1u;
            g_resizing = 1u;
            g_resize_handle = handle;
            g_drag_original_layer = g_doc.layers[g_doc.selected_layer];
            return;
        }
        g_doc.selected_layer = document_hit_test(doc_x, doc_y);
        refresh_properties();
        g_dragging = g_doc.selected_layer >= 0 &&
                     !g_doc.layers[g_doc.selected_layer].locked;
        if (g_dragging)
            g_drag_original_layer = g_doc.layers[g_doc.selected_layer];
        return;
    }
    if (g_tool == EDITOR_TOOL_CROP)
    {
        g_dragging = 1u;
        return;
    }
    if (g_tool == EDITOR_TOOL_ADJUSTMENT)
    {
        if (document_add_layer(EDITOR_LAYER_ADJUSTMENT) >= 0)
        {
            document_commit();
            document_render_now();
        }
        refresh_properties();
        return;
    }
    uint32_t layer_type = EDITOR_LAYER_RECT;
    switch (g_tool)
    {
    case EDITOR_TOOL_TEXT: layer_type = EDITOR_LAYER_TEXT; break;
    case EDITOR_TOOL_ARROW: layer_type = EDITOR_LAYER_ARROW; break;
    case EDITOR_TOOL_RECT: layer_type = EDITOR_LAYER_RECT; break;
    case EDITOR_TOOL_ELLIPSE: layer_type = EDITOR_LAYER_ELLIPSE; break;
    case EDITOR_TOOL_PEN: layer_type = EDITOR_LAYER_PEN; break;
    case EDITOR_TOOL_HIGHLIGHTER: layer_type = EDITOR_LAYER_HIGHLIGHTER; break;
    case EDITOR_TOOL_BLUR: layer_type = EDITOR_LAYER_BLUR; break;
    case EDITOR_TOOL_PIXELATE: layer_type = EDITOR_LAYER_PIXELATE; break;
    case EDITOR_TOOL_REDACT: layer_type = EDITOR_LAYER_REDACT; break;
    default: return;
    }
    int index = document_add_layer(layer_type);
    if (index < 0)
        return;
    editor_layer *layer = &g_doc.layers[index];
    g_creating_layer = 1u;
    layer->x0 = layer->x1 = doc_x;
    layer->y0 = layer->y1 = doc_y;
    if (layer_type == EDITOR_LAYER_TEXT)
    {
        layer->x1 = doc_x + 120;
        layer->y1 = doc_y + 32;
        document_commit();
        document_render_now();
        refresh_properties();
        (void)g_api->textbox_set_focus(g_textbox, 1u);
        g_creating_layer = 0u;
        return;
    }
    if (layer_type == EDITOR_LAYER_PEN || layer_type == EDITOR_LAYER_HIGHLIGHTER)
    {
        if (g_doc.point_count >= EDITOR_MAX_POINTS)
        {
            --g_doc.layer_count;
            g_doc.selected_layer = g_doc.layer_count ? (int32_t)g_doc.layer_count - 1 : -1;
            return;
        }
        layer->point_start = g_doc.point_count;
        layer->point_count = 1u;
        g_doc.points[g_doc.point_count++] = (editor_point){doc_x, doc_y};
    }
    g_dragging = 1u;
    refresh_properties();
}

static void update_tool_drag(int32_t doc_x, int32_t doc_y)
{
    if (!g_dragging)
        return;
    if (doc_x != g_last_doc_x || doc_y != g_last_doc_y)
        g_drag_changed = 1u;
    if (g_tool == EDITOR_TOOL_SELECT)
    {
        editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
        if (g_resizing)
        {
            if (g_resize_handle == RESIZE_HANDLE_ARROW_START)
            {
                layer->x0 = doc_x;
                layer->y0 = doc_y;
            }
            else if (g_resize_handle == RESIZE_HANDLE_ARROW_END)
            {
                layer->x1 = doc_x;
                layer->y1 = doc_y;
            }
            else
            {
                int32_t x0 = g_drag_original_layer.x0 < g_drag_original_layer.x1
                                 ? g_drag_original_layer.x0 : g_drag_original_layer.x1;
                int32_t y0 = g_drag_original_layer.y0 < g_drag_original_layer.y1
                                 ? g_drag_original_layer.y0 : g_drag_original_layer.y1;
                int32_t x1 = g_drag_original_layer.x0 > g_drag_original_layer.x1
                                 ? g_drag_original_layer.x0 : g_drag_original_layer.x1;
                int32_t y1 = g_drag_original_layer.y0 > g_drag_original_layer.y1
                                 ? g_drag_original_layer.y0 : g_drag_original_layer.y1;
                if (g_resize_handle == RESIZE_HANDLE_TOP_LEFT)
                {
                    layer->x0 = doc_x; layer->y0 = doc_y;
                    layer->x1 = x1; layer->y1 = y1;
                }
                else if (g_resize_handle == RESIZE_HANDLE_TOP_RIGHT)
                {
                    layer->x0 = x0; layer->y0 = doc_y;
                    layer->x1 = doc_x; layer->y1 = y1;
                }
                else if (g_resize_handle == RESIZE_HANDLE_BOTTOM_LEFT)
                {
                    layer->x0 = doc_x; layer->y0 = y0;
                    layer->x1 = x1; layer->y1 = doc_y;
                }
                else
                {
                    layer->x0 = x0; layer->y0 = y0;
                    layer->x1 = doc_x; layer->y1 = doc_y;
                }
            }
        }
        else
        {
            int32_t dx = doc_x - g_last_doc_x;
            int32_t dy = doc_y - g_last_doc_y;
            layer->x0 += dx; layer->x1 += dx;
            layer->y0 += dy; layer->y1 += dy;
            if (layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER)
                for (uint32_t p = 0u; p < layer->point_count; ++p)
                {
                    g_doc.points[layer->point_start + p].x += dx;
                    g_doc.points[layer->point_start + p].y += dy;
                }
        }
    }
    else if (g_tool == EDITOR_TOOL_CROP)
    {
        g_last_doc_x = doc_x;
        g_last_doc_y = doc_y;
        update_selection_overlay();
        return;
    }
    else if (g_doc.selected_layer >= 0)
    {
        editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
        layer->x1 = doc_x;
        layer->y1 = doc_y;
        if ((layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER) &&
            g_doc.point_count < EDITOR_MAX_POINTS)
        {
            editor_point *last = &g_doc.points[layer->point_start + layer->point_count - 1u];
            if (last->x != doc_x || last->y != doc_y)
            {
                g_doc.points[g_doc.point_count++] = (editor_point){doc_x, doc_y};
                ++layer->point_count;
            }
        }
    }
    g_last_doc_x = doc_x;
    g_last_doc_y = doc_y;
    if (g_doc.selected_layer >= 0 &&
        (uint32_t)g_doc.selected_layer < g_doc.layer_count &&
        layer_previews_live(g_doc.layers[g_doc.selected_layer].type))
    {
        document_request_render();
        document_render_now();
    }
    update_selection_overlay();
}

static void finish_tool_drag(int32_t doc_x, int32_t doc_y)
{
    if (!g_dragging)
        return;
    update_tool_drag(doc_x, doc_y);
    if (!g_drag_changed)
    {
        if (g_creating_layer && g_doc.layer_count &&
            g_doc.selected_layer == (int32_t)g_doc.layer_count - 1)
        {
            editor_layer *layer = &g_doc.layers[g_doc.selected_layer];
            if ((layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER) &&
                layer->point_start <= g_doc.point_count)
                g_doc.point_count = layer->point_start;
            --g_doc.layer_count;
            g_doc.selected_layer = g_doc.layer_count ? (int32_t)g_doc.layer_count - 1 : -1;
        }
        g_dragging = 0u;
        g_resizing = 0u;
        g_creating_layer = 0u;
        g_resize_handle = RESIZE_HANDLE_NONE;
        refresh_properties();
        return;
    }
    if (g_tool == EDITOR_TOOL_CROP)
    {
        int32_t x0 = g_drag_start_doc_x < doc_x ? g_drag_start_doc_x : doc_x;
        int32_t y0 = g_drag_start_doc_y < doc_y ? g_drag_start_doc_y : doc_y;
        int32_t x1 = g_drag_start_doc_x > doc_x ? g_drag_start_doc_x : doc_x;
        int32_t y1 = g_drag_start_doc_y > doc_y ? g_drag_start_doc_y : doc_y;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= (int32_t)g_doc.canvas_w) x1 = (int32_t)g_doc.canvas_w - 1;
        if (y1 >= (int32_t)g_doc.canvas_h) y1 = (int32_t)g_doc.canvas_h - 1;
        if (x1 > x0 && y1 > y0)
        {
            g_doc.crop_x = x0;
            g_doc.crop_y = y0;
            g_doc.crop_w = (uint32_t)(x1 - x0 + 1);
            g_doc.crop_h = (uint32_t)(y1 - y0 + 1);
            document_commit();
            document_render_now();
            char crop_status[64];
            crop_status[0] = 0;
            editor_append_text(crop_status, sizeof(crop_status), "crop ");
            editor_append_uint(crop_status, sizeof(crop_status), g_doc.crop_w);
            editor_append_text(crop_status, sizeof(crop_status), " x ");
            editor_append_uint(crop_status, sizeof(crop_status), g_doc.crop_h);
            set_status(crop_status);
        }
    }
    else
    {
        document_commit();
        document_render_now();
        if (g_doc.selected_layer >= 0 && (uint32_t)g_doc.selected_layer < g_doc.layer_count)
        {
            uint32_t type = g_doc.layers[g_doc.selected_layer].type;
            if (type == EDITOR_LAYER_BLUR)
                set_status("blur applied");
            else if (type == EDITOR_LAYER_PIXELATE)
                set_status("pixelate applied");
            else if (type == EDITOR_LAYER_REDACT)
                set_status("redaction applied");
        }
    }
    g_dragging = 0u;
    g_resizing = 0u;
    g_drag_changed = 0u;
    g_creating_layer = 0u;
    g_resize_handle = RESIZE_HANDLE_NONE;
    refresh_properties();
}

static void handle_mouse(void)
{
    sacx_mouse_state mouse;
    if (!g_doc.canvas_w || g_modal_mode != MODAL_NONE ||
        (g_api->dialog_active && g_api->dialog_active()) ||
        g_api->mouse_get_state(&mouse) != 0)
        return;
    uint8_t left = mouse.buttons & 0x01u;
    uint8_t middle = mouse.buttons & 0x04u;
    uint8_t old_left = g_previous_buttons & 0x01u;
    uint8_t old_middle = g_previous_buttons & 0x04u;
    int inside = point_in_canvas(mouse.x, mouse.y);

    if (mouse.wheel && inside)
        zoom_about(mouse.x, mouse.y, mouse.wheel > 0 ? g_zoom_pct * 125u / 100u : g_zoom_pct * 80u / 100u);

    if ((middle && !old_middle && inside) || (left && !old_left && inside && space_down()))
    {
        g_panning = 1u;
        g_last_mouse_x = mouse.x;
        g_last_mouse_y = mouse.y;
        (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_MOVE);
    }
    if (g_panning && (middle || (left && space_down())))
    {
        g_fit_mode = 0u;
        g_image_x += mouse.x - g_last_mouse_x;
        g_image_y += mouse.y - g_last_mouse_y;
        g_last_mouse_x = mouse.x;
        g_last_mouse_y = mouse.y;
        clamp_pan();
        sync_image_layout();
    }
    else if (g_panning)
    {
        g_panning = 0u;
        (void)g_api->mouse_set_cursor(SACX_MOUSE_CURSOR_ARROW);
    }

    if (!g_panning)
    {
        int32_t doc_x = 0;
        int32_t doc_y = 0;
        int have_doc = screen_to_document(mouse.x, mouse.y, &doc_x, &doc_y);
        if (left && !old_left && have_doc)
            begin_tool_drag(doc_x, doc_y);
        else if (left && g_dragging)
        {
            if (screen_to_document_clamped(mouse.x, mouse.y, &doc_x, &doc_y))
                update_tool_drag(doc_x, doc_y);
        }
        else if (!left && old_left && g_dragging)
        {
            if (!screen_to_document_clamped(mouse.x, mouse.y, &doc_x, &doc_y))
            {
                doc_x = g_last_doc_x;
                doc_y = g_last_doc_y;
            }
            finish_tool_drag(doc_x, doc_y);
        }
        if (inside && !left)
        {
            uint32_t cursor = SACX_MOUSE_CURSOR_CROSS;
            if (g_tool == EDITOR_TOOL_SELECT)
            {
                int32_t handle = have_doc ? selected_resize_handle_at(doc_x, doc_y) : RESIZE_HANDLE_NONE;
                cursor = cursor_for_resize_handle(handle);
                if (handle == RESIZE_HANDLE_NONE && have_doc &&
                    document_hit_test(doc_x, doc_y) == g_doc.selected_layer)
                    cursor = SACX_MOUSE_CURSOR_MOVE;
            }
            (void)g_api->mouse_set_cursor(cursor);
        }
    }
    g_previous_buttons = mouse.buttons;
}

static void handle_close_request(void)
{
    if (!g_api->window_close_requested(g_window) || g_modal_mode != MODAL_NONE)
        return;
    if (!g_doc.dirty)
    {
        (void)g_api->window_close_accept(g_window);
        return;
    }
    show_modal(MODAL_CLOSE, "Unsaved project",
               "Save the editable project before closing?", "Save", "Discard", "Cancel");
}

static int ui_fail(const char *stage)
{
    if (g_api && g_api->log)
        g_api->log(stage);
    return -1;
}

static int create_ui(void)
{
    sacx_window_style style = sacx_window_style_default();
    sacx_textbox_style textbox_style = sacx_textbox_style_default();
    const char *top_names[15] = {"Open", "Save", "Export", "Copy", "Undo", "Redo", "-", "+",
                                 "Fit", "Rotate", "Flip X", "Flip Y", "Resize", "Save As", "100%"};
    const uint32_t top_actions[15] = {ACTION_OPEN, ACTION_SAVE, ACTION_EXPORT, ACTION_COPY, ACTION_UNDO,
                                      ACTION_REDO, ACTION_ZOOM_OUT, ACTION_ZOOM_IN, ACTION_FIT,
                                      ACTION_ROTATE, ACTION_FLIP_X, ACTION_FLIP_Y, ACTION_RESIZE,
                                      ACTION_SAVE_AS, 0u};
    const char *tool_names[EDITOR_TOOL_COUNT] = {"Select", "Crop", "Text", "Arrow", "Rectangle", "Ellipse",
                                                 "Pen", "Highlighter", "Blur", "Pixelate", "Redact", "Adjust"};
    const char *property_names[17] = {"Move up", "Move down", "Show / hide", "Lock / unlock", "Delete", "Opacity -",
                                      "Opacity +", "Size -", "Size +", "Bright -", "Bright +",
                                      "Contrast -", "Contrast +", "Saturate -", "Saturate +", "Grayscale", "Fit"};
    const uint32_t property_actions[17] = {ACTION_LAYER_UP, ACTION_LAYER_DOWN, ACTION_LAYER_VISIBLE,
                                           ACTION_LAYER_LOCK, ACTION_LAYER_DELETE, ACTION_OPACITY_DOWN,
                                           ACTION_OPACITY_UP, ACTION_STROKE_DOWN, ACTION_STROKE_UP,
                                           ACTION_BRIGHTNESS_DOWN, ACTION_BRIGHTNESS_UP, ACTION_CONTRAST_DOWN,
                                           ACTION_CONTRAST_UP, ACTION_SATURATION_DOWN, ACTION_SATURATION_UP,
                                           ACTION_GRAYSCALE, ACTION_FIT};
    const char *color_names[5] = {"Red", "Yellow", "White", "Black", "Cyan"};
    const uint32_t color_actions[5] = {ACTION_COLOR_RED, ACTION_COLOR_YELLOW, ACTION_COLOR_WHITE,
                                       ACTION_COLOR_BLACK, ACTION_COLOR_CYAN};

    style.body_fill = rgb(12, 16, 20);
    style.body_outline = rgb(55, 91, 110);
    style.titlebar_fill = rgb(20, 30, 38);
    style.title_color = rgb(238, 245, 248);
    style.titlebar_height = 38u;
    style.title_scale = 2u;
    style.close_glyph_scale = 2u;
    style.fullscreen_glyph_scale = 2u;
    if (g_api->window_create_ex(70, 55, 1120, 720, 30, "Image Editor", &style, &g_window) != 0)
        return ui_fail("image editor UI: window allocation failed");
    if (g_api->window_root(g_window, &g_root) != 0)
        return ui_fail("image editor UI: window root registration failed");

    if (g_api->gfx_obj_add_rect(136, 84, 700, 580, 1, rgb(31, 34, 38), 1u, &g_canvas) != 0)
        return ui_fail("image editor UI: canvas allocation failed");
    (void)g_api->gfx_obj_set_parent(g_canvas, g_root);
    (void)g_api->gfx_obj_set_clip_to_parent(g_canvas, 1u);
    (void)g_api->gfx_obj_set_outline_rgb(g_canvas, 74, 94, 106);
    (void)g_api->gfx_obj_set_outline_width(g_canvas, 1u);

    {
        uint32_t *pixels = 0;
        uint32_t stride = 0u;
        if (g_api->img_create(512u, 512u, 0xFF22272Cu, &g_checker_image) != 0 ||
            g_api->img_pixels(g_checker_image, &pixels, &stride) != 0)
            return ui_fail("image editor UI: checker image allocation failed");
        for (uint32_t y = 0u; y < 512u; ++y)
            for (uint32_t x = 0u; x < 512u; ++x)
            {
                uint32_t light = ((x / 16u) + (y / 16u)) & 1u;
                pixels[(uint64_t)y * stride + x] = light ? 0xFF394047u : 0xFF252B30u;
            }
        (void)g_api->img_touch(g_checker_image);
        if (g_api->gfx_obj_add_image_from_img(g_checker_image, 0, 0, &g_checker_obj) != 0)
            return ui_fail("image editor UI: checker object allocation failed");
        (void)g_api->gfx_obj_set_parent(g_checker_obj, g_canvas);
        (void)g_api->gfx_obj_set_clip_to_parent(g_checker_obj, 1u);
        (void)g_api->gfx_obj_set_z(g_checker_obj, 0);
        (void)g_api->gfx_image_set_sample_mode(g_checker_obj, SACX_GFX_IMAGE_SAMPLE_NEAREST);
    }

    if (g_api->gfx_obj_add_rect(0, 0, 1, 1, 5, rgb(0, 0, 0), 0u, &g_selection) != 0)
        return ui_fail("image editor UI: selection allocation failed");
    (void)g_api->gfx_obj_set_parent(g_selection, g_canvas);
    (void)g_api->gfx_obj_set_alpha(g_selection, 0u);
    (void)g_api->gfx_obj_set_outline_rgb(g_selection, 50, 214, 255);
    (void)g_api->gfx_obj_set_outline_width(g_selection, 2u);
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (g_api->gfx_obj_add_rect(0, 0, 1u, 1u, 4, rgb(0, 0, 0), 1u, &g_crop_dim[i]) != 0)
            return ui_fail("image editor UI: crop overlay allocation failed");
        (void)g_api->gfx_obj_set_parent(g_crop_dim[i], g_canvas);
        (void)g_api->gfx_obj_set_alpha(g_crop_dim[i], 150u);
        (void)g_api->gfx_obj_set_visible(g_crop_dim[i], 0u);
    }
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (g_api->gfx_obj_add_rect(0, 0, 8u, 8u, 6, rgb(50, 214, 255), 1u,
                                    &g_selection_handles[i]) != 0)
            return ui_fail("image editor UI: selection handle allocation failed");
        (void)g_api->gfx_obj_set_parent(g_selection_handles[i], g_canvas);
        (void)g_api->gfx_obj_set_visible(g_selection_handles[i], 0u);
    }

    if (g_api->gfx_obj_add_text("Ready", 14, 680, 2, rgb(180, 202, 214), 255u, 1u, 0, 0,
                                SACX_TEXT_ALIGN_LEFT, 1u, &g_status_text) != 0 ||
        g_api->gfx_obj_add_text("Layers", 860, 84, 2, rgb(226, 237, 242), 255u, 2u, 0, 0,
                                SACX_TEXT_ALIGN_LEFT, 1u, &g_layer_title) != 0 ||
        g_api->gfx_obj_add_text("No layer selected", 860, 360, 2, rgb(169, 191, 202), 255u, 1u, 0, 2,
                                SACX_TEXT_ALIGN_LEFT, 1u, &g_property_text) != 0)
        return ui_fail("image editor UI: static text allocation failed");
    (void)g_api->gfx_obj_set_parent(g_status_text, g_root);
    (void)g_api->gfx_obj_set_parent(g_layer_title, g_root);
    (void)g_api->gfx_obj_set_parent(g_property_text, g_root);

    for (uint32_t i = 0u; i < 15u; ++i)
        if (create_button(&g_top[i], top_names[i], top_actions[i]) != 0)
            return ui_fail("image editor UI: top button allocation failed");
    g_zoom_text_obj = g_top[14].label;
    for (uint32_t i = 0u; i < EDITOR_TOOL_COUNT; ++i)
        if (create_button(&g_tools[i], tool_names[i], ACTION_TOOL_BASE + i) != 0)
            return ui_fail("image editor UI: tool button allocation failed");
    for (uint32_t i = 0u; i < 8u; ++i)
        if (create_button(&g_layer_rows[i], "", ACTION_LAYER_ROW_BASE + i) != 0)
            return ui_fail("image editor UI: layer row allocation failed");
    for (uint32_t i = 0u; i < 17u; ++i)
        if (create_button(&g_properties[i], property_names[i], property_actions[i]) != 0)
            return ui_fail("image editor UI: property button allocation failed");
    for (uint32_t i = 0u; i < 5u; ++i)
        if (create_button(&g_colors[i], color_names[i], color_actions[i]) != 0)
            return ui_fail("image editor UI: color button allocation failed");

    textbox_style.fill = rgb(18, 24, 29);
    textbox_style.focus_fill = rgb(27, 37, 44);
    textbox_style.outline = rgb(84, 112, 128);
    textbox_style.focus_outline = rgb(50, 214, 255);
    textbox_style.text_color = rgb(240, 246, 248);
    if (g_api->textbox_add_rect(860, 650, 230, 30, 5, &textbox_style, text_submit, 0, &g_textbox) != 0 ||
        g_api->textbox_root(g_textbox, &g_textbox_root) != 0)
        return ui_fail("image editor UI: textbox allocation failed");
    (void)g_api->gfx_obj_set_parent(g_textbox_root, g_root);
    (void)g_api->textbox_set_enabled(g_textbox, 0u);

    if (g_api->gfx_obj_add_rect(300, 230, 430, 190, 20, rgb(25, 32, 38), 0u, &g_modal_panel) != 0 ||
        g_api->gfx_obj_add_text("", 320, 250, 21, rgb(245, 248, 250), 255u, 2u, 0, 0,
                                SACX_TEXT_ALIGN_LEFT, 0u, &g_modal_title) != 0 ||
        g_api->gfx_obj_add_text("", 320, 290, 21, rgb(187, 205, 214), 255u, 1u, 0, 2,
                                SACX_TEXT_ALIGN_LEFT, 0u, &g_modal_body) != 0)
        return ui_fail("image editor UI: modal allocation failed");
    (void)g_api->gfx_obj_set_parent(g_modal_panel, g_root);
    (void)g_api->gfx_obj_set_parent(g_modal_title, g_root);
    (void)g_api->gfx_obj_set_parent(g_modal_body, g_root);
    (void)g_api->gfx_obj_set_outline_rgb(g_modal_panel, 82, 121, 141);
    (void)g_api->gfx_obj_set_outline_width(g_modal_panel, 2u);
    if (create_button(&g_modal_buttons[0], "Save", ACTION_MODAL_PRIMARY) != 0 ||
        create_button(&g_modal_buttons[1], "Discard", ACTION_MODAL_SECONDARY) != 0 ||
        create_button(&g_modal_buttons[2], "Cancel", ACTION_MODAL_CANCEL) != 0)
        return ui_fail("image editor UI: modal button allocation failed");
    for (uint32_t i = 0u; i < 3u; ++i)
        (void)g_api->gfx_obj_set_z(g_modal_buttons[i].root, 22);
    hide_modal();
    (void)g_api->window_set_close_deferred(g_window, 1u);
    (void)g_api->window_set_visible(g_window, 1u);
    refresh_tool_styles();
    refresh_properties();
    return 0;
}

static int load_startup_document(void)
{
    uint32_t startup_image = SACX_API_HAS(g_api, app_arg_image) ? g_api->app_arg_image() : 0u;
    const char *raw = sacx_app_arg_raw_path(g_api);
    const char *friendly = sacx_app_arg_friendly_path(g_api);
    if (startup_image)
    {
        if (document_open_image(startup_image, "", "Screenshot") == 0)
            return attach_preview();
        return -1;
    }
    if (raw && raw[0])
        return load_path(raw, friendly && friendly[0] ? friendly : raw);
    begin_dialog(DIALOG_OPEN);
    return 0;
}

static int editor_update(const sacx_api *api)
{
    g_api = api;
    if (!api || !g_window)
        return -1;
    layout();
    process_dialog_result();
    if (g_export_busy)
    {
        update_export_job();
        update_title();
        return api->app_yield();
    }
    handle_close_request();
    if (!api->window_visible(g_window))
        return api->app_exit(0, "image editor closed");
    if (!api->window_focused(g_window))
        return api->app_yield();

    handle_shortcuts();
    if (g_pending_action)
    {
        uint32_t action = g_pending_action;
        g_pending_action = 0u;
        process_action(action);
    }
    if (g_export_busy)
        return api->app_yield();
    handle_mouse();
    if (g_doc.render_pending)
        (void)document_render_step(32u);
    update_selection_overlay();
    update_title();
    return api->app_yield();
}

extern "C" int sacx_main(const sacx_api *api)
{
    if (!api || api->abi_version != SACX_API_ABI_VERSION ||
        !SACX_API_HAS(api, img_create) || !SACX_API_HAS(api, img_pixels) ||
        !SACX_API_HAS(api, dialog_save_file) || !SACX_API_HAS(api, window_set_close_deferred))
        return -1;
    g_api = api;
    document_reset();
    (void)SACX_APP_NO_CONSOLE(api);
    if (create_ui() != 0)
    {
        if (api->log)
            api->log("image editor: UI initialization failed");
        return api->app_exit(-1, "image editor UI failed");
    }
    if (load_startup_document() != 0)
    {
        if (api->log)
            api->log("image editor: startup document failed");
        set_status("unable to load startup image");
    }
    if (api->app_set_update(editor_update) != 0)
    {
        if (api->log)
            api->log("image editor: update callback registration failed");
        return api->app_exit(-1, "image editor update failed");
    }
    return 0;
}
