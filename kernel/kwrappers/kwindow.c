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
#define KWINDOW_TITLE_CAP 128u
#define KWINDOW_DESIGN_W 1920u
#define KWINDOW_DESIGN_H 1080u
#define KWINDOW_SCALE_ONE 1024u

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
    uint8_t close_deferred;
    uint8_t close_requested;
    int16_t modal_parent_idx;
    int16_t modal_child_idx;
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
    char title[KWINDOW_TITLE_CAP];
    kwindow_style style;
} kwindow_slot;

static kwindow_slot G_windows[KWINDOW_MAX];
static uint16_t G_window_generation[KWINDOW_MAX];
static uint8_t G_prev_buttons = 0;
static int32_t G_prev_mouse_x = 0;
static int32_t G_prev_mouse_y = 0;
static uint8_t G_prev_mouse_valid = 0;
static uint32_t G_work_area_bottom_inset = 0;

static int kwindow_handle_valid(kwindow_handle h)
{
    if (h.idx < 0 || h.idx >= KWINDOW_MAX)
        return 0;
    if (!G_windows[h.idx].used)
        return 0;
    return h.generation == G_window_generation[h.idx];
}

static uint32_t kwindow_scale_apply_u32(uint32_t px, uint32_t scale_fp)
{
    uint64_t value = (uint64_t)px * (uint64_t)scale_fp;
    uint32_t scaled = (uint32_t)((value + (KWINDOW_SCALE_ONE / 2u)) / KWINDOW_SCALE_ONE);
    if (px && !scaled)
        scaled = 1u;
    return scaled;
}

static int32_t kwindow_scale_apply_i32(int32_t px, uint32_t scale_fp)
{
    int negative = px < 0;
    uint32_t magnitude = negative ? (uint32_t)(-px) : (uint32_t)px;
    uint32_t scaled = kwindow_scale_apply_u32(magnitude, scale_fp);
    return negative ? -(int32_t)scaled : (int32_t)scaled;
}

uint32_t kwindow_ui_scale_fp(void)
{
    const kfb *fb = kgfx_info();
    uint64_t sx;
    uint64_t sy;
    uint32_t scale;

    if (!fb || !fb->width || !fb->height)
        return KWINDOW_SCALE_ONE;

    sx = ((uint64_t)fb->width * KWINDOW_SCALE_ONE) / KWINDOW_DESIGN_W;
    sy = ((uint64_t)fb->height * KWINDOW_SCALE_ONE) / KWINDOW_DESIGN_H;
    scale = (uint32_t)(sx < sy ? sx : sy);
    if (scale < 256u)
        scale = 256u;
    if (scale > 2048u)
        scale = 2048u;
    return scale;
}

uint32_t kwindow_ui_scale_u32(uint32_t px)
{
    return kwindow_scale_apply_u32(px, kwindow_ui_scale_fp());
}

int32_t kwindow_ui_scale_i32(int32_t px)
{
    return kwindow_scale_apply_i32(px, kwindow_ui_scale_fp());
}

static uint32_t kwindow_scale_text_scale(uint32_t scale, uint32_t scale_fp)
{
    uint64_t base_fp;
    uint64_t scaled_fp;

    if (scale == 0u)
        scale = 1u;

    if (scale & KTEXT_SCALE_FP_FLAG)
        base_fp = (uint64_t)(scale & KTEXT_SCALE_FP_MASK);
    else
        base_fp = ((10ull + (uint64_t)(scale - 1u)) * KTEXT_SCALE_FP_ONE + 5u) / 10u;

    scaled_fp = (base_fp * (uint64_t)scale_fp + (KWINDOW_SCALE_ONE / 2u)) / KWINDOW_SCALE_ONE;
    if (scaled_fp < 256u)
        scaled_fp = 256u;
    if (scaled_fp > KTEXT_SCALE_FP_MASK)
        scaled_fp = KTEXT_SCALE_FP_MASK;
    return ktext_scale_from_fp((uint32_t)scaled_fp);
}

uint32_t kwindow_ui_text_scale(uint32_t base_scale)
{
    return kwindow_scale_text_scale(base_scale ? base_scale : 1u, kwindow_ui_scale_fp());
}

static void kwindow_scale_button_style(kbutton_style *style, uint32_t scale_fp)
{
    if (!style)
        return;
    if (style->outline_width == 0u)
        return;
    style->outline_width = (uint16_t)kwindow_scale_apply_u32(style->outline_width, scale_fp);
    if (style->outline_width == 0u)
        style->outline_width = 1u;
}

static void kwindow_scale_style(kwindow_style *style, uint32_t scale_fp)
{
    uint32_t min_title = 24u;

    if (!style || scale_fp == KWINDOW_SCALE_ONE)
        return;

    style->body_outline_width = (uint16_t)kwindow_scale_apply_u32(style->body_outline_width, scale_fp);
    if (style->body_outline_width == 0u)
        style->body_outline_width = 1u;
    style->titlebar_height = kwindow_scale_apply_u32(style->titlebar_height, scale_fp);
    if (style->titlebar_height < min_title)
        style->titlebar_height = min_title;
    style->close_button_width = kwindow_scale_apply_u32(style->close_button_width, scale_fp);
    style->close_button_height = kwindow_scale_apply_u32(style->close_button_height, scale_fp);
    style->fullscreen_button_width = kwindow_scale_apply_u32(style->fullscreen_button_width, scale_fp);
    style->fullscreen_button_height = kwindow_scale_apply_u32(style->fullscreen_button_height, scale_fp);
    style->title_scale = kwindow_scale_text_scale(style->title_scale, scale_fp);
    style->close_glyph_scale = kwindow_scale_text_scale(style->close_glyph_scale, scale_fp);
    style->fullscreen_glyph_scale = kwindow_scale_text_scale(style->fullscreen_glyph_scale, scale_fp);
    kwindow_scale_button_style(&style->close_button_style, scale_fp);
    kwindow_scale_button_style(&style->fullscreen_button_style, scale_fp);
}

static inline int32_t kwindow_max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline int32_t kwindow_min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static void kwindow_copy_title(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;

    if (!dst || cap == 0u)
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

static void kwindow_apply_fullscreen_bounds(kwindow_slot *slot)
{
    const kfb *fb = 0;
    kgfx_obj *root = 0;
    uint32_t usable_h = 0u;
    uint32_t min_h = 0u;

    if (!slot)
        return;

    fb = kgfx_info();
    root = kgfx_obj_ref(slot->root);
    if (!fb || !root || root->kind != KGFX_OBJ_RECT)
        return;

    usable_h = fb->height;
    if (G_work_area_bottom_inset < usable_h)
        usable_h -= G_work_area_bottom_inset;

    min_h = kwindow_min_height_for_style(&slot->style);
    if (usable_h < min_h)
        usable_h = min_h;

    root->u.rect.x = 0;
    root->u.rect.y = 0;
    root->u.rect.w = fb->width;
    root->u.rect.h = usable_h;
    kwindow_layout_controls(slot);
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
        slot->fullscreen = 1;
        kwindow_apply_fullscreen_bounds(slot);
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
    if (!slot->fullscreen)
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
    parent_handle.generation = o->parent_generation;
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

static kgfx_obj_handle kwindow_top_ancestor(kgfx_obj_handle h)
{
    kgfx_obj *obj = kgfx_obj_ref(h);

    while (obj && obj->parent_idx >= 0)
    {
        h.idx = (int)obj->parent_idx;
        h.generation = obj->parent_generation;
        obj = kgfx_obj_ref(h);
    }

    return h;
}

static int kwindow_top_window_at_point(int32_t x, int32_t y, kwindow_handle *out_window, int32_t *out_z)
{
    int found = 0;
    int best_idx = -1;
    int32_t best_z = 0;

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        kwindow_resolved_rect root_bounds = {0};

        if (!G_windows[i].used || !G_windows[i].visible)
            continue;

        if (!kwindow_resolve_rect_bounds(G_windows[i].root, &root_bounds))
            continue;

        if (!kwindow_point_in_bounds(x, y, &root_bounds))
            continue;

        if (!found || root_bounds.z >= best_z)
        {
            found = 1;
            best_idx = (int)i;
            best_z = root_bounds.z;
        }
    }

    if (!found)
        return 0;

    if (out_window)
    {
        out_window->idx = best_idx;
        out_window->generation = G_window_generation[best_idx];
    }
    if (out_z)
        *out_z = best_z;
    return 1;
}

static int kwindow_active_modal_child_idx(int parent_idx)
{
    int child_idx = -1;

    if (parent_idx < 0 || parent_idx >= KWINDOW_MAX || !G_windows[parent_idx].used)
        return -1;

    child_idx = G_windows[parent_idx].modal_child_idx;
    if (child_idx < 0 || child_idx >= KWINDOW_MAX)
        return -1;
    if (!G_windows[child_idx].used || !G_windows[child_idx].visible)
        return -1;

    return child_idx;
}

static int kwindow_idx_for_root(kgfx_obj_handle root)
{
    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        if (G_windows[i].used &&
            G_windows[i].root.idx == root.idx &&
            G_windows[i].root.generation == root.generation)
            return (int)i;
    }

    return -1;
}

static void kwindow_clear_modal_refs_for_idx(int idx)
{
    int child_idx = -1;
    int parent_idx = -1;

    if (idx < 0 || idx >= KWINDOW_MAX)
        return;

    child_idx = G_windows[idx].modal_child_idx;
    if (child_idx >= 0 && child_idx < KWINDOW_MAX && G_windows[child_idx].used &&
        G_windows[child_idx].modal_parent_idx == idx)
        G_windows[child_idx].modal_parent_idx = -1;

    parent_idx = G_windows[idx].modal_parent_idx;
    if (parent_idx >= 0 && parent_idx < KWINDOW_MAX && G_windows[parent_idx].used &&
        G_windows[parent_idx].modal_child_idx == idx)
        G_windows[parent_idx].modal_child_idx = -1;

    G_windows[idx].modal_child_idx = -1;
    G_windows[idx].modal_parent_idx = -1;
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

    if (slot->close_deferred)
    {
        slot->close_requested = 1u;
        return;
    }

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
    {
        G_windows[i] = (kwindow_slot){0};
        G_window_generation[i] = 0u;
        G_windows[i].modal_parent_idx = -1;
        G_windows[i].modal_child_idx = -1;
    }
    G_prev_buttons = 0;
    G_prev_mouse_x = 0;
    G_prev_mouse_y = 0;
    G_prev_mouse_valid = 0;
    G_work_area_bottom_inset = 0;
}

kwindow_handle kwindow_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                              int32_t z, const kfont *font, const char *title,
                              const kwindow_style *style)
{
    kwindow_handle handle = {-1};
    kwindow_style resolved_style = kwindow_style_default();
    const char *resolved_title = title ? title : "Window";
    uint32_t scale_fp = kwindow_ui_scale_fp();

    if (style)
        resolved_style = *style;
    kwindow_scale_style(&resolved_style, scale_fp);
    x = kwindow_scale_apply_i32(x, scale_fp);
    y = kwindow_scale_apply_i32(y, scale_fp);
    w = kwindow_scale_apply_u32(w, scale_fp);
    h = kwindow_scale_apply_u32(h, scale_fp);

    if (w < kwindow_min_width_for_style(&resolved_style))
        w = kwindow_min_width_for_style(&resolved_style);
    if (h < kwindow_min_height_for_style(&resolved_style))
        h = kwindow_min_height_for_style(&resolved_style);
    {
        const kfb *fb = kgfx_info();
        uint32_t usable_w = (fb && fb->width > 16u) ? fb->width - 16u : (fb ? fb->width : 0u);
        uint32_t usable_h = (fb && fb->height > 16u + G_work_area_bottom_inset)
                                ? fb->height - 16u - G_work_area_bottom_inset
                                : (fb ? fb->height : 0u);

        if (usable_w && w > usable_w)
            w = usable_w;
        if (usable_h && h > usable_h)
            h = usable_h;
        if (fb && fb->width)
        {
            int32_t max_x = (int32_t)fb->width - (int32_t)w - 8;
            if (max_x < 0)
                max_x = 0;
            if (x < 8)
                x = 8;
            if (x > max_x)
                x = max_x;
        }
        if (fb && fb->height)
        {
            int32_t max_y = (int32_t)fb->height - (int32_t)G_work_area_bottom_inset - (int32_t)h - 8;
            if (max_y < 0)
                max_y = 0;
            if (y < 8)
                y = 8;
            if (y > max_y)
                y = max_y;
        }
    }
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

        ++G_window_generation[i];
        if (!G_window_generation[i])
            G_window_generation[i] = 1u;

        G_windows[i] = (kwindow_slot){0};
        G_windows[i].root.idx = -1;
        G_windows[i].titlebar.idx = -1;
        G_windows[i].title_text.idx = -1;
        G_windows[i].close_text.idx = -1;
        G_windows[i].fullscreen_text.idx = -1;
        G_windows[i].modal_parent_idx = -1;
        G_windows[i].modal_child_idx = -1;
        G_windows[i].close_button.idx = -1;
        G_windows[i].fullscreen_button.idx = -1;
        kwindow_copy_title(G_windows[i].title, sizeof(G_windows[i].title), resolved_title);

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
            G_windows[i].title_text = kgfx_obj_add_text(font, G_windows[i].title,
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
        handle.generation = G_window_generation[i];
        return handle;
    }

    return handle;
}

int kwindow_destroy(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return -1;

    kwindow_clear_modal_refs_for_idx(h.idx);

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
        {
            int modal_idx = kwindow_active_modal_child_idx(focus_idx);
            if (modal_idx >= 0)
            {
                focus_idx = modal_idx;
                resize_candidate_idx = -1;
                resize_candidate_edges = 0;
                drag_candidate_idx = -1;
            }
            kwindow_raise_to_front(focus_idx);
        }

        if (resize_candidate_idx != focus_idx)
        {
            resize_candidate_idx = -1;
            resize_candidate_edges = 0;
        }
        if (drag_candidate_idx != focus_idx)
            drag_candidate_idx = -1;

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
        kwindow_handle top_window = {-1};

        (void)kwindow_top_window_at_point(mouse.x, mouse.y, &top_window, 0);
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
            if (top_window.idx >= 0 && top_window.idx != (int)i)
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

    if (!kwindow_handle_valid(h))
        return;

    G_windows[h.idx].visible = visible ? 1u : 0u;
    if (visible)
        G_windows[h.idx].close_requested = 0u;
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
    if (!kwindow_handle_valid(h))
        return 0;
    return G_windows[h.idx].visible != 0;
}

int kwindow_focused(kwindow_handle h)
{
    kgfx_obj *target = 0;
    int32_t target_z = 0;
    int32_t front_z = 0;
    int found_front = 0;

    if (!kwindow_handle_valid(h) || !G_windows[h.idx].visible)
        return 0;

    target = kgfx_obj_ref(G_windows[h.idx].root);
    if (!target || !target->visible)
        return 0;

    if (kwindow_active_modal_child_idx(h.idx) >= 0)
        return 0;

    target_z = target->z;
    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        kgfx_obj *root = 0;

        if (!G_windows[i].used || !G_windows[i].visible)
            continue;

        root = kgfx_obj_ref(G_windows[i].root);
        if (!root || !root->visible)
            continue;

        if (!found_front || root->z > front_z)
        {
            front_z = root->z;
            found_front = 1;
        }
    }

    return found_front && target_z >= front_z;
}

int kwindow_set_close_deferred(kwindow_handle h, uint8_t deferred)
{
    if (!kwindow_handle_valid(h))
        return -1;
    G_windows[h.idx].close_deferred = deferred ? 1u : 0u;
    if (!deferred)
        G_windows[h.idx].close_requested = 0u;
    return 0;
}

int kwindow_close_requested(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return 0;
    return G_windows[h.idx].close_requested ? 1 : 0;
}

int kwindow_close_accept(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return -1;
    G_windows[h.idx].close_requested = 0u;
    kwindow_set_visible(h, 0u);
    return 0;
}

int kwindow_close_cancel(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return -1;
    G_windows[h.idx].close_requested = 0u;
    return 0;
}

int kwindow_raise(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return 0;
    return kwindow_raise_to_front(h.idx);
}

void kwindow_set_work_area_bottom_inset(uint32_t px)
{
    if (G_work_area_bottom_inset == px)
        return;

    G_work_area_bottom_inset = px;

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        if (G_windows[i].used && G_windows[i].fullscreen)
            kwindow_apply_fullscreen_bounds(&G_windows[i]);
    }
}

void kwindow_set_title(kwindow_handle h, const char *title)
{
    kgfx_obj *title_text = 0;

    if (!kwindow_handle_valid(h))
        return;

    title_text = kgfx_obj_ref(G_windows[h.idx].title_text);
    if (!title_text || title_text->kind != KGFX_OBJ_TEXT)
        return;

    kwindow_copy_title(G_windows[h.idx].title, sizeof(G_windows[h.idx].title), title);
    title_text->u.text.text = G_windows[h.idx].title;
}

kgfx_obj_handle kwindow_root(kwindow_handle h)
{
    if (!kwindow_handle_valid(h))
        return (kgfx_obj_handle){-1};
    return G_windows[h.idx].root;
}

int kwindow_obj_can_receive_input(kgfx_obj_handle h, int32_t x, int32_t y)
{
    kwindow_handle top_window = {-1};
    kwindow_resolved_rect ancestor_bounds = {0};
    kgfx_obj_handle ancestor = {-1};
    int32_t top_window_z = 0;
    int target_idx = -1;
    int modal_idx = -1;

    if (h.idx < 0)
        return 0;

    ancestor = kwindow_top_ancestor(h);
    if (!kwindow_resolve_obj(ancestor, &ancestor_bounds, 0))
        return 0;

    target_idx = kwindow_idx_for_root(ancestor);
    if (target_idx >= 0)
    {
        modal_idx = kwindow_active_modal_child_idx(target_idx);
        if (modal_idx >= 0)
        {
            (void)kwindow_raise_to_front(modal_idx);
            return 0;
        }
    }

    if (!kwindow_top_window_at_point(x, y, &top_window, &top_window_z))
        return 1;

    if (top_window.idx >= 0)
    {
        kgfx_obj_handle top_root = kwindow_root(top_window);
        if (top_root.idx == ancestor.idx && top_root.generation == ancestor.generation)
            return 1;
    }

    return ancestor_bounds.z > top_window_z;
}

int kwindow_point_can_receive_input(kwindow_handle h, int32_t x, int32_t y)
{
    return kwindow_obj_can_receive_input(kwindow_root(h), x, y);
}

int kwindow_center_on_parent(kwindow_handle child, kwindow_handle parent)
{
    kgfx_obj *child_root = 0;
    kwindow_resolved_rect parent_bounds = {0};

    if (!kwindow_handle_valid(child) || !kwindow_handle_valid(parent))
        return -1;

    child_root = kgfx_obj_ref(G_windows[child.idx].root);
    if (!child_root || child_root->kind != KGFX_OBJ_RECT)
        return -1;

    if (!kwindow_resolve_rect_bounds(G_windows[parent.idx].root, &parent_bounds))
        return -1;

    child_root->u.rect.x = parent_bounds.x + (int32_t)(((uint32_t)(parent_bounds.clip.x1 - parent_bounds.clip.x0) > child_root->u.rect.w)
                                                           ? (((uint32_t)(parent_bounds.clip.x1 - parent_bounds.clip.x0) - child_root->u.rect.w) / 2u)
                                                           : 12u);
    child_root->u.rect.y = parent_bounds.y + (int32_t)(((uint32_t)(parent_bounds.clip.y1 - parent_bounds.clip.y0) > child_root->u.rect.h)
                                                           ? (((uint32_t)(parent_bounds.clip.y1 - parent_bounds.clip.y0) - child_root->u.rect.h) / 2u)
                                                           : 12u);
    return 0;
}

int kwindow_set_modal_child(kwindow_handle parent, kwindow_handle child)
{
    int old_child = -1;
    int old_parent = -1;

    if (!kwindow_handle_valid(parent) || !kwindow_handle_valid(child) ||
        parent.idx == child.idx)
        return -1;

    old_child = G_windows[parent.idx].modal_child_idx;
    if (old_child >= 0 && old_child < KWINDOW_MAX && G_windows[old_child].used &&
        G_windows[old_child].modal_parent_idx == parent.idx)
        G_windows[old_child].modal_parent_idx = -1;

    old_parent = G_windows[child.idx].modal_parent_idx;
    if (old_parent >= 0 && old_parent < KWINDOW_MAX && G_windows[old_parent].used &&
        G_windows[old_parent].modal_child_idx == child.idx)
        G_windows[old_parent].modal_child_idx = -1;

    G_windows[parent.idx].modal_child_idx = (int16_t)child.idx;
    G_windows[child.idx].modal_parent_idx = (int16_t)parent.idx;
    (void)kwindow_center_on_parent(child, parent);
    (void)kwindow_raise_to_front(child.idx);
    return 0;
}

int kwindow_clear_modal_child(kwindow_handle parent)
{
    int child_idx = -1;

    if (!kwindow_handle_valid(parent))
        return -1;

    child_idx = G_windows[parent.idx].modal_child_idx;
    if (child_idx >= 0 && child_idx < KWINDOW_MAX && G_windows[child_idx].used &&
        G_windows[child_idx].modal_parent_idx == parent.idx)
        G_windows[child_idx].modal_parent_idx = -1;

    G_windows[parent.idx].modal_child_idx = -1;
    return 0;
}

int kwindow_has_active_modal(kwindow_handle parent)
{
    if (!kwindow_handle_valid(parent))
        return 0;
    return kwindow_active_modal_child_idx(parent.idx) >= 0 ? 1 : 0;
}

kwindow_handle kwindow_modal_parent(kwindow_handle child)
{
    kwindow_handle parent = {-1};
    int parent_idx = -1;

    if (!kwindow_handle_valid(child))
        return parent;

    parent_idx = G_windows[child.idx].modal_parent_idx;
    if (parent_idx >= 0 && parent_idx < KWINDOW_MAX && G_windows[parent_idx].used)
    {
        parent.idx = parent_idx;
        parent.generation = G_window_generation[parent_idx];
    }
    return parent;
}

kwindow_handle kwindow_modal_child(kwindow_handle parent)
{
    kwindow_handle child = {-1};
    int child_idx = -1;

    if (!kwindow_handle_valid(parent))
        return child;

    child_idx = G_windows[parent.idx].modal_child_idx;
    if (child_idx >= 0 && child_idx < KWINDOW_MAX && G_windows[child_idx].used)
    {
        child.idx = child_idx;
        child.generation = G_window_generation[child_idx];
    }
    return child;
}
