#include "kwrappers/kbutton.h"
#include "kwrappers/kmouse.h"

#ifndef KBUTTON_MAX
#define KBUTTON_MAX 128
#endif

#define KBUTTON_MOUSE_LEFT 0x01u

typedef struct
{
    int32_t x0, y0, x1, y1;
    uint8_t enabled;
} kbutton_clip_rect;

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t z;
    kbutton_clip_rect clip;
    uint8_t valid;
} kbutton_resolved_rect;

typedef struct
{
    uint8_t used;
    uint8_t enabled;
    uint8_t hovered;
    uint8_t pressed;
    kgfx_obj_handle root;
    kbutton_style style;
    kbutton_on_click_fn on_click;
    void *user;
} kbutton_slot;

static kbutton_slot G_buttons[KBUTTON_MAX];
static uint8_t G_prev_buttons = 0;

static inline int32_t kbutton_max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline int32_t kbutton_min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static inline int kbutton_clip_intersect(kbutton_clip_rect *dst, const kbutton_clip_rect *other)
{
    if (!dst || !other || !dst->enabled || !other->enabled)
        return 1;

    dst->x0 = kbutton_max_i32(dst->x0, other->x0);
    dst->y0 = kbutton_max_i32(dst->y0, other->y0);
    dst->x1 = kbutton_min_i32(dst->x1, other->x1);
    dst->y1 = kbutton_min_i32(dst->y1, other->y1);
    return dst->x0 < dst->x1 && dst->y0 < dst->y1;
}

static void kbutton_obj_local_origin(const kgfx_obj *o, int32_t *x, int32_t *y)
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

static int kbutton_parent_clip_bounds(const kgfx_obj *o, int32_t world_x, int32_t world_y, kbutton_clip_rect *clip)
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

static int kbutton_resolve_obj(kgfx_obj_handle h, kbutton_resolved_rect *out, uint32_t depth)
{
    const kfb *fb = 0;
    kgfx_obj *o = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    kbutton_resolved_rect parent = {0};
    kbutton_clip_rect parent_bounds = {0};
    kgfx_obj_handle parent_handle;

    if (!out || depth > 32u)
        return 0;

    o = kgfx_obj_ref(h);
    fb = kgfx_info();
    if (!o || !fb)
        return 0;

    kbutton_obj_local_origin(o, &local_x, &local_y);

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
    if (!kbutton_resolve_obj(parent_handle, &parent, depth + 1u))
        return 0;

    out->x += parent.x;
    out->y += parent.y;
    out->z += parent.z;
    out->clip = parent.clip;

    if (o->clip_to_parent && kbutton_parent_clip_bounds(kgfx_obj_ref(parent_handle), parent.x, parent.y, &parent_bounds))
    {
        if (!kbutton_clip_intersect(&out->clip, &parent_bounds))
            return 0;
    }

    return 1;
}

static int kbutton_resolve_root_bounds(const kbutton_slot *slot, kbutton_resolved_rect *out)
{
    kgfx_obj *o = 0;

    if (!slot || !slot->used || !slot->enabled || !out)
        return 0;

    o = kgfx_obj_ref(slot->root);
    if (!o || !o->visible || o->kind != KGFX_OBJ_RECT)
        return 0;

    if (!kbutton_resolve_obj(slot->root, out, 0))
        return 0;

    out->clip.x0 = kbutton_max_i32(out->clip.x0, out->x);
    out->clip.y0 = kbutton_max_i32(out->clip.y0, out->y);
    out->clip.x1 = kbutton_min_i32(out->clip.x1, out->x + (int32_t)o->u.rect.w);
    out->clip.y1 = kbutton_min_i32(out->clip.y1, out->y + (int32_t)o->u.rect.h);

    return out->clip.x0 < out->clip.x1 && out->clip.y0 < out->clip.y1;
}

static void kbutton_apply_visual(kbutton_slot *slot)
{
    kgfx_obj *o = 0;
    kcolor fill = black;

    if (!slot || !slot->used)
        return;

    o = kgfx_obj_ref(slot->root);
    if (!o || o->kind != KGFX_OBJ_RECT)
        return;

    fill = slot->style.fill;
    if (!slot->enabled)
        fill = dim_gray;
    else if (slot->pressed && slot->hovered)
        fill = slot->style.pressed_fill;
    else if (slot->hovered)
        fill = slot->style.hover_fill;

    o->fill = fill;
    o->alpha = slot->style.alpha;
    o->outline = slot->style.outline;
    o->outline_alpha = slot->style.outline_alpha;
    o->outline_width = slot->style.outline_width;
}

void kbutton_init(void)
{
    for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
        G_buttons[i] = (kbutton_slot){0};
    G_prev_buttons = 0;
}

kbutton_handle kbutton_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                int32_t z, const kbutton_style *style,
                                kbutton_on_click_fn on_click, void *user)
{
    kbutton_handle hnd = {-1};
    kbutton_style resolved_style = kbutton_style_default();
    kgfx_obj_handle root = {-1};

    if (style)
        resolved_style = *style;

    for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
    {
        if (G_buttons[i].used)
            continue;

        root = kgfx_obj_add_rect(x, y, w, h, z, resolved_style.fill, 1);
        if (root.idx < 0)
            return hnd;

        G_buttons[i].used = 1;
        G_buttons[i].enabled = 1;
        G_buttons[i].hovered = 0;
        G_buttons[i].pressed = 0;
        G_buttons[i].root = root;
        G_buttons[i].style = resolved_style;
        G_buttons[i].on_click = on_click;
        G_buttons[i].user = user;
        kbutton_apply_visual(&G_buttons[i]);

        hnd.idx = (int)i;
        return hnd;
    }

    return hnd;
}

int kbutton_destroy(kbutton_handle h)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return -1;

    kgfx_obj_destroy(G_buttons[h.idx].root);
    G_buttons[h.idx] = (kbutton_slot){0};
    return 0;
}

void kbutton_update_all(void)
{
    kmouse_state mouse = {0};
    uint8_t left_now = 0;
    uint8_t left_pressed = 0;
    uint8_t left_released = 0;
    int hovered_idx = -1;
    int32_t hovered_z = 0;
    kbutton_resolved_rect resolved[KBUTTON_MAX];

    kmouse_get_state(&mouse);

    left_now = (mouse.buttons & KBUTTON_MOUSE_LEFT) ? 1u : 0u;
    left_pressed = left_now && !(G_prev_buttons & KBUTTON_MOUSE_LEFT);
    left_released = !left_now && (G_prev_buttons & KBUTTON_MOUSE_LEFT);

    for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
    {
        int inside = 0;

        resolved[i] = (kbutton_resolved_rect){0};
        G_buttons[i].hovered = 0;

        if (!kbutton_resolve_root_bounds(&G_buttons[i], &resolved[i]))
            continue;

        inside = mouse.x >= resolved[i].clip.x0 && mouse.y >= resolved[i].clip.y0 &&
                 mouse.x < resolved[i].clip.x1 && mouse.y < resolved[i].clip.y1;

        if (inside && (hovered_idx < 0 || resolved[i].z >= hovered_z))
        {
            hovered_idx = (int)i;
            hovered_z = resolved[i].z;
        }
    }

    if (hovered_idx >= 0)
        G_buttons[hovered_idx].hovered = 1;

    if (left_pressed)
    {
        for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
            G_buttons[i].pressed = 0;
        if (hovered_idx >= 0)
            G_buttons[hovered_idx].pressed = 1;
    }

    if (left_released)
    {
        for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
        {
            if (G_buttons[i].pressed && G_buttons[i].hovered && G_buttons[i].on_click)
                G_buttons[i].on_click((kbutton_handle){(int)i}, G_buttons[i].user);
            G_buttons[i].pressed = 0;
        }
    }
    else if (!left_now)
    {
        for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
            G_buttons[i].pressed = 0;
    }

    for (uint32_t i = 0; i < KBUTTON_MAX; ++i)
        if (G_buttons[i].used)
            kbutton_apply_visual(&G_buttons[i]);

    G_prev_buttons = mouse.buttons;
}

kgfx_obj_handle kbutton_root(kbutton_handle h)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return (kgfx_obj_handle){-1};
    return G_buttons[h.idx].root;
}

void kbutton_set_callback(kbutton_handle h, kbutton_on_click_fn on_click, void *user)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return;
    G_buttons[h.idx].on_click = on_click;
    G_buttons[h.idx].user = user;
}

void kbutton_set_enabled(kbutton_handle h, uint8_t enabled)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return;

    G_buttons[h.idx].enabled = enabled ? 1u : 0u;
    if (!G_buttons[h.idx].enabled)
    {
        G_buttons[h.idx].hovered = 0;
        G_buttons[h.idx].pressed = 0;
    }
    kbutton_apply_visual(&G_buttons[h.idx]);
}

int kbutton_enabled(kbutton_handle h)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return 0;
    return G_buttons[h.idx].enabled != 0;
}

int kbutton_hovered(kbutton_handle h)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return 0;
    return G_buttons[h.idx].hovered != 0;
}

int kbutton_pressed(kbutton_handle h)
{
    if (h.idx < 0 || h.idx >= KBUTTON_MAX || !G_buttons[h.idx].used)
        return 0;
    return G_buttons[h.idx].pressed != 0;
}
