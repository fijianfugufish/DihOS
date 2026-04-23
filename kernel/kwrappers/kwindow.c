#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"

#ifndef KWINDOW_MAX
#define KWINDOW_MAX 64
#endif

#define KWINDOW_MOUSE_LEFT 0x01u

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
    kgfx_obj_handle root;
    kgfx_obj_handle titlebar;
    kgfx_obj_handle title_text;
    kgfx_obj_handle close_text;
    kbutton_handle close_button;
    kwindow_style style;
} kwindow_slot;

static kwindow_slot G_windows[KWINDOW_MAX];
static uint8_t G_prev_buttons = 0;

static inline int32_t kwindow_max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline int32_t kwindow_min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
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

static void kwindow_close_click(kbutton_handle button, void *user)
{
    kwindow_slot *slot = (kwindow_slot *)user;

    (void)button;

    if (!slot)
        return;

    slot->visible = 0;
    slot->dragging = 0;
    if (kgfx_obj_ref(slot->root))
        kgfx_obj_ref(slot->root)->visible = 0;
    kbutton_set_enabled(slot->close_button, 0);
}

void kwindow_init(void)
{
    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        G_windows[i] = (kwindow_slot){0};
    G_prev_buttons = 0;
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

    if (w < 80u)
        w = 80u;
    if (h < resolved_style.titlebar_height + 20u)
        h = resolved_style.titlebar_height + 20u;
    owned_title = kgfx_pmem_strdup(resolved_title);
    if (!owned_title)
        owned_title = resolved_title;

    for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
    {
        kgfx_obj *root_obj = 0;
        kgfx_obj *titlebar_obj = 0;
        kgfx_obj *close_text_obj = 0;
        int32_t title_y = 0;
        int32_t close_x = 0;
        int32_t close_y = 0;
        int32_t close_text_y = 0;

        if (G_windows[i].used)
            continue;

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
            title_y = (int32_t)((resolved_style.titlebar_height > font->h * resolved_style.title_scale)
                                    ? (resolved_style.titlebar_height - font->h * resolved_style.title_scale) / 2u
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

        kgfx_obj_set_parent(kbutton_root(G_windows[i].close_button), G_windows[i].titlebar);

        G_windows[i].close_text.idx = -1;
        if (font)
        {
            close_text_y = (int32_t)((resolved_style.close_button_height > font->h * resolved_style.close_glyph_scale)
                                         ? (resolved_style.close_button_height - font->h * resolved_style.close_glyph_scale) / 2u
                                         : 0u);
            
            close_text_y -= 5;

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
        }

        G_windows[i].used = 1;
        G_windows[i].visible = 1;
        G_windows[i].dragging = 0;
        G_windows[i].style = resolved_style;

        handle.idx = (int)i;
        return handle;
    }

    return handle;
}

int kwindow_destroy(kwindow_handle h)
{
    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return -1;

    if (G_windows[h.idx].close_text.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].close_text);
    if (G_windows[h.idx].title_text.idx >= 0)
        kgfx_obj_destroy(G_windows[h.idx].title_text);
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
    uint8_t left_down = 0;
    uint8_t left_pressed = 0;
    int dragging_idx = -1;
    int candidate_idx = -1;
    int32_t candidate_z = 0;

    kmouse_get_state(&mouse);
    left_down = (mouse.buttons & KWINDOW_MOUSE_LEFT) ? 1u : 0u;
    left_pressed = left_down && !(G_prev_buttons & KWINDOW_MOUSE_LEFT);

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

    if (dragging_idx >= 0)
    {
        kgfx_obj *root = kgfx_obj_ref(G_windows[dragging_idx].root);
        if (!left_down || !root || !G_windows[dragging_idx].visible)
        {
            G_windows[dragging_idx].dragging = 0;
        }
        else if (root->kind == KGFX_OBJ_RECT)
        {
            root->u.rect.x += mouse.dx;
            root->u.rect.y += mouse.dy;
        }
    }
    else if (left_pressed)
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
        {
            kwindow_resolved_rect titlebar_bounds = {0};
            kwindow_resolved_rect close_bounds = {0};
            int over_titlebar = 0;
            int over_close = 0;

            if (!G_windows[i].used || !G_windows[i].visible)
                continue;

            if (!kwindow_resolve_rect_bounds(G_windows[i].titlebar, &titlebar_bounds))
                continue;

            over_titlebar = kwindow_point_in_bounds(mouse.x, mouse.y, &titlebar_bounds);
            if (!over_titlebar)
                continue;

            if (kwindow_resolve_rect_bounds(kbutton_root(G_windows[i].close_button), &close_bounds))
                over_close = kwindow_point_in_bounds(mouse.x, mouse.y, &close_bounds);

            if (!over_close && (candidate_idx < 0 || titlebar_bounds.z >= candidate_z))
            {
                candidate_idx = (int)i;
                candidate_z = titlebar_bounds.z;
            }
        }

        if (candidate_idx >= 0)
            G_windows[candidate_idx].dragging = 1;
    }

    if (!left_down)
    {
        for (uint32_t i = 0; i < KWINDOW_MAX; ++i)
            G_windows[i].dragging = 0;
    }

    G_prev_buttons = mouse.buttons;
}

void kwindow_set_visible(kwindow_handle h, uint8_t visible)
{
    kgfx_obj *root = 0;

    if (h.idx < 0 || h.idx >= KWINDOW_MAX || !G_windows[h.idx].used)
        return;

    G_windows[h.idx].visible = visible ? 1u : 0u;
    G_windows[h.idx].dragging = 0;

    root = kgfx_obj_ref(G_windows[h.idx].root);
    if (root)
        root->visible = visible ? 1u : 0u;

    kbutton_set_enabled(G_windows[h.idx].close_button, visible ? 1u : 0u);
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
