#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"

#ifndef KWINDOW_MAX
#define KWINDOW_MAX 64
#endif

#define KWINDOW_MOUSE_LEFT 0x01u
#define KWINDOW_MIN_CLIENT_HEIGHT 20u
#define KWINDOW_RESIZE_BORDER 6
#define KWINDOW_RESIZE_INSIDE_EXTRA 3
#define KWINDOW_RESIZE_LEFT 0x01u
#define KWINDOW_RESIZE_RIGHT 0x02u
#define KWINDOW_RESIZE_TOP 0x04u
#define KWINDOW_RESIZE_BOTTOM 0x08u

typedef struct
{
    int32_t x0, y0, x1, y1;
    uint8_t enabled;
} kwindow_clip_rect;

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t z;
    kwindow_clip_rect clip;
    uint8_t valid;
} kwindow_resolved_rect;

typedef struct
{
    uint8_t used;
    uint8_t visible;
    uint8_t dragging;
    uint8_t resizing;
    uint8_t resize_edges;
    uint8_t fullscreen;
    kgfx_obj_handle root;
    kgfx_obj_handle titlebar;
    kgfx_obj_handle title_text;
    kgfx_obj_handle close_text;
    kgfx_obj_handle fullscreen_text;
    kbutton_handle close_button;
    kbutton_handle fullscreen_button;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_w;
    uint32_t restore_h;
    kwindow_style style;
} kwindow_slot;

static kwindow_slot G_windows[KWINDOW_MAX];
static uint8_t G_prev_buttons = 0;
static int32_t G_prev_mouse_x = 0;
static int32_t G_prev_mouse_y = 0;
static uint8_t G_prev_mouse_valid = 0;

static inline int32_t kwindow_max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline int32_t kwindow_min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static uint32_t kwindow_min_width_for_style(const kwindow_style *style)
{
    uint32_t button_span = 0;

    if (!style)
        return 80u;

    button_span = style->close_button_width + style->fullscreen_button_width + 30u;
    return (button_span > 80u) ? button_span : 80u;
}

static uint32_t kwindow_min_height_for_style(const kwindow_style *style)
{
    if (!style)
        return 48u;
    return style->titlebar_height + KWINDOW_MIN_CLIENT_HEIGHT;
}

static void kwindow_layout_controls(kwindow_slot *slot)
{
    kgfx_obj *root = 0;
    kgfx_obj *titlebar = 0;
    kgfx_obj *title_text = 0;
    kgfx_obj *close_root = 0;
    kgfx_obj *fullscreen_root = 0;
    kgfx_obj *close_text = 0;
    kgfx_obj *fullscreen_text = 0;
    int32_t close_x = 0;
    int32_t close_y = 0;
    int32_t full_x = 0;
    int32_t full_y = 0;
    int32_t close_text_y = 0;
    int32_t full_text_y = 0;

    if (!slot)
        return;

    root = kgfx_obj_ref(slot->root);
    titlebar = kgfx_obj_ref(slot->titlebar);
    if (!root || root->kind != KGFX_OBJ_RECT || !titlebar || titlebar->kind != KGFX_OBJ_RECT)
        return;

    titlebar->u.rect.w = root->u.rect.w;
    titlebar->u.rect.h = slot->style.titlebar_height;

    title_text = kgfx_obj_ref(slot->title_text);
    if (title_text && title_text->kind == KGFX_OBJ_TEXT && title_text->u.text.font)
    {
        uint32_t title_h = ktext_scale_mul_px(title_text->u.text.font->h, slot->style.title_scale);
        int32_t title_y = (int32_t)((slot->style.titlebar_height > title_h)
                                        ? (slot->style.titlebar_height - title_h) / 2u
                                        : 0u);
        title_text->u.text.y = title_y;
    }

    close_x = (int32_t)root->u.rect.w - (int32_t)slot->style.close_button_width - 6;
    close_y = (int32_t)((slot->style.titlebar_height > slot->style.close_button_height)
                            ? (slot->style.titlebar_height - slot->style.close_button_height) / 2u
                            : 0u);
    full_x = close_x - (int32_t)slot->style.fullscreen_button_width - 6;
    full_y = (int32_t)((slot->style.titlebar_height > slot->style.fullscreen_button_height)
                           ? (slot->style.titlebar_height - slot->style.fullscreen_button_height) / 2u
                           : 0u);

    close_root = kgfx_obj_ref(kbutton_root(slot->close_button));
    if (close_root && close_root->kind == KGFX_OBJ_RECT)
    {
        close_root->u.rect.x = close_x;
        close_root->u.rect.y = close_y;
        close_root->u.rect.w = slot->style.close_button_width;
        close_root->u.rect.h = slot->style.close_button_height;
    }

    fullscreen_root = kgfx_obj_ref(kbutton_root(slot->fullscreen_button));
    if (fullscreen_root && fullscreen_root->kind == KGFX_OBJ_RECT)
    {
        fullscreen_root->u.rect.x = full_x;
        fullscreen_root->u.rect.y = full_y;
        fullscreen_root->u.rect.w = slot->style.fullscreen_button_width;
        fullscreen_root->u.rect.h = slot->style.fullscreen_button_height;
    }

    close_text = kgfx_obj_ref(slot->close_text);
    if (close_text && close_text->kind == KGFX_OBJ_TEXT)
    {
        if (close_text->u.text.font)
        {
            uint32_t close_h = ktext_scale_mul_px(close_text->u.text.font->h, slot->style.close_glyph_scale);
            close_text_y = (int32_t)((slot->style.close_button_height > close_h)
                                         ? (slot->style.close_button_height - close_h) / 2u
                                         : 0u);
            close_text_y -= 3;
            close_text->u.text.y = close_text_y;
        }
        close_text->u.text.x = (int32_t)slot->style.close_button_width / 2;
    }

    fullscreen_text = kgfx_obj_ref(slot->fullscreen_text);
    if (fullscreen_text && fullscreen_text->kind == KGFX_OBJ_TEXT)
    {
        if (fullscreen_text->u.text.font)
        {
            uint32_t full_h = ktext_scale_mul_px(fullscreen_text->u.text.font->h, slot->style.fullscreen_glyph_scale);
            full_text_y = (int32_t)((slot->style.fullscreen_button_height > full_h)
                                        ? (slot->style.fullscreen_button_height - full_h) / 2u
                                        : 0u);
            full_text_y -= 3;
            fullscreen_text->u.text.y = full_text_y;
        }
        fullscreen_text->u.text.x = (int32_t)slot->style.fullscreen_button_width / 2;
    }
}

static void kwindow_toggle_fullscreen(kwindow_slot *slot)
{
    const kfb *fb = 0;
    kgfx_obj *root = 0;

    if (!slot)
        return;

    fb = kgfx_info();
    root = kgfx_obj_ref(slot->root);
    if (!fb || !root || root->kind != KGFX_OBJ_RECT)
        return;

    if (!slot->fullscreen)
    {
        slot->restore_x = root->u.rect.x;
        slot->restore_y = root->u.rect.y;
        slot->restore_w = root->u.rect.w;
        slot->restore_h = root->u.rect.h;
        root->u.rect.x = 0;
        root->u.rect.y = 0;
        root->u.rect.w = fb->width;
        root->u.rect.h = fb->height;
        slot->fullscreen = 1;
    }
    else
    {
        root->u.rect.x = slot->restore_x;
        root->u.rect.y = slot->restore_y;
        root->u.rect.w = slot->restore_w;
        root->u.rect.h = slot->restore_h;
        if (root->u.rect.w < kwindow_min_width_for_style(&slot->style))
            root->u.rect.w = kwindow_min_width_for_style(&slot->style);
        if (root->u.rect.h < kwindow_min_height_for_style(&slot->style))
            root->u.rect.h = kwindow_min_height_for_style(&slot->style);
        slot->fullscreen = 0;
    }

    slot->dragging = 0;
    slot->resizing = 0;
    slot->resize_edges = 0;
    kwindow_layout_controls(slot);
}

static inline int kwindow_clip_intersect(kwindow_clip_rect *dst, const kwindow_clip_rect *other)
{
    if (!dst || !other || !dst->enabled || !other->enabled)
        return 1;

    dst->x0 = kwindow_max_i32(dst->x0, other->x0);
    dst->y0 = kwindow_max_i32(dst->y0, other->y0);
    dst->x1 = kwindow_min_i32(dst->x1, other->x1);
    dst->y1 = kwindow_min_i32(dst->y1, other->y1);
    return dst->x0 < dst->x1 && dst->y0 < dst->y1;
}

static void kwindow_obj_local_origin(const kgfx_obj *o, int32_t *x, int32_t *y)
{
    if (!o || !x || !y)
        return;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        *x = o->u.rect.x;
        *y = o->u.rect.y;
        break;
    case KGFX_OBJ_CIRCLE:
        *x = o->u.circle.cx;
        *y = o->u.circle.cy;
        break;
    case KGFX_OBJ_TEXT:
        *x = o->u.text.x;
        *y = o->u.text.y;
        break;
    case KGFX_OBJ_IMAGE:
        *x = o->u.image.x;
        *y = o->u.image.y;
        break;
    default:
        *x = 0;
        *y = 0;
        break;
    }
}

static int kwindow_parent_clip_bounds(const kgfx_obj *o, int32_t world_x, int32_t world_y, kwindow_clip_rect *clip)
{
    if (!o || !clip)
        return 0;

    clip->enabled = 1;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        clip->x0 = world_x;
        clip->y0 = world_y;
        clip->x1 = world_x + (int32_t)o->u.rect.w;
        clip->y1 = world_y + (int32_t)o->u.rect.h;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    case KGFX_OBJ_CIRCLE:
        clip->x0 = world_x - (int32_t)o->u.circle.r;
        clip->y0 = world_y - (int32_t)o->u.circle.r;
        clip->x1 = world_x + (int32_t)o->u.circle.r + 1;
        clip->y1 = world_y + (int32_t)o->u.circle.r + 1;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    case KGFX_OBJ_IMAGE:
        clip->x0 = world_x;
        clip->y0 = world_y;
        clip->x1 = world_x + (int32_t)o->u.image.w;
        clip->y1 = world_y + (int32_t)o->u.image.h;
        return clip->x0 < clip->x1 && clip->y0 < clip->y1;
    default:
        return 0;
    }
}

static int kwindow_resolve_obj(kgfx_obj_handle h, kwindow_resolved_rect *out, uint32_t depth)
{
    const kfb *fb = 0;
    kgfx_obj *o = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    kwindow_resolved_rect parent = {0};
    kwindow_clip_rect parent_bounds = {0};
    kgfx_obj_handle parent_handle;

    if (!out || depth > 32u)
        return 0;

    o = kgfx_obj_ref(h);
    fb = kgfx_info();
    if (!o || !fb || !o->visible)
        return 0;

    kwindow_obj_local_origin(o, &local_x, &local_y);

    out->x = local_x;
    out->y = local_y;
    out->z = o->z;
    out->clip.enabled = 1;
    out->clip.x0 = 0;
    out->clip.y0 = 0;
    out->clip.x1 = (int32_t)fb->width;
    out->clip.y1 = (int32_t)fb->height;
    out->valid = 1;

    if (o->parent_idx < 0)
        return 1;

    parent_handle.idx = (int)o->parent_idx;
    if (!kgfx_obj_ref(parent_handle) || !kgfx_obj_ref(parent_handle)->visible)
        return 0;
    if (!kwindow_resolve_obj(parent_handle, &parent, depth + 1u))
        return 0;

    out->x += parent.x;
    out->y += parent.y;
    out->z += parent.z;
    out->clip = parent.clip;

    if (o->clip_to_parent && kwindow_parent_clip_bounds(kgfx_obj_ref(parent_handle), parent.x, parent.y, &parent_bounds))
    {
        if (!kwindow_clip_intersect(&out->clip, &parent_bounds))
            return 0;
    }

    return 1;
}

static int kwindow_resolve_rect_bounds(kgfx_obj_handle h, kwindow_resolved_rect *out)
{
    kgfx_obj *o = 0;

    if (!out)
        return 0;

    o = kgfx_obj_ref(h);
    if (!o || o->kind != KGFX_OBJ_RECT)
        return 0;

    if (!kwindow_resolve_obj(h, out, 0))
        return 0;

    out->clip.x0 = kwindow_max_i32(out->clip.x0, out->x);
    out->clip.y0 = kwindow_max_i32(out->clip.y0, out->y);
    out->clip.x1 = kwindow_min_i32(out->clip.x1, out->x + (int32_t)o->u.rect.w);
    out->clip.y1 = kwindow_min_i32(out->clip.y1, out->y + (int32_t)o->u.rect.h);

    return out->clip.x0 < out->clip.x1 && out->clip.y0 < out->clip.y1;
}

static int kwindow_point_in_bounds(int32_t x, int32_t y, const kwindow_resolved_rect *r)
{
    if (!r || !r->valid)
        return 0;
    return x >= r->clip.x0 && y >= r->clip.y0 && x < r->clip.x1 && y < r->clip.y1;
}

static uint8_t kwindow_hit_resize_edges(const kwindow_resolved_rect *r, int32_t x, int32_t y, uint16_t outline_width)
{
    uint8_t edges = 0;
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t outside = 0;
    int32_t inside = 0;

    if (!r || !r->valid)
        return 0;
    
    x0 = r->clip.x0;
    y0 = r->clip.y0;
    x1 = r->clip.x1 - 1;
    y1 = r->clip.y1 - 1;
    outside = (int32_t)outline_width;
    inside = KWINDOW_RESIZE_BORDER + KWINDOW_RESIZE_INSIDE_EXTRA;

    if (x < (x0 - outside) || x > (x1 + outside) ||
        y < (y0 - outside) || y > (y1 + outside))
        return 0;

    if (x >= (x0 - outside) && x < (x0 + inside))
        edges |= KWINDOW_RESIZE_LEFT;
    if (x <= (x1 + outside) && x > (x1 - inside))
        edges |= KWINDOW_RESIZE_RIGHT;
    if (y >= (y0 - outside) && y < (y0 + inside))
        edges |= KWINDOW_RESIZE_TOP;
    if (y <= (y1 + outside) && y > (y1 - inside))
        edges |= KWINDOW_RESIZE_BOTTOM;

    return edges;
}

static kmouse_cursor kwindow_cursor_for_resize_edges(uint8_t edges)
{
    uint8_t has_h = (edges & (KWINDOW_RESIZE_LEFT | KWINDOW_RESIZE_RIGHT)) ? 1u : 0u;
    uint8_t has_v = (edges & (KWINDOW_RESIZE_TOP | KWINDOW_RESIZE_BOTTOM)) ? 1u : 0u;

    if (has_h && has_v)
    {
        if (((edges & KWINDOW_RESIZE_LEFT) && (edges & KWINDOW_RESIZE_TOP)) ||
            ((edges & KWINDOW_RESIZE_RIGHT) && (edges & KWINDOW_RESIZE_BOTTOM)))
            return KMOUSE_CURSOR_SIZE2;
        return KMOUSE_CURSOR_SIZE1;
    }

    if (has_h)
        return KMOUSE_CURSOR_SIZE3;
    if (has_v)
        return KMOUSE_CURSOR_SIZE4;
    return KMOUSE_CURSOR_ARROW;
}

static void kwindow_apply_resize_delta(kwindow_slot *slot, int32_t dx, int32_t dy)
{
    kgfx_obj *root = 0;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    int32_t min_w = 0;
    int32_t min_h = 0;
    int32_t new_w = 0;
    int32_t new_h = 0;

    if (!slot || !slot->resize_edges || slot->fullscreen)
        return;

    root = kgfx_obj_ref(slot->root);
    if (!root || root->kind != KGFX_OBJ_RECT)
        return;

    left = root->u.rect.x;
    top = root->u.rect.y;
    right = left + (int32_t)root->u.rect.w;
    bottom = top + (int32_t)root->u.rect.h;

    if (slot->resize_edges & KWINDOW_RESIZE_LEFT)
        left += dx;
    if (slot->resize_edges & KWINDOW_RESIZE_RIGHT)
        right += dx;
    if (slot->resize_edges & KWINDOW_RESIZE_TOP)
        top += dy;
    if (slot->resize_edges & KWINDOW_RESIZE_BOTTOM)
        bottom += dy;

    min_w = (int32_t)kwindow_min_width_for_style(&slot->style);
    min_h = (int32_t)kwindow_min_height_for_style(&slot->style);
    new_w = right - left;
    new_h = bottom - top;

    if (new_w < min_w)
    {
        if (slot->resize_edges & KWINDOW_RESIZE_LEFT)
            left = right - min_w;
        else
            right = left + min_w;
    }

    if (new_h < min_h)
    {
        if (slot->resize_edges & KWINDOW_RESIZE_TOP)
            top = bottom - min_h;
        else
            bottom = top + min_h;
    }

    if (right <= left || bottom <= top)
        return;

    root->u.rect.x = left;
    root->u.rect.y = top;
    root->u.rect.w = (uint32_t)(right - left);
    root->u.rect.h = (uint32_t)(bottom - top);
    kwindow_layout_controls(slot);
}

static int kwindow_raise_to_front(int idx)
{
    kgfx_obj *target = 0;
    int32_t max_z = 0;
    int max_z_valid = 0;
    int has_same_z_other = 0;

    if (idx < 0 || idx >= KWINDOW_MAX)
        return 0;
    if (!G_windows[idx].used || !G_windows[idx].visible)
        return 0;

    target = kgfx_obj_ref(G_windows[idx].root);
    if (!target || target->kind != KGFX_OBJ_RECT || !target->visible)
        return 0;

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        kgfx_obj *root = 0;

        if (!G_windows[i].used || !G_windows[i].visible)
            continue;

        root = kgfx_obj_ref(G_windows[i].root);
        if (!root || root->kind != KGFX_OBJ_RECT || !root->visible)
            continue;

        if (!max_z_valid || root->z > max_z)
        {
            max_z = root->z;
            max_z_valid = 1;
        }

        if ((int)i != idx && root->z == target->z)
            has_same_z_other = 1;
    }

    if (!max_z_valid)
        return 0;

    if (target->z < max_z || has_same_z_other)
        target->z = max_z + 1;

    return 1;
}

static void kwindow_close_click(kbutton_handle button, void *user)
{
    kwindow_slot *slot = (kwindow_slot *)user;

    (void)button;

    if (!slot)
        return;

    slot->visible = 0;
    slot->dragging = 0;
    slot->resizing = 0;
    slot->resize_edges = 0;
    if (kgfx_obj_ref(slot->root))
        kgfx_obj_ref(slot->root)->visible = 0;
    kbutton_set_enabled(slot->close_button, 0);
    kbutton_set_enabled(slot->fullscreen_button, 0);
}

static void kwindow_fullscreen_click(kbutton_handle button, void *user)
{
    kwindow_slot *slot = (kwindow_slot *)user;

    (void)button;

    if (!slot || !slot->used || !slot->visible)
        return;

    kwindow_toggle_fullscreen(slot);
    kwindow_raise_to_front((int)(slot - G_windows));
}

void kwindow_init(void)
{
    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        G_windows[i] = (kwindow_slot){0};
    G_prev_buttons = 0;
    G_prev_mouse_x = 0;
    G_prev_mouse_y = 0;
    G_prev_mouse_valid = 0;
}

kwindow_handle kwindow_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                              int32_t z, const kfont *font, const char *title,
                              const kwindow_style *style)
{
    kwindow_handle handle = {-1};
    kwindow_style resolved_style = kwindow_style_default();
    const char *resolved_title = title ? title : "Window";
    const char *owned_title = 0;

    if (style)
        resolved_style = *style;

    if (w < kwindow_min_width_for_style(&resolved_style))
        w = kwindow_min_width_for_style(&resolved_style);
    if (h < kwindow_min_height_for_style(&resolved_style))
        h = kwindow_min_height_for_style(&resolved_style);
    owned_title = kgfx_pmem_strdup(resolved_title);
    if (!owned_title)
        owned_title = resolved_title;

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        kgfx_obj *root_obj = 0;
        kgfx_obj *titlebar_obj = 0;
        kgfx_obj *close_text_obj = 0;
        kgfx_obj *fullscreen_text_obj = 0;
        int32_t title_y = 0;
        int32_t close_x = 0;
        int32_t close_y = 0;
        int32_t full_x = 0;
        int32_t full_y = 0;
        int32_t close_text_y = 0;
        int32_t full_text_y = 0;

        if (G_windows[i].used)
            continue;

        G_windows[i] = (kwindow_slot){0};
        G_windows[i].root.idx = -1;
        G_windows[i].titlebar.idx = -1;
        G_windows[i].title_text.idx = -1;
        G_windows[i].close_text.idx = -1;
        G_windows[i].fullscreen_text.idx = -1;
        G_windows[i].close_button.idx = -1;
        G_windows[i].fullscreen_button.idx = -1;

        G_windows[i].root = kgfx_obj_add_rect(x, y, w, h, z, resolved_style.body_fill, 1);
        if (G_windows[i].root.idx < 0)
            return handle;

        G_windows[i].titlebar = kgfx_obj_add_rect(0, 0, w, resolved_style.titlebar_height, 1,
                                                  resolved_style.titlebar_fill, 1);
        if (G_windows[i].titlebar.idx < 0)
        {
            kgfx_obj_destroy(G_windows[i].root);
            G_windows[i] = (kwindow_slot){0};
            return handle;
        }

        kgfx_obj_set_parent(G_windows[i].titlebar, G_windows[i].root);

        root_obj = kgfx_obj_ref(G_windows[i].root);
        titlebar_obj = kgfx_obj_ref(G_windows[i].titlebar);
        if (root_obj)
        {
            root_obj->outline = resolved_style.body_outline;
            root_obj->outline_width = resolved_style.body_outline_width;
            root_obj->outline_alpha = 255;
        }
        if (titlebar_obj)
        {
            titlebar_obj->outline_width = 0;
            titlebar_obj->fill = resolved_style.titlebar_fill;
        }

        G_windows[i].title_text.idx = -1;
        if (font && resolved_title)
        {
            uint32_t title_h = ktext_scale_mul_px(font->h, resolved_style.title_scale);
            title_y = (int32_t)((resolved_style.titlebar_height > title_h)
                                    ? (resolved_style.titlebar_height - title_h) / 2u
                                    : 0u);
            G_windows[i].title_text = kgfx_obj_add_text(font, owned_title,
                                                        10, title_y, 1,
                                                        resolved_style.title_color, 255,
                                                        resolved_style.title_scale,
                                                        0, 0, KTEXT_ALIGN_LEFT, 1);
            if (G_windows[i].title_text.idx >= 0)
                kgfx_obj_set_parent(G_windows[i].title_text, G_windows[i].titlebar);
        }

        close_x = (int32_t)w - (int32_t)resolved_style.close_button_width - 6;
        close_y = (int32_t)((resolved_style.titlebar_height > resolved_style.close_button_height)
                                ? (resolved_style.titlebar_height - resolved_style.close_button_height) / 2u
                                : 0u);
        G_windows[i].close_button = kbutton_add_rect(close_x, close_y,
                                                     resolved_style.close_button_width,
                                                     resolved_style.close_button_height,
                                                     2, &resolved_style.close_button_style,
                                                     kwindow_close_click, &G_windows[i]);
        if (G_windows[i].close_button.idx < 0)
        {
            if (G_windows[i].title_text.idx >= 0)
                kgfx_obj_destroy(G_windows[i].title_text);
            kgfx_obj_destroy(G_windows[i].titlebar);
            kgfx_obj_destroy(G_windows[i].root);
            G_windows[i] = (kwindow_slot){0};
            return handle;
        }

        full_x = close_x - (int32_t)resolved_style.fullscreen_button_width - 6;
        full_y = (int32_t)((resolved_style.titlebar_height > resolved_style.fullscreen_button_height)
                               ? (resolved_style.titlebar_height - resolved_style.fullscreen_button_height) / 2u
                               : 0u);
        G_windows[i].fullscreen_button = kbutton_add_rect(full_x, full_y,
                                                          resolved_style.fullscreen_button_width,
                                                          resolved_style.fullscreen_button_height,
                                                          2, &resolved_style.fullscreen_button_style,
                                                          kwindow_fullscreen_click, &G_windows[i]);
        if (G_windows[i].fullscreen_button.idx < 0)
        {
            if (G_windows[i].title_text.idx >= 0)
                kgfx_obj_destroy(G_windows[i].title_text);
            kbutton_destroy(G_windows[i].close_button);
            kgfx_obj_destroy(G_windows[i].titlebar);
            kgfx_obj_destroy(G_windows[i].root);
            G_windows[i] = (kwindow_slot){0};
            return handle;
        }

        kgfx_obj_set_parent(kbutton_root(G_windows[i].close_button), G_windows[i].titlebar);
        kgfx_obj_set_parent(kbutton_root(G_windows[i].fullscreen_button), G_windows[i].titlebar);

        G_windows[i].close_text.idx = -1;
        if (font)
        {
            uint32_t close_h = ktext_scale_mul_px(font->h, resolved_style.close_glyph_scale);
            close_text_y = (int32_t)((resolved_style.close_button_height > close_h)
                                         ? (resolved_style.close_button_height - close_h) / 2u
                                         : 0u);
            close_text_y -= 3;

            G_windows[i].close_text = kgfx_obj_add_text(font, "X",
                                                        (int32_t)resolved_style.close_button_width / 2,
                                                        close_text_y, 1,
                                                        resolved_style.close_text_color, 255,
                                                        resolved_style.close_glyph_scale,
                                                        0, 0, KTEXT_ALIGN_CENTER, 1);

            if (G_windows[i].close_text.idx >= 0)
            {
                kgfx_obj_set_parent(G_windows[i].close_text, kbutton_root(G_windows[i].close_button));
                close_text_obj = kgfx_obj_ref(G_windows[i].close_text);
                if (close_text_obj)
                    close_text_obj->outline_width = 0;
            }

            uint32_t full_h = ktext_scale_mul_px(font->h, resolved_style.fullscreen_glyph_scale);
            full_text_y = (int32_t)((resolved_style.fullscreen_button_height > full_h)
                                        ? (resolved_style.fullscreen_button_height - full_h) / 2u
                                        : 0u);
            full_text_y -= 3;
            G_windows[i].fullscreen_text = kgfx_obj_add_text(font, "O",
                                                             (int32_t)resolved_style.fullscreen_button_width / 2,
                                                             full_text_y, 1,
                                                             resolved_style.fullscreen_text_color, 255,
                                                             resolved_style.fullscreen_glyph_scale,
                                                             0, 0, KTEXT_ALIGN_CENTER, 1);
            if (G_windows[i].fullscreen_text.idx >= 0)
            {
                kgfx_obj_set_parent(G_windows[i].fullscreen_text, kbutton_root(G_windows[i].fullscreen_button));
                fullscreen_text_obj = kgfx_obj_ref(G_windows[i].fullscreen_text);
                if (fullscreen_text_obj)
                    fullscreen_text_obj->outline_width = 0;
            }
        }

        G_windows[i].used = 1;
        G_windows[i].visible = 1;
        G_windows[i].dragging = 0;
        G_windows[i].resizing = 0;
        G_windows[i].resize_edges = 0;
        G_windows[i].fullscreen = 0;
        G_windows[i].restore_x = x;
        G_windows[i].restore_y = y;
        G_windows[i].restore_w = w;
        G_windows[i].restore_h = h;
        G_windows[i].style = resolved_style;
        kwindow_layout_controls(&G_windows[i]);

        handle.idx = (int)i;
        return handle;
    }

    return handle;
}

int kwindow_destroy(kwindow_handle h)
{
    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return -1;

    if (G_windows[h.idx].fullscreen_text.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].fullscreen_text);
    if (G_windows[h.idx].close_text.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].close_text);
    if (G_windows[h.idx].title_text.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].title_text);
    kbutton_destroy(G_windows[h.idx].fullscreen_button);
    kbutton_destroy(G_windows[h.idx].close_button);
    if (G_windows[h.idx].titlebar.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].titlebar);
    if (G_windows[h.idx].root.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].root);

    G_windows[h.idx] = (kwindow_slot){0};
    return 0;
}

void kwindow_update_all(void)
{
    kmouse_state mouse = {0};
    int32_t cursor_dx = 0;
    int32_t cursor_dy = 0;
    kmouse_cursor cursor_shape = KMOUSE_CURSOR_ARROW;
    uint8_t hover_resize_edges = 0;
    int32_t hover_resize_z = 0;
    uint8_t left_down = 0;
    uint8_t left_pressed = 0;
    int resizing_idx = -1;
    int dragging_idx = -1;
    int focus_idx = -1;
    int32_t focus_z = 0;
    int drag_candidate_idx = -1;
    int32_t drag_candidate_z = 0;
    int resize_candidate_idx = -1;
    int32_t resize_candidate_z = 0;
    uint8_t resize_candidate_edges = 0;

    kmouse_get_state(&mouse);
    if (G_prev_mouse_valid)
    {
        cursor_dx = mouse.x - G_prev_mouse_x;
        cursor_dy = mouse.y - G_prev_mouse_y;
    }
    else
    {
        G_prev_mouse_valid = 1;
    }

    left_down = (mouse.buttons & KWINDOW_MOUSE_LEFT) ? 1u : 0u;
    left_pressed = left_down && !(G_prev_buttons & KWINDOW_MOUSE_LEFT);

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        if (!G_windows[i].used)
            continue;
        if (G_windows[i].resizing)
        {
            resizing_idx = (int)i;
            break;
        }
    }

    if (resizing_idx < 0)
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        {
            if (!G_windows[i].used)
                continue;
            if (G_windows[i].dragging)
            {
                dragging_idx = (int)i;
                break;
            }
        }
    }

    if (resizing_idx >= 0)
    {
        kwindow_slot *slot = &G_windows[resizing_idx];
        kgfx_obj *root = kgfx_obj_ref(slot->root);

        if (!left_down || !root || !slot->visible)
        {
            slot->resizing = 0;
            slot->resize_edges = 0;
        }
        else if (root->kind == KGFX_OBJ_RECT)
        {
            kwindow_apply_resize_delta(slot, cursor_dx, cursor_dy);
        }
    }
    else if (dragging_idx >= 0)
    {
        kwindow_slot *slot = &G_windows[dragging_idx];
        kgfx_obj *root = kgfx_obj_ref(slot->root);

        if (!left_down || !root || !slot->visible || slot->fullscreen)
        {
            slot->dragging = 0;
        }
        else if (root->kind == KGFX_OBJ_RECT)
        {
            root->u.rect.x += cursor_dx;
            root->u.rect.y += cursor_dy;
        }
    }
    else if (left_pressed)
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        {
            kwindow_resolved_rect root_bounds = {0};
            kwindow_resolved_rect titlebar_bounds = {0};
            kwindow_resolved_rect close_bounds = {0};
            kwindow_resolved_rect fullscreen_bounds = {0};
            int over_window = 0;
            int over_titlebar = 0;
            int over_close = 0;
            int over_fullscreen = 0;
            uint8_t resize_edges = 0;

            if (!G_windows[i].used || !G_windows[i].visible)
                continue;

            if (kwindow_resolve_rect_bounds(G_windows[i].root, &root_bounds))
            {
                over_window = kwindow_point_in_bounds(mouse.x, mouse.y, &root_bounds);
                if (over_window && (focus_idx < 0 || root_bounds.z >= focus_z))
                {
                    focus_idx = (int)i;
                    focus_z = root_bounds.z;
                }
            }
            else
            {
                continue;
            }

            if (kwindow_resolve_rect_bounds(kbutton_root(G_windows[i].close_button), &close_bounds))
                over_close = kwindow_point_in_bounds(mouse.x, mouse.y, &close_bounds);
            if (kwindow_resolve_rect_bounds(kbutton_root(G_windows[i].fullscreen_button), &fullscreen_bounds))
                over_fullscreen = kwindow_point_in_bounds(mouse.x, mouse.y, &fullscreen_bounds);

            if (!G_windows[i].fullscreen && !over_close && !over_fullscreen)
            {
                resize_edges = kwindow_hit_resize_edges(&root_bounds, mouse.x, mouse.y,
                                                        G_windows[i].style.body_outline_width);
                if (resize_edges && (resize_candidate_idx < 0 || root_bounds.z >= resize_candidate_z))
                {
                    resize_candidate_idx = (int)i;
                    resize_candidate_z = root_bounds.z;
                    resize_candidate_edges = resize_edges;
                }
            }

            if (!kwindow_resolve_rect_bounds(G_windows[i].titlebar, &titlebar_bounds))
                continue;

            over_titlebar = kwindow_point_in_bounds(mouse.x, mouse.y, &titlebar_bounds);
            if (!over_titlebar || G_windows[i].fullscreen || over_close || over_fullscreen)
                continue;

            if (drag_candidate_idx < 0 || titlebar_bounds.z >= drag_candidate_z)
            {
                drag_candidate_idx = (int)i;
                drag_candidate_z = titlebar_bounds.z;
            }
        }

        if (focus_idx >= 0)
            kwindow_raise_to_front(focus_idx);

        if (resize_candidate_idx >= 0)
        {
            G_windows[resize_candidate_idx].dragging = 0;
            G_windows[resize_candidate_idx].resizing = 1;
            G_windows[resize_candidate_idx].resize_edges = resize_candidate_edges;
        }
        else if (drag_candidate_idx >= 0)
        {
            G_windows[drag_candidate_idx].resizing = 0;
            G_windows[drag_candidate_idx].resize_edges = 0;
            G_windows[drag_candidate_idx].dragging = 1;
        }
    }

    if (resizing_idx >= 0 && G_windows[resizing_idx].resizing && G_windows[resizing_idx].resize_edges)
    {
        cursor_shape = kwindow_cursor_for_resize_edges(G_windows[resizing_idx].resize_edges);
    }
    else
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        {
            kwindow_resolved_rect root_bounds = {0};
            kwindow_resolved_rect close_bounds = {0};
            kwindow_resolved_rect fullscreen_bounds = {0};
            uint8_t edges = 0;
            int over_close = 0;
            int over_fullscreen = 0;

            if (!G_windows[i].used || !G_windows[i].visible || G_windows[i].fullscreen)
                continue;
            if (!kwindow_resolve_rect_bounds(G_windows[i].root, &root_bounds))
                continue;
            if (!kwindow_point_in_bounds(mouse.x, mouse.y, &root_bounds))
                continue;

            if (kwindow_resolve_rect_bounds(kbutton_root(G_windows[i].close_button), &close_bounds))
                over_close = kwindow_point_in_bounds(mouse.x, mouse.y, &close_bounds);
            if (kwindow_resolve_rect_bounds(kbutton_root(G_windows[i].fullscreen_button), &fullscreen_bounds))
                over_fullscreen = kwindow_point_in_bounds(mouse.x, mouse.y, &fullscreen_bounds);
            if (over_close || over_fullscreen)
                continue;

            edges = kwindow_hit_resize_edges(&root_bounds, mouse.x, mouse.y,
                                             G_windows[i].style.body_outline_width);
            if (!edges)
                continue;

            if (!hover_resize_edges || root_bounds.z >= hover_resize_z)
            {
                hover_resize_edges = edges;
                hover_resize_z = root_bounds.z;
            }
        }

        if (hover_resize_edges)
            cursor_shape = kwindow_cursor_for_resize_edges(hover_resize_edges);
    }

    (void)kmouse_set_cursor(cursor_shape);

    if (!left_down)
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        {
            G_windows[i].dragging = 0;
            G_windows[i].resizing = 0;
            G_windows[i].resize_edges = 0;
        }
    }

    G_prev_buttons = mouse.buttons;
    G_prev_mouse_x = mouse.x;
    G_prev_mouse_y = mouse.y;
}

void kwindow_set_visible(kwindow_handle h, uint8_t visible)
{
    kgfx_obj *root = 0;

    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return;

    G_windows[h.idx].visible = visible ? 1u : 0u;
    G_windows[h.idx].dragging = 0;
    G_windows[h.idx].resizing = 0;
    G_windows[h.idx].resize_edges = 0;

    root = kgfx_obj_ref(G_windows[h.idx].root);
    if (root)
        root->visible = visible ? 1u : 0u;

    kbutton_set_enabled(G_windows[h.idx].close_button, visible ? 1u : 0u);
    kbutton_set_enabled(G_windows[h.idx].fullscreen_button, visible ? 1u : 0u);
}

int kwindow_visible(kwindow_handle h)
{
    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return 0;
    return G_windows[h.idx].visible != 0;
}

kgfx_obj_handle kwindow_root(kwindow_handle h)
{
    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return (kgfx_obj_handle){-1};
    return G_windows[h.idx].root;
}
