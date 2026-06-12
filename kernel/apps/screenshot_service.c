#include "apps/screenshot_service.h"
#include "apps/sacx_runtime.h"
#include "kwrappers/colors.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/ktext.h"
#include "memory/pmem.h"
#include "system/kimage_clipboard.h"

#define SCREENSHOT_APP_RAW "0:/OS/System/Programs/Image Viewer/image_viewer.sacx"
#define SCREENSHOT_APP_FRIENDLY "/OS/System/Programs/Image Viewer/image_viewer.sacx"

typedef struct screenshot_state
{
    const kfont *font;
    kimg frozen;
    kgfx_obj_handle background;
    kgfx_obj_handle dim[4];
    kgfx_obj_handle border[4];
    kgfx_obj_handle size_text;
    kmouse_cursor old_cursor;
    int32_t start_x;
    int32_t start_y;
    int32_t current_x;
    int32_t current_y;
    uint8_t active;
    uint8_t dragging;
    uint8_t previous_buttons;
    char size_label[48];
} screenshot_state;

static screenshot_state G;

static void image_free(kimg *image)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;
    if (!image)
        return;
    if (image->px && image->w && image->h)
    {
        bytes = (uint64_t)image->w * image->h * 4ull;
        pages = (bytes + 4095ull) >> 12;
        if (pages)
            pmem_free_pages(image->px, pages);
    }
    *image = (kimg){0};
}

static int capture_desktop(kimg *out)
{
    const kfb *fb = kgfx_info();
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!out || !fb || !fb->width || !fb->height)
        return -1;
    *out = (kimg){0};
    bytes = (uint64_t)fb->width * fb->height * 4ull;
    pages = (bytes + 4095ull) >> 12;
    out->px = (uint32_t *)pmem_alloc_pages(pages);
    if (!out->px)
        return -1;
    out->w = fb->width;
    out->h = fb->height;

    kmouse_set_visible(0u);
    kgfx_render_all(black);
    if (kgfx_capture_argb(out->px, out->w, 0, 0, out->w, out->h) != 0)
    {
        kmouse_set_visible(1u);
        image_free(out);
        return -1;
    }
    kmouse_set_visible(1u);
    return 0;
}

static void append_uint(char *dst, uint32_t cap, uint32_t value)
{
    char reverse[12];
    uint32_t len = 0u;
    uint32_t out = 0u;

    while (dst[out] && out + 1u < cap)
        ++out;
    if (!value)
    {
        if (out + 1u < cap)
            dst[out++] = '0';
    }
    else
    {
        while (value && len < sizeof(reverse))
        {
            reverse[len++] = (char)('0' + value % 10u);
            value /= 10u;
        }
        while (len && out + 1u < cap)
            dst[out++] = reverse[--len];
    }
    dst[out] = 0;
}

static void selection_rect(int32_t *x, int32_t *y, uint32_t *w, uint32_t *h)
{
    int32_t x0 = G.start_x < G.current_x ? G.start_x : G.current_x;
    int32_t y0 = G.start_y < G.current_y ? G.start_y : G.current_y;
    int32_t x1 = G.start_x > G.current_x ? G.start_x : G.current_x;
    int32_t y1 = G.start_y > G.current_y ? G.start_y : G.current_y;
    *x = x0;
    *y = y0;
    *w = (uint32_t)(x1 - x0 + 1);
    *h = (uint32_t)(y1 - y0 + 1);
}

static void set_rect(kgfx_obj_handle handle, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    kgfx_obj *obj = kgfx_obj_ref(handle);
    if (!obj || obj->kind != KGFX_OBJ_RECT)
        return;
    obj->u.rect.x = x;
    obj->u.rect.y = y;
    obj->u.rect.w = w ? w : 1u;
    obj->u.rect.h = h ? h : 1u;
}

static void update_overlay(void)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 1u;
    uint32_t h = 1u;
    uint32_t screen_w = G.frozen.w;
    uint32_t screen_h = G.frozen.h;
    kgfx_obj *text = 0;

    selection_rect(&x, &y, &w, &h);
    set_rect(G.dim[0], 0, 0, screen_w, y > 0 ? (uint32_t)y : 1u);
    set_rect(G.dim[1], 0, y + (int32_t)h, screen_w,
             y + (int32_t)h < (int32_t)screen_h ? screen_h - (uint32_t)(y + (int32_t)h) : 1u);
    set_rect(G.dim[2], 0, y, x > 0 ? (uint32_t)x : 1u, h);
    set_rect(G.dim[3], x + (int32_t)w, y,
             x + (int32_t)w < (int32_t)screen_w ? screen_w - (uint32_t)(x + (int32_t)w) : 1u, h);

    set_rect(G.border[0], x, y, w, 2u);
    set_rect(G.border[1], x, y + (int32_t)h - 2, w, 2u);
    set_rect(G.border[2], x, y, 2u, h);
    set_rect(G.border[3], x + (int32_t)w - 2, y, 2u, h);

    G.size_label[0] = 0;
    append_uint(G.size_label, sizeof(G.size_label), w);
    {
        uint32_t len = 0u;
        while (G.size_label[len] && len + 3u < sizeof(G.size_label))
            ++len;
        G.size_label[len++] = ' ';
        G.size_label[len++] = 'x';
        G.size_label[len++] = ' ';
        G.size_label[len] = 0;
    }
    append_uint(G.size_label, sizeof(G.size_label), h);
    text = kgfx_obj_ref(G.size_text);
    if (text && text->kind == KGFX_OBJ_TEXT)
    {
        text->u.text.text = G.size_label;
        text->u.text.x = x + 8;
        text->u.text.y = y > 28 ? y - 24 : y + 8;
    }
}

static void destroy_overlay(void)
{
    if (G.background.idx >= 0)
        (void)kgfx_obj_destroy(G.background);
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (G.dim[i].idx >= 0)
            (void)kgfx_obj_destroy(G.dim[i]);
        if (G.border[i].idx >= 0)
            (void)kgfx_obj_destroy(G.border[i]);
    }
    if (G.size_text.idx >= 0)
        (void)kgfx_obj_destroy(G.size_text);
    G.background.idx = -1;
    G.size_text.idx = -1;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        G.dim[i].idx = -1;
        G.border[i].idx = -1;
    }
}

static void finish_region(int accepted)
{
    kimg selected = {0};
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0u;
    uint32_t h = 0u;

    if (accepted)
    {
        selection_rect(&x, &y, &w, &h);
        if (w >= 2u && h >= 2u)
        {
            uint64_t bytes = (uint64_t)w * h * 4ull;
            uint64_t pages = (bytes + 4095ull) >> 12;
            selected.px = (uint32_t *)pmem_alloc_pages(pages);
            if (selected.px)
            {
                selected.w = w;
                selected.h = h;
                for (uint32_t iy = 0u; iy < h; ++iy)
                {
                    const uint32_t *src = G.frozen.px + (uint64_t)(y + (int32_t)iy) * G.frozen.w + (uint32_t)x;
                    uint32_t *dst = selected.px + (uint64_t)iy * w;
                    for (uint32_t ix = 0u; ix < w; ++ix)
                        dst[ix] = src[ix];
                }
                (void)kimage_clipboard_set(&selected);
                (void)sacx_runtime_launch_image(SCREENSHOT_APP_RAW, SCREENSHOT_APP_FRIENDLY,
                                                &selected, 0, 0);
            }
        }
    }

    destroy_overlay();
    image_free(&G.frozen);
    image_free(&selected);
    G.active = 0u;
    G.dragging = 0u;
    G.previous_buttons = 0u;
    (void)kmouse_set_cursor(G.old_cursor);
}

static int begin_region(void)
{
    const kfb *fb = kgfx_info();
    if (!fb || capture_desktop(&G.frozen) != 0)
        return -1;

    G.background = kgfx_obj_add_image(G.frozen.px, G.frozen.w, G.frozen.h, 0, 0, G.frozen.w);
    if (G.background.idx < 0)
    {
        image_free(&G.frozen);
        return -1;
    }
    kgfx_obj_ref(G.background)->z = 900;

    for (uint32_t i = 0u; i < 4u; ++i)
    {
        G.dim[i] = kgfx_obj_add_rect(0, 0, fb->width, fb->height, 901, black, 1u);
        G.border[i] = kgfx_obj_add_rect(0, 0, 2u, 2u, 903, cyan, 1u);
        if (G.dim[i].idx >= 0)
            kgfx_obj_ref(G.dim[i])->alpha = 150u;
    }
    if (G.font)
        G.size_text = kgfx_obj_add_text(G.font, G.size_label, 8, 8, 904,
                                        white, 255u, 2u, 0, 0, KTEXT_ALIGN_LEFT, 1u);

    G.old_cursor = kmouse_current_cursor();
    (void)kmouse_set_cursor(KMOUSE_CURSOR_CROSS);
    G.start_x = G.current_x = kmouse_x();
    G.start_y = G.current_y = kmouse_y();
    G.previous_buttons = kmouse_buttons();
    G.dragging = 0u;
    G.active = 1u;
    update_overlay();
    return 0;
}

void screenshot_service_init(const kfont *font)
{
    G = (screenshot_state){0};
    G.font = font;
    G.background.idx = -1;
    G.size_text.idx = -1;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        G.dim[i].idx = -1;
        G.border[i].idx = -1;
    }
}

int screenshot_service_update(void)
{
    uint8_t shift = (kinput_key_down(KEY_LSHIFT) || kinput_key_down(KEY_RSHIFT)) ? 1u : 0u;

    if (!G.active)
    {
        if (!kinput_key_pressed(KEY_PRINTSCREEN))
            return 0;
        if (shift)
        {
            (void)begin_region();
            return G.active ? 1 : 0;
        }
        else
        {
            kimg capture = {0};
            if (capture_desktop(&capture) == 0)
            {
                (void)kimage_clipboard_set(&capture);
                (void)sacx_runtime_launch_image(SCREENSHOT_APP_RAW, SCREENSHOT_APP_FRIENDLY,
                                                &capture, 0, 0);
            }
            image_free(&capture);
            return 0;
        }
    }

    if (kinput_key_pressed(KEY_ESCAPE))
    {
        finish_region(0);
        return 0;
    }

    {
        uint8_t buttons = kmouse_buttons();
        uint8_t left = buttons & 0x01u;
        uint8_t old_left = G.previous_buttons & 0x01u;

        if (left && !old_left)
        {
            G.start_x = G.current_x = kmouse_x();
            G.start_y = G.current_y = kmouse_y();
            G.dragging = 1u;
        }
        if (left && G.dragging)
        {
            G.current_x = kmouse_x();
            G.current_y = kmouse_y();
            update_overlay();
        }
        if (!left && old_left && G.dragging)
        {
            finish_region(1);
            return 0;
        }
        G.previous_buttons = buttons;
    }

    return 1;
}
