#include "kwrappers/kui.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/ktext.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/colors.h"
#include "kwrappers/string.h"

#ifndef KUI_MAX_VIEWS
#define KUI_MAX_VIEWS 64u
#endif

#ifndef KUI_MAX_VIEW_CHILDREN
#define KUI_MAX_VIEW_CHILDREN 96u
#endif

#ifndef KUI_MAX_DROPDOWNS
#define KUI_MAX_DROPDOWNS 32u
#endif

#ifndef KUI_MAX_RADIOS
#define KUI_MAX_RADIOS 32u
#endif

#ifndef KUI_MAX_TOGGLES
#define KUI_MAX_TOGGLES 64u
#endif

#define KUI_MAX_ITEMS 16u
#define KUI_TEXT_CAP 64u
#define KUI_MOUSE_LEFT 0x01u

typedef struct
{
    uint8_t used;
    uint32_t state;
    kgfx_obj_handle obj;
} kui_view_child;

typedef struct
{
    uint8_t used;
    uint8_t visible;
    uint32_t active_state;
    kgfx_obj_handle root;
    kgfx_obj_handle parent;
    kui_layout_desc layout;
    kui_view_child children[KUI_MAX_VIEW_CHILDREN];
} kui_view_slot;

typedef struct
{
    uint8_t owner_idx;
    uint8_t item_idx;
} kui_item_ref;

typedef struct
{
    uint8_t used;
    uint8_t open;
    uint8_t enabled;
    uint32_t item_count;
    uint32_t selected;
    uint32_t item_h;
    kgfx_obj_handle parent;
    kbutton_handle button;
    kgfx_obj_handle root;
    kgfx_obj_handle label;
    kgfx_obj_handle panel;
    kbutton_handle item_buttons[KUI_MAX_ITEMS];
    kgfx_obj_handle item_labels[KUI_MAX_ITEMS];
    kui_item_ref item_refs[KUI_MAX_ITEMS];
    char items[KUI_MAX_ITEMS][KUI_TEXT_CAP];
    char button_label[KUI_TEXT_CAP + 4u];
    kui_on_change_fn on_change;
    void *user;
} kui_dropdown_slot;

typedef struct
{
    uint8_t used;
    uint8_t enabled;
    uint32_t item_count;
    uint32_t selected;
    uint32_t item_h;
    kgfx_obj_handle parent;
    kgfx_obj_handle root;
    kbutton_handle item_buttons[KUI_MAX_ITEMS];
    kgfx_obj_handle item_labels[KUI_MAX_ITEMS];
    kgfx_obj_handle item_outer[KUI_MAX_ITEMS];
    kgfx_obj_handle item_inner[KUI_MAX_ITEMS];
    kui_item_ref item_refs[KUI_MAX_ITEMS];
    char items[KUI_MAX_ITEMS][KUI_TEXT_CAP];
    char item_text[KUI_MAX_ITEMS][KUI_TEXT_CAP + 5u];
    kui_on_change_fn on_change;
    void *user;
} kui_radio_slot;

typedef struct
{
    uint8_t used;
    uint8_t enabled;
    uint8_t checked;
    kbutton_handle button;
    kgfx_obj_handle root;
    kgfx_obj_handle label_obj;
    kgfx_obj_handle parent;
    char label[KUI_TEXT_CAP];
    char button_label[KUI_TEXT_CAP + 5u];
    kui_on_change_fn on_change;
    void *user;
} kui_toggle_slot;

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint8_t valid;
} kui_resolved_obj;

static const kfont *G_font = 0;
static kui_view_slot G_views[KUI_MAX_VIEWS];
static kui_dropdown_slot G_dropdowns[KUI_MAX_DROPDOWNS];
static kui_radio_slot G_radios[KUI_MAX_RADIOS];
static kui_toggle_slot G_toggles[KUI_MAX_TOGGLES];
static uint8_t G_prev_buttons = 0u;

static kgfx_obj_handle kui_invalid_obj(void)
{
    kgfx_obj_handle h;
    h.idx = -1;
    return h;
}

static kbutton_handle kui_invalid_button(void)
{
    kbutton_handle h;
    h.idx = -1;
    return h;
}

static int kui_obj_valid(kgfx_obj_handle h)
{
    return h.idx >= 0 && kgfx_obj_ref(h) != 0;
}

static uint32_t kui_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static void kui_copy_text(char *dst, uint32_t cap, const char *src)
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

static uint32_t kui_text_len(const char *s)
{
    uint32_t n = 0u;
    if (!s)
        return 0u;
    while (s[n])
        ++n;
    return n;
}

static void kui_append_text(char *dst, uint32_t cap, const char *suffix)
{
    uint32_t len = kui_text_len(dst);
    uint32_t i = 0u;

    if (!dst || !suffix || cap == 0u || len >= cap)
        return;
    while (suffix[i] && len + 1u < cap)
        dst[len++] = suffix[i++];
    dst[len] = 0;
}

uint32_t kui_text_measure_line_px(const char *text, uint32_t scale, int32_t char_spacing)
{
    if (!G_font)
        return 0u;
    return ktext_measure_line_px(G_font, text ? text : "", scale ? scale : 1u, char_spacing);
}

uint32_t kui_text_line_height(uint32_t scale, int32_t line_spacing)
{
    if (!G_font)
        return 0u;
    return ktext_line_height(G_font, scale ? scale : 1u, line_spacing);
}

static int kui_text_fits_px(const char *text, uint32_t scale, const kui_fit_desc *desc)
{
    uint32_t avail_w;
    uint32_t avail_h;
    uint32_t line_h;
    uint32_t line_count = 1u;
    uint32_t line_w = 0u;
    uint32_t widest = 0u;
    const char *line = text ? text : "";

    if (!desc)
        return 1;
    avail_w = desc->max_w > desc->padding_x * 2u ? desc->max_w - desc->padding_x * 2u : 0u;
    avail_h = desc->max_h > desc->padding_y * 2u ? desc->max_h - desc->padding_y * 2u : 0u;
    if (!avail_w && !avail_h)
        return 1;

    for (const char *p = line; ; ++p)
    {
        if (*p == '\n' || *p == 0)
        {
            char tmp[192];
            uint32_t n = (uint32_t)(p - line);
            if (n >= sizeof(tmp))
                n = sizeof(tmp) - 1u;
            for (uint32_t i = 0u; i < n; ++i)
                tmp[i] = line[i];
            tmp[n] = 0;
            line_w = kui_text_measure_line_px(tmp, scale, desc->char_spacing);
            if (line_w > widest)
                widest = line_w;
            if (*p == 0)
                break;
            ++line_count;
            line = p + 1;
        }
    }

    line_h = kui_text_line_height(scale, desc->line_spacing);
    if (avail_w && widest > avail_w)
        return 0;
    if (avail_h && line_h * line_count > avail_h)
        return 0;
    return 1;
}

uint32_t kui_text_fit_scale(const char *text, const kui_fit_desc *desc)
{
    uint32_t max_scale = desc && desc->max_scale ? desc->max_scale : 1u;
    uint32_t min_scale = desc && desc->min_scale ? desc->min_scale : max_scale;

    if (!desc || kui_text_fits_px(text, max_scale, desc))
        return max_scale;

    if ((desc->flags & KUI_TEXT_FIT_ALLOW_FRACTIONAL) != 0u)
    {
        static const uint32_t fps[] = {960u, 900u, 840u, 780u, 720u, 660u, 600u, 540u, 480u};
        for (uint32_t i = 0u; i < (uint32_t)(sizeof(fps) / sizeof(fps[0])); ++i)
        {
            uint32_t s = ktext_scale_from_fp(fps[i]);
            if (kui_text_fits_px(text, s, desc))
                return s;
        }
    }

    return min_scale;
}

int kui_text_ellipsize(const char *text, uint32_t scale, int32_t char_spacing,
                       uint32_t max_w, char *out, uint32_t out_cap)
{
    uint32_t len = 0u;

    if (!out || out_cap == 0u)
        return -1;
    kui_copy_text(out, out_cap, text);
    if (!max_w || kui_text_measure_line_px(out, scale, char_spacing) <= max_w)
        return 0;
    while (out[len])
        ++len;
    if (out_cap < 4u)
    {
        out[0] = 0;
        return 1;
    }
    while (len > 0u)
    {
        --len;
        out[len] = 0;
        if (len + 4u <= out_cap)
        {
            out[len] = '.';
            out[len + 1u] = '.';
            out[len + 2u] = '.';
            out[len + 3u] = 0;
            if (kui_text_measure_line_px(out, scale, char_spacing) <= max_w)
                return 1;
        }
    }
    kui_copy_text(out, out_cap, "...");
    return 1;
}

int kui_text_wrap_words(const char *text, uint32_t scale, int32_t char_spacing,
                        uint32_t max_w, uint32_t max_lines, char *out, uint32_t out_cap)
{
    uint32_t out_i = 0u;
    uint32_t line_start = 0u;
    uint32_t last_space = 0xFFFFFFFFu;
    uint32_t lines = 1u;

    if (!out || out_cap == 0u)
        return -1;
    if (!text)
        text = "";
    if (max_lines == 0u)
        max_lines = 1u;

    for (uint32_t i = 0u; text[i] && out_i + 1u < out_cap; ++i)
    {
        out[out_i++] = text[i];
        out[out_i] = 0;
        if (text[i] == ' ')
            last_space = out_i - 1u;
        if (text[i] == '\n')
        {
            ++lines;
            line_start = out_i;
            last_space = 0xFFFFFFFFu;
            if (lines > max_lines)
                break;
            continue;
        }
        if (max_w && kui_text_measure_line_px(&out[line_start], scale, char_spacing) > max_w)
        {
            if (lines >= max_lines)
            {
                out[out_i] = 0;
                (void)kui_text_ellipsize(&out[line_start], scale, char_spacing, max_w, &out[line_start], out_cap - line_start);
                return 1;
            }
            if (last_space != 0xFFFFFFFFu && last_space >= line_start)
            {
                out[last_space] = '\n';
                line_start = last_space + 1u;
                while (out[line_start] == ' ')
                    ++line_start;
                last_space = 0xFFFFFFFFu;
            }
            else if (out_i + 1u < out_cap)
            {
                out[out_i - 1u] = '\n';
                out[out_i++] = text[i];
                out[out_i] = 0;
                line_start = out_i - 1u;
            }
            ++lines;
        }
    }
    out[out_i] = 0;
    return 0;
}

int kui_label_fit(kgfx_obj_handle text_obj, char *buffer, uint32_t buffer_cap,
                  const char *text, const kui_fit_desc *desc)
{
    kui_fit_desc fit = desc ? *desc : (kui_fit_desc){0};
    uint32_t scale;
    uint32_t avail_w;

    if (!buffer || buffer_cap == 0u)
        return -1;
    if (!fit.max_scale)
        fit.max_scale = 1u;
    if (!fit.min_scale)
        fit.min_scale = fit.max_scale;
    scale = kui_text_fit_scale(text, &fit);
    avail_w = fit.max_w > fit.padding_x * 2u ? fit.max_w - fit.padding_x * 2u : 0u;
    if ((fit.flags & KUI_TEXT_FIT_WRAP) != 0u)
        (void)kui_text_wrap_words(text, scale, fit.char_spacing, avail_w, fit.max_lines, buffer, buffer_cap);
    else if ((fit.flags & KUI_TEXT_FIT_ELLIPSIZE) != 0u)
        (void)kui_text_ellipsize(text, scale, fit.char_spacing, avail_w, buffer, buffer_cap);
    else
        kui_copy_text(buffer, buffer_cap, text);

    kgfx_text_set_scale(text_obj, scale);
    kgfx_text_set_spacing(text_obj, fit.char_spacing, fit.line_spacing);
    kgfx_text_set(text_obj, buffer);
    return 0;
}

int kui_button_fit_label(kbutton_handle button, kgfx_obj_handle label_obj,
                         char *buffer, uint32_t buffer_cap,
                         const char *text, const kui_fit_desc *desc)
{
    kgfx_obj *root = kgfx_obj_ref(kbutton_root(button));
    kgfx_obj *label = kgfx_obj_ref(label_obj);
    kui_fit_desc fit = desc ? *desc : (kui_fit_desc){0};
    uint32_t line_h;

    if (!root || root->kind != KGFX_OBJ_RECT || !label || label->kind != KGFX_OBJ_TEXT)
        return -1;
    if (!fit.max_w)
        fit.max_w = root->u.rect.w;
    if (!fit.max_scale)
        fit.max_scale = 1u;
    if (!fit.min_scale)
        fit.min_scale = fit.max_scale;
    if (kui_label_fit(label_obj, buffer, buffer_cap, text, &fit) != 0)
        return -1;
    line_h = kui_text_line_height(label->u.text.scale, fit.line_spacing);
    label->u.text.x = (int32_t)(root->u.rect.w / 2u);
    label->u.text.y = (int32_t)((root->u.rect.h > line_h) ? ((root->u.rect.h - line_h) / 2u) : 0u) - 2;
    if (label->u.text.y < 0)
        label->u.text.y = 0;
    label->u.text.align = KTEXT_ALIGN_CENTER;
    return 0;
}

static void kui_fit_label_to_parent(kgfx_obj_handle label_obj, kgfx_obj_handle parent, char *buffer, uint32_t cap)
{
    kgfx_obj *label = kgfx_obj_ref(label_obj);
    kgfx_obj *root = kgfx_obj_ref(parent);
    kui_fit_desc fit = {0};
    uint32_t x = 0u;

    if (!label || label->kind != KGFX_OBJ_TEXT || !root || root->kind != KGFX_OBJ_RECT || !buffer)
        return;

    x = label->u.text.x > 0 ? (uint32_t)label->u.text.x : 0u;
    fit.max_w = root->u.rect.w > x + 4u ? root->u.rect.w - x - 4u : 1u;
    fit.max_scale = 1u;
    fit.min_scale = ktext_scale_from_fp(560u);
    fit.flags = KUI_TEXT_FIT_ELLIPSIZE | KUI_TEXT_FIT_ALLOW_FRACTIONAL;
    (void)kui_label_fit(label_obj, buffer, cap, buffer, &fit);
}

static int32_t kui_label_y(uint32_t h)
{
    uint32_t line_h = 16u;
    if (G_font)
        line_h = ktext_line_height(G_font, 1u, 0);
    if (h <= line_h)
        return 2;
    return (int32_t)((h - line_h) / 2u);
}

static kbutton_style kui_button_style(void)
{
    kbutton_style s = kbutton_style_default();
    s.fill = dim_gray;
    s.hover_fill = slate_gray;
    s.pressed_fill = steel_blue;
    s.outline = light_gray;
    s.outline_width = 1;
    s.alpha = 245;
    return s;
}

static kbutton_style kui_selected_button_style(void)
{
    kbutton_style s = kui_button_style();
    s.fill = steel_blue;
    s.hover_fill = dodger_blue;
    s.pressed_fill = royal_blue;
    return s;
}

static kgfx_obj_handle kui_add_label_obj_at(kgfx_obj_handle parent, const char *text, uint32_t h, int32_t z, int32_t x)
{
    kgfx_obj_handle label = kui_invalid_obj();

    if (!G_font)
        return label;

    label = kgfx_obj_add_text(G_font, text ? text : "", x, kui_label_y(h), z,
                              white, 255u, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1u);
    if (label.idx >= 0 && parent.idx >= 0)
    {
        kgfx_obj_set_parent(label, parent);
        kgfx_obj_set_clip_to_parent(label, 1u);
    }
    return label;
}

static kgfx_obj_handle kui_add_label_obj(kgfx_obj_handle parent, const char *text, uint32_t h, int32_t z)
{
    return kui_add_label_obj_at(parent, text, h, z, 8);
}

static void kui_obj_set_visible(kgfx_obj_handle h, uint8_t visible)
{
    kgfx_obj *o = kgfx_obj_ref(h);
    if (o)
        o->visible = visible ? 1u : 0u;
}

static void kui_obj_set_bounds(kgfx_obj_handle h, int32_t x, int32_t y, uint32_t w, uint32_t h_px)
{
    kgfx_obj *o = kgfx_obj_ref(h);
    uint32_t min_dim = kui_min_u32(w, h_px);

    if (!o)
        return;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        o->u.rect.x = x;
        o->u.rect.y = y;
        o->u.rect.w = w;
        o->u.rect.h = h_px;
        break;
    case KGFX_OBJ_IMAGE:
        o->u.image.x = x;
        o->u.image.y = y;
        o->u.image.w = w;
        o->u.image.h = h_px;
        break;
    case KGFX_OBJ_CIRCLE:
        o->u.circle.cx = x + (int32_t)(w / 2u);
        o->u.circle.cy = y + (int32_t)(h_px / 2u);
        o->u.circle.r = min_dim / 2u;
        break;
    case KGFX_OBJ_TEXT:
        o->u.text.x = x + 2;
        o->u.text.y = y + kui_label_y(h_px);
        break;
    default:
        break;
    }
}

static void kui_obj_local_origin(const kgfx_obj *o, int32_t *x, int32_t *y)
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
        *x = o->u.circle.cx - (int32_t)o->u.circle.r;
        *y = o->u.circle.cy - (int32_t)o->u.circle.r;
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

static int kui_resolve_obj(kgfx_obj_handle h, kui_resolved_obj *out, uint32_t depth)
{
    kgfx_obj *o = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    kui_resolved_obj parent = {0};
    kgfx_obj_handle parent_handle;

    if (!out || depth > 32u)
        return 0;

    o = kgfx_obj_ref(h);
    if (!o || !o->visible)
        return 0;

    kui_obj_local_origin(o, &local_x, &local_y);
    out->x = local_x;
    out->y = local_y;
    out->z = o->z;
    out->valid = 1u;

    if (o->parent_idx < 0)
        return 1;

    parent_handle.idx = (int)o->parent_idx;
    parent_handle.generation = o->parent_generation;
    if (!kui_resolve_obj(parent_handle, &parent, depth + 1u))
        return 0;

    out->x += parent.x;
    out->y += parent.y;
    out->z += parent.z;
    return 1;
}

static int kui_obj_world_bounds(kgfx_obj_handle h, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    kgfx_obj *o = kgfx_obj_ref(h);
    kui_resolved_obj r = {0};
    uint32_t w = 0u;
    uint32_t h_px = 0u;

    if (!o || !x0 || !y0 || !x1 || !y1)
        return 0;
    if (!kui_resolve_obj(h, &r, 0u))
        return 0;

    switch (o->kind)
    {
    case KGFX_OBJ_RECT:
        w = o->u.rect.w;
        h_px = o->u.rect.h;
        break;
    case KGFX_OBJ_IMAGE:
        w = o->u.image.w;
        h_px = o->u.image.h;
        break;
    case KGFX_OBJ_CIRCLE:
        w = o->u.circle.r * 2u;
        h_px = o->u.circle.r * 2u;
        break;
    default:
        return 0;
    }

    *x0 = r.x;
    *y0 = r.y;
    *x1 = r.x + (int32_t)w;
    *y1 = r.y + (int32_t)h_px;
    return *x0 < *x1 && *y0 < *y1;
}

static int kui_point_in_obj(kgfx_obj_handle h, int32_t x, int32_t y)
{
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;

    if (!kui_obj_world_bounds(h, &x0, &y0, &x1, &y1))
        return 0;
    return x >= x0 && y >= y0 && x < x1 && y < y1;
}

static kui_view_slot *kui_view_slot_from_handle(kui_view_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= KUI_MAX_VIEWS || !G_views[h.idx].used)
        return 0;
    return &G_views[h.idx];
}

static kui_dropdown_slot *kui_dropdown_slot_from_handle(kui_dropdown_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= KUI_MAX_DROPDOWNS || !G_dropdowns[h.idx].used)
        return 0;
    return &G_dropdowns[h.idx];
}

static kui_radio_slot *kui_radio_slot_from_handle(kui_radio_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= KUI_MAX_RADIOS || !G_radios[h.idx].used)
        return 0;
    return &G_radios[h.idx];
}

static kui_toggle_slot *kui_toggle_slot_from_handle(kui_toggle_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= KUI_MAX_TOGGLES || !G_toggles[h.idx].used)
        return 0;
    return &G_toggles[h.idx];
}

static int kui_child_visible_for_state(const kui_view_slot *slot, const kui_view_child *child)
{
    if (!slot || !child || !child->used)
        return 0;
    return slot->visible && (child->state == KUI_STATE_ALWAYS || child->state == slot->active_state);
}

static void kui_view_sync_children(kui_view_slot *slot)
{
    if (!slot || !slot->used)
        return;

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
    {
        if (!slot->children[i].used)
            continue;
        kui_obj_set_visible(slot->children[i].obj,
                            kui_child_visible_for_state(slot, &slot->children[i]) ? 1u : 0u);
    }
}

static void kui_view_reset_slot(kui_view_slot *slot)
{
    if (!slot)
        return;
    *slot = (kui_view_slot){0};
    slot->root = kui_invalid_obj();
    slot->parent = kui_invalid_obj();
    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
        slot->children[i].obj = kui_invalid_obj();
}

static void kui_dropdown_reset_slot(kui_dropdown_slot *slot)
{
    if (!slot)
        return;
    *slot = (kui_dropdown_slot){0};
    slot->button = kui_invalid_button();
    slot->root = kui_invalid_obj();
    slot->label = kui_invalid_obj();
    slot->panel = kui_invalid_obj();
    slot->parent = kui_invalid_obj();
    for (uint32_t i = 0u; i < KUI_MAX_ITEMS; ++i)
    {
        slot->item_buttons[i] = kui_invalid_button();
        slot->item_labels[i] = kui_invalid_obj();
    }
}

static void kui_radio_reset_slot(kui_radio_slot *slot)
{
    if (!slot)
        return;
    *slot = (kui_radio_slot){0};
    slot->root = kui_invalid_obj();
    slot->parent = kui_invalid_obj();
    for (uint32_t i = 0u; i < KUI_MAX_ITEMS; ++i)
    {
        slot->item_buttons[i] = kui_invalid_button();
        slot->item_labels[i] = kui_invalid_obj();
        slot->item_outer[i] = kui_invalid_obj();
        slot->item_inner[i] = kui_invalid_obj();
    }
}

static void kui_toggle_reset_slot(kui_toggle_slot *slot)
{
    if (!slot)
        return;
    *slot = (kui_toggle_slot){0};
    slot->button = kui_invalid_button();
    slot->root = kui_invalid_obj();
    slot->label_obj = kui_invalid_obj();
    slot->parent = kui_invalid_obj();
}

void kui_init(const struct kfont *font)
{
    G_font = (const kfont *)font;
    G_prev_buttons = 0u;
    for (uint32_t i = 0u; i < KUI_MAX_VIEWS; ++i)
        kui_view_reset_slot(&G_views[i]);
    for (uint32_t i = 0u; i < KUI_MAX_DROPDOWNS; ++i)
        kui_dropdown_reset_slot(&G_dropdowns[i]);
    for (uint32_t i = 0u; i < KUI_MAX_RADIOS; ++i)
        kui_radio_reset_slot(&G_radios[i]);
    for (uint32_t i = 0u; i < KUI_MAX_TOGGLES; ++i)
        kui_toggle_reset_slot(&G_toggles[i]);
}

void kui_set_font(const struct kfont *font)
{
    G_font = (const kfont *)font;

    for (uint32_t i = 0u; i < KUI_MAX_DROPDOWNS; ++i)
    {
        if (!G_dropdowns[i].used)
            continue;
        kgfx_text_set_font(G_dropdowns[i].label, G_font);
        for (uint32_t j = 0u; j < G_dropdowns[i].item_count; ++j)
            kgfx_text_set_font(G_dropdowns[i].item_labels[j], G_font);
    }

    for (uint32_t i = 0u; i < KUI_MAX_RADIOS; ++i)
    {
        if (!G_radios[i].used)
            continue;
        for (uint32_t j = 0u; j < G_radios[i].item_count; ++j)
            kgfx_text_set_font(G_radios[i].item_labels[j], G_font);
    }

    for (uint32_t i = 0u; i < KUI_MAX_TOGGLES; ++i)
        if (G_toggles[i].used)
            kgfx_text_set_font(G_toggles[i].label_obj, G_font);
}

int kui_view_create_rect(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t h,
                         int32_t z, kcolor fill, uint32_t visible,
                         kui_view_handle *out_view, kgfx_obj_handle *out_root)
{
    kgfx_obj_handle root = kui_invalid_obj();

    if (!out_view || !out_root || w == 0u || h == 0u)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_VIEWS; ++i)
    {
        if (G_views[i].used)
            continue;

        root = kgfx_obj_add_rect(x, y, w, h, z, fill, visible ? 1u : 0u);
        if (root.idx < 0)
            return -1;
        if (parent.idx >= 0 && kgfx_obj_ref(parent))
            kgfx_obj_set_parent(root, parent);

        kui_view_reset_slot(&G_views[i]);
        G_views[i].used = 1u;
        G_views[i].visible = visible ? 1u : 0u;
        G_views[i].root = root;
        G_views[i].parent = parent;
        G_views[i].active_state = 0u;
        G_views[i].layout.kind = KUI_LAYOUT_NONE;

        out_view->idx = (int)i;
        *out_root = root;
        return 0;
    }

    return -1;
}

int kui_view_destroy(kui_view_handle view)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    if (!slot)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
    {
        if (!slot->children[i].used)
            continue;
        if (kui_obj_valid(slot->children[i].obj))
        {
            kgfx_obj_clear_parent(slot->children[i].obj);
            kui_obj_set_visible(slot->children[i].obj, 1u);
        }
    }

    if (slot->root.idx >= 0)
        (void)kgfx_obj_destroy(slot->root);
    kui_view_reset_slot(slot);
    return 0;
}

kgfx_obj_handle kui_view_root(kui_view_handle view)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    return slot ? slot->root : kui_invalid_obj();
}

int kui_view_add_obj(kui_view_handle view, uint32_t state, kgfx_obj_handle obj)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);

    if (!slot || !kui_obj_valid(obj))
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
    {
        if (!slot->children[i].used)
            continue;
        if (slot->children[i].obj.idx == obj.idx)
        {
            slot->children[i].state = state;
            kgfx_obj_set_parent(obj, slot->root);
            kgfx_obj_set_clip_to_parent(obj, 1u);
            kui_view_sync_children(slot);
            (void)kui_view_apply_layout(view);
            return 0;
        }
    }

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
    {
        if (slot->children[i].used)
            continue;
        slot->children[i].used = 1u;
        slot->children[i].state = state;
        slot->children[i].obj = obj;
        kgfx_obj_set_parent(obj, slot->root);
        kgfx_obj_set_clip_to_parent(obj, 1u);
        kui_view_sync_children(slot);
        (void)kui_view_apply_layout(view);
        return 0;
    }

    return -1;
}

int kui_view_set_state(kui_view_handle view, uint32_t state)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    if (!slot)
        return -1;

    slot->active_state = state;
    kui_view_sync_children(slot);
    return kui_view_apply_layout(view);
}

uint32_t kui_view_state(kui_view_handle view)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    return slot ? slot->active_state : 0u;
}

int kui_view_set_visible(kui_view_handle view, uint32_t visible)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    if (!slot)
        return -1;

    slot->visible = visible ? 1u : 0u;
    kui_obj_set_visible(slot->root, slot->visible);
    kui_view_sync_children(slot);
    return 0;
}

int kui_view_set_layout(kui_view_handle view, const kui_layout_desc *desc)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    if (!slot)
        return -1;

    if (desc)
        slot->layout = *desc;
    else
        slot->layout = (kui_layout_desc){0};

    return kui_view_apply_layout(view);
}

int kui_view_apply_layout(kui_view_handle view)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    kgfx_obj *root = 0;
    uint32_t count = 0u;
    uint32_t index = 0u;
    uint32_t columns = 1u;
    uint32_t rows = 1u;
    uint32_t avail_w = 0u;
    uint32_t avail_h = 0u;
    uint32_t cell_w = 0u;
    uint32_t cell_h = 0u;

    if (!slot)
        return -1;
    if (slot->layout.kind == KUI_LAYOUT_NONE)
        return 0;

    root = kgfx_obj_ref(slot->root);
    if (!root || root->kind != KGFX_OBJ_RECT)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
        if (kui_child_visible_for_state(slot, &slot->children[i]) && kui_obj_valid(slot->children[i].obj))
            ++count;
    if (count == 0u)
        return 0;

    avail_w = root->u.rect.w;
    avail_h = root->u.rect.h;
    if (avail_w > slot->layout.padding_x * 2u)
        avail_w -= slot->layout.padding_x * 2u;
    else
        avail_w = 0u;
    if (avail_h > slot->layout.padding_y * 2u)
        avail_h -= slot->layout.padding_y * 2u;
    else
        avail_h = 0u;

    if (slot->layout.kind == KUI_LAYOUT_ROW)
    {
        if (count > 1u && avail_w > slot->layout.gap_x * (count - 1u))
            avail_w -= slot->layout.gap_x * (count - 1u);
        cell_w = count ? (avail_w / count) : avail_w;
        cell_h = avail_h;
    }
    else if (slot->layout.kind == KUI_LAYOUT_COLUMN)
    {
        if (count > 1u && avail_h > slot->layout.gap_y * (count - 1u))
            avail_h -= slot->layout.gap_y * (count - 1u);
        cell_w = avail_w;
        cell_h = count ? (avail_h / count) : avail_h;
    }
    else if (slot->layout.kind == KUI_LAYOUT_GRID)
    {
        columns = slot->layout.columns ? slot->layout.columns : 1u;
        if (columns > count)
            columns = count;
        rows = (count + columns - 1u) / columns;
        if (columns > 1u && avail_w > slot->layout.gap_x * (columns - 1u))
            avail_w -= slot->layout.gap_x * (columns - 1u);
        if (rows > 1u && avail_h > slot->layout.gap_y * (rows - 1u))
            avail_h -= slot->layout.gap_y * (rows - 1u);
        cell_w = columns ? (avail_w / columns) : avail_w;
        cell_h = rows ? (avail_h / rows) : avail_h;
        if (slot->layout.flags & KUI_LAYOUT_SQUARE_CELLS)
        {
            uint32_t side = kui_min_u32(cell_w, cell_h);
            cell_w = side;
            cell_h = side;
        }
    }
    else
        return -1;

    if (cell_w == 0u)
        cell_w = 1u;
    if (cell_h == 0u)
        cell_h = 1u;

    for (uint32_t i = 0u; i < KUI_MAX_VIEW_CHILDREN; ++i)
    {
        int32_t x = (int32_t)slot->layout.padding_x;
        int32_t y = (int32_t)slot->layout.padding_y;

        if (!kui_child_visible_for_state(slot, &slot->children[i]) || !kui_obj_valid(slot->children[i].obj))
            continue;

        if (slot->layout.kind == KUI_LAYOUT_ROW)
            x += (int32_t)(index * (cell_w + slot->layout.gap_x));
        else if (slot->layout.kind == KUI_LAYOUT_COLUMN)
            y += (int32_t)(index * (cell_h + slot->layout.gap_y));
        else
        {
            uint32_t col = columns ? (index % columns) : 0u;
            uint32_t row = columns ? (index / columns) : 0u;
            x += (int32_t)(col * (cell_w + slot->layout.gap_x));
            y += (int32_t)(row * (cell_h + slot->layout.gap_y));
        }

        kui_obj_set_bounds(slot->children[i].obj, x, y, cell_w, cell_h);
        ++index;
    }

    return 0;
}

int kui_view_set_bounds(kui_view_handle view, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    kui_view_slot *slot = kui_view_slot_from_handle(view);
    if (!slot || w == 0u || h == 0u)
        return -1;

    kui_obj_set_bounds(slot->root, x, y, w, h);
    return kui_view_apply_layout(view);
}

static void kui_dropdown_make_label(kui_dropdown_slot *slot)
{
    if (!slot)
        return;
    if (slot->item_count == 0u || slot->selected >= slot->item_count)
        kui_copy_text(slot->button_label, sizeof(slot->button_label), "");
    else
        kui_copy_text(slot->button_label, sizeof(slot->button_label), slot->items[slot->selected]);
    kui_append_text(slot->button_label, sizeof(slot->button_label), slot->open ? " ^" : " v");
    kgfx_text_set(slot->label, slot->button_label);
    kui_fit_label_to_parent(slot->label, slot->root, slot->button_label, sizeof(slot->button_label));
}

static void kui_dropdown_set_open(kui_dropdown_slot *slot, uint8_t open)
{
    if (!slot || !slot->used)
        return;
    if (!slot->enabled)
        open = 0u;
    slot->open = open ? 1u : 0u;
    kui_obj_set_visible(slot->panel, slot->open);
    kui_dropdown_make_label(slot);
}

static void kui_dropdown_root_click(kbutton_handle button, void *user)
{
    kui_dropdown_slot *slot = (kui_dropdown_slot *)user;
    (void)button;

    if (!slot || !slot->used || !slot->enabled)
        return;

    for (uint32_t i = 0u; i < KUI_MAX_DROPDOWNS; ++i)
        if (&G_dropdowns[i] != slot)
            kui_dropdown_set_open(&G_dropdowns[i], 0u);
    kui_dropdown_set_open(slot, slot->open ? 0u : 1u);
}

static void kui_dropdown_item_click(kbutton_handle button, void *user)
{
    kui_item_ref *ref = (kui_item_ref *)user;
    kui_dropdown_slot *slot = 0;
    (void)button;

    if (!ref || ref->owner_idx >= KUI_MAX_DROPDOWNS)
        return;

    slot = &G_dropdowns[ref->owner_idx];
    if (!slot->used || !slot->enabled || ref->item_idx >= slot->item_count)
        return;

    slot->selected = ref->item_idx;
    kui_dropdown_set_open(slot, 0u);
    if (slot->on_change)
        slot->on_change((uint32_t)ref->owner_idx + 1u, (int32_t)slot->selected, slot->user);
}

int kui_dropdown_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                        int32_t z, const char *const *items, uint32_t item_count,
                        uint32_t selected, kui_on_change_fn on_change, void *user,
                        kui_dropdown_handle *out_dropdown, kgfx_obj_handle *out_root)
{
    kbutton_style style = kui_button_style();
    kbutton_style item_style = kui_button_style();

    if (!out_dropdown || !out_root || !items || item_count == 0u || item_count > KUI_MAX_ITEMS || w == 0u || item_h == 0u)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_DROPDOWNS; ++i)
    {
        kui_dropdown_slot *slot = &G_dropdowns[i];
        if (slot->used)
            continue;

        kui_dropdown_reset_slot(slot);
        slot->used = 1u;
        slot->enabled = 1u;
        slot->parent = parent;
        slot->item_count = item_count;
        slot->selected = selected < item_count ? selected : 0u;
        slot->item_h = item_h;
        slot->on_change = on_change;
        slot->user = user;

        for (uint32_t j = 0u; j < item_count; ++j)
            kui_copy_text(slot->items[j], sizeof(slot->items[j]), items[j]);

        slot->button = kbutton_add_rect(x, y, w, item_h, z, &style, kui_dropdown_root_click, slot);
        slot->root = kbutton_root(slot->button);
        if (slot->button.idx < 0 || slot->root.idx < 0)
            goto fail;
        if (parent.idx >= 0 && kgfx_obj_ref(parent))
            kgfx_obj_set_parent(slot->root, parent);

        kui_dropdown_make_label(slot);
        slot->label = kui_add_label_obj(slot->root, slot->button_label, item_h, 1);

        slot->panel = kgfx_obj_add_rect(x, y + (int32_t)item_h, w, item_h * item_count, z + 16, dark_slate_gray, 0u);
        if (slot->panel.idx < 0)
            goto fail;
        kgfx_obj_set_outline(slot->panel, 1u, light_gray);
        if (parent.idx >= 0 && kgfx_obj_ref(parent))
        {
            kgfx_obj_set_parent(slot->panel, parent);
            kgfx_obj_set_clip_to_parent(slot->panel, 0u);
        }

        for (uint32_t j = 0u; j < item_count; ++j)
        {
            slot->item_refs[j].owner_idx = (uint8_t)i;
            slot->item_refs[j].item_idx = (uint8_t)j;
            slot->item_buttons[j] = kbutton_add_rect(0, (int32_t)(j * item_h), w, item_h, 1,
                                                     &item_style, kui_dropdown_item_click, &slot->item_refs[j]);
            if (slot->item_buttons[j].idx < 0)
                goto fail;
            kgfx_obj_set_parent(kbutton_root(slot->item_buttons[j]), slot->panel);
            slot->item_labels[j] = kui_add_label_obj(kbutton_root(slot->item_buttons[j]), slot->items[j], item_h, 1);
        }

        kui_dropdown_set_open(slot, 0u);
        out_dropdown->idx = (int)i;
        *out_root = slot->root;
        return 0;

    fail:
        (void)kui_dropdown_destroy((kui_dropdown_handle){(int)i});
        return -1;
    }

    return -1;
}

int kui_dropdown_destroy(kui_dropdown_handle dropdown)
{
    kui_dropdown_slot *slot = kui_dropdown_slot_from_handle(dropdown);
    if (!slot)
        return -1;

    if (slot->label.idx >= 0)
        (void)kgfx_obj_destroy(slot->label);
    for (uint32_t i = 0u; i < KUI_MAX_ITEMS; ++i)
    {
        if (slot->item_labels[i].idx >= 0)
            (void)kgfx_obj_destroy(slot->item_labels[i]);
        if (slot->item_buttons[i].idx >= 0)
            (void)kbutton_destroy(slot->item_buttons[i]);
    }
    if (slot->panel.idx >= 0)
        (void)kgfx_obj_destroy(slot->panel);
    if (slot->button.idx >= 0)
        (void)kbutton_destroy(slot->button);
    kui_dropdown_reset_slot(slot);
    return 0;
}

kgfx_obj_handle kui_dropdown_root(kui_dropdown_handle dropdown)
{
    kui_dropdown_slot *slot = kui_dropdown_slot_from_handle(dropdown);
    return slot ? slot->root : kui_invalid_obj();
}

int kui_dropdown_selected(kui_dropdown_handle dropdown)
{
    kui_dropdown_slot *slot = kui_dropdown_slot_from_handle(dropdown);
    return slot ? (int)slot->selected : -1;
}

int kui_dropdown_set_selected(kui_dropdown_handle dropdown, uint32_t selected)
{
    kui_dropdown_slot *slot = kui_dropdown_slot_from_handle(dropdown);
    if (!slot || selected >= slot->item_count)
        return -1;
    slot->selected = selected;
    kui_dropdown_make_label(slot);
    return 0;
}

int kui_dropdown_set_enabled(kui_dropdown_handle dropdown, uint32_t enabled)
{
    kui_dropdown_slot *slot = kui_dropdown_slot_from_handle(dropdown);
    if (!slot)
        return -1;
    slot->enabled = enabled ? 1u : 0u;
    kbutton_set_enabled(slot->button, slot->enabled);
    if (!slot->enabled)
        kui_dropdown_set_open(slot, 0u);
    return 0;
}

static void kui_radio_make_label(kui_radio_slot *slot, uint32_t item)
{
    if (!slot || item >= slot->item_count)
        return;
    kui_copy_text(slot->item_text[item], sizeof(slot->item_text[item]), slot->items[item]);
    kgfx_text_set(slot->item_labels[item], slot->item_text[item]);
    kui_fit_label_to_parent(slot->item_labels[item], kbutton_root(slot->item_buttons[item]),
                            slot->item_text[item], sizeof(slot->item_text[item]));
}

static void kui_radio_sync(kui_radio_slot *slot)
{
    kbutton_style normal_style = kui_button_style();
    kbutton_style selected_style = kui_selected_button_style();

    if (!slot || !slot->used)
        return;

    for (uint32_t i = 0u; i < slot->item_count; ++i)
    {
        kui_radio_make_label(slot, i);
        kbutton_set_style(slot->item_buttons[i], i == slot->selected ? &selected_style : &normal_style);
        kui_obj_set_visible(slot->item_inner[i], i == slot->selected ? 1u : 0u);
    }
}

static void kui_radio_item_click(kbutton_handle button, void *user)
{
    kui_item_ref *ref = (kui_item_ref *)user;
    kui_radio_slot *slot = 0;
    (void)button;

    if (!ref || ref->owner_idx >= KUI_MAX_RADIOS)
        return;

    slot = &G_radios[ref->owner_idx];
    if (!slot->used || !slot->enabled || ref->item_idx >= slot->item_count)
        return;

    slot->selected = ref->item_idx;
    kui_radio_sync(slot);
    if (slot->on_change)
        slot->on_change((uint32_t)ref->owner_idx + 1u, (int32_t)slot->selected, slot->user);
}

int kui_radio_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                     int32_t z, const char *const *items, uint32_t item_count,
                     uint32_t selected, kui_on_change_fn on_change, void *user,
                     kui_radio_handle *out_radio, kgfx_obj_handle *out_root)
{
    kbutton_style style = kui_button_style();

    if (!out_radio || !out_root || !items || item_count == 0u || item_count > KUI_MAX_ITEMS || w == 0u || item_h == 0u)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_RADIOS; ++i)
    {
        kui_radio_slot *slot = &G_radios[i];
        if (slot->used)
            continue;

        kui_radio_reset_slot(slot);
        slot->used = 1u;
        slot->enabled = 1u;
        slot->parent = parent;
        slot->item_count = item_count;
        slot->selected = selected < item_count ? selected : 0u;
        slot->item_h = item_h;
        slot->on_change = on_change;
        slot->user = user;

        slot->root = kgfx_obj_add_rect(x, y, w, item_h * item_count, z, black, 1u);
        if (slot->root.idx < 0)
            goto fail;
        kgfx_obj_set_alpha(slot->root, 0u);
        if (parent.idx >= 0 && kgfx_obj_ref(parent))
            kgfx_obj_set_parent(slot->root, parent);

        for (uint32_t j = 0u; j < item_count; ++j)
        {
            kui_copy_text(slot->items[j], sizeof(slot->items[j]), items[j]);
            slot->item_refs[j].owner_idx = (uint8_t)i;
            slot->item_refs[j].item_idx = (uint8_t)j;
            slot->item_buttons[j] = kbutton_add_rect(0, (int32_t)(j * item_h), w, item_h, 1,
                                                     &style, kui_radio_item_click, &slot->item_refs[j]);
            if (slot->item_buttons[j].idx < 0)
                goto fail;
            kgfx_obj_set_parent(kbutton_root(slot->item_buttons[j]), slot->root);
            {
                uint32_t outer_r = item_h >= 18u ? 6u : (item_h / 3u);
                if (outer_r < 3u)
                    outer_r = 3u;
                slot->item_outer[j] = kgfx_obj_add_circle(12, (int32_t)(item_h / 2u), outer_r, 2, black, 1u);
                slot->item_inner[j] = kgfx_obj_add_circle(12, (int32_t)(item_h / 2u), outer_r > 3u ? outer_r - 3u : 1u, 3, steel_blue, 0u);
                if (slot->item_outer[j].idx < 0 || slot->item_inner[j].idx < 0)
                    goto fail;
                kgfx_obj_set_alpha(slot->item_outer[j], 0u);
                kgfx_obj_set_outline(slot->item_outer[j], 1u, white);
                kgfx_obj_set_parent(slot->item_outer[j], kbutton_root(slot->item_buttons[j]));
                kgfx_obj_set_parent(slot->item_inner[j], kbutton_root(slot->item_buttons[j]));
            }
            slot->item_labels[j] = kui_add_label_obj_at(kbutton_root(slot->item_buttons[j]), "", item_h, 1, 28);
        }

        kui_radio_sync(slot);
        out_radio->idx = (int)i;
        *out_root = slot->root;
        return 0;

    fail:
        (void)kui_radio_destroy((kui_radio_handle){(int)i});
        return -1;
    }

    return -1;
}

int kui_radio_destroy(kui_radio_handle radio)
{
    kui_radio_slot *slot = kui_radio_slot_from_handle(radio);
    if (!slot)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_ITEMS; ++i)
    {
        if (slot->item_labels[i].idx >= 0)
            (void)kgfx_obj_destroy(slot->item_labels[i]);
        if (slot->item_inner[i].idx >= 0)
            (void)kgfx_obj_destroy(slot->item_inner[i]);
        if (slot->item_outer[i].idx >= 0)
            (void)kgfx_obj_destroy(slot->item_outer[i]);
        if (slot->item_buttons[i].idx >= 0)
            (void)kbutton_destroy(slot->item_buttons[i]);
    }
    if (slot->root.idx >= 0)
        (void)kgfx_obj_destroy(slot->root);
    kui_radio_reset_slot(slot);
    return 0;
}

kgfx_obj_handle kui_radio_root(kui_radio_handle radio)
{
    kui_radio_slot *slot = kui_radio_slot_from_handle(radio);
    return slot ? slot->root : kui_invalid_obj();
}

int kui_radio_selected(kui_radio_handle radio)
{
    kui_radio_slot *slot = kui_radio_slot_from_handle(radio);
    return slot ? (int)slot->selected : -1;
}

int kui_radio_set_selected(kui_radio_handle radio, uint32_t selected)
{
    kui_radio_slot *slot = kui_radio_slot_from_handle(radio);
    if (!slot || selected >= slot->item_count)
        return -1;
    slot->selected = selected;
    kui_radio_sync(slot);
    return 0;
}

int kui_radio_set_enabled(kui_radio_handle radio, uint32_t enabled)
{
    kui_radio_slot *slot = kui_radio_slot_from_handle(radio);
    if (!slot)
        return -1;
    slot->enabled = enabled ? 1u : 0u;
    for (uint32_t i = 0u; i < slot->item_count; ++i)
        kbutton_set_enabled(slot->item_buttons[i], slot->enabled);
    return 0;
}

static void kui_toggle_make_label(kui_toggle_slot *slot)
{
    if (!slot)
        return;
    kui_copy_text(slot->button_label, sizeof(slot->button_label), slot->checked ? "[x] " : "[ ] ");
    kui_append_text(slot->button_label, sizeof(slot->button_label), slot->label);
    kgfx_text_set(slot->label_obj, slot->button_label);
    kui_fit_label_to_parent(slot->label_obj, slot->root, slot->button_label, sizeof(slot->button_label));
}

static void kui_toggle_sync(kui_toggle_slot *slot)
{
    kbutton_style normal_style = kui_button_style();
    kbutton_style selected_style = kui_selected_button_style();

    if (!slot || !slot->used)
        return;

    kbutton_set_style(slot->button, slot->checked ? &selected_style : &normal_style);
    kui_toggle_make_label(slot);
}

static void kui_toggle_click(kbutton_handle button, void *user)
{
    kui_toggle_slot *slot = (kui_toggle_slot *)user;
    (void)button;

    if (!slot || !slot->used || !slot->enabled)
        return;

    slot->checked = slot->checked ? 0u : 1u;
    kui_toggle_sync(slot);
    if (slot->on_change)
        slot->on_change((uint32_t)(slot - G_toggles) + 1u, slot->checked ? 1 : 0, slot->user);
}

int kui_toggle_create(kgfx_obj_handle parent, int32_t x, int32_t y, uint32_t w, uint32_t h,
                      int32_t z, const char *label, uint32_t checked,
                      kui_on_change_fn on_change, void *user,
                      kui_toggle_handle *out_toggle, kgfx_obj_handle *out_root)
{
    kbutton_style style = kui_button_style();

    if (!out_toggle || !out_root || w == 0u || h == 0u)
        return -1;

    for (uint32_t i = 0u; i < KUI_MAX_TOGGLES; ++i)
    {
        kui_toggle_slot *slot = &G_toggles[i];
        if (slot->used)
            continue;

        kui_toggle_reset_slot(slot);
        slot->used = 1u;
        slot->enabled = 1u;
        slot->checked = checked ? 1u : 0u;
        slot->parent = parent;
        slot->on_change = on_change;
        slot->user = user;
        kui_copy_text(slot->label, sizeof(slot->label), label);

        slot->button = kbutton_add_rect(x, y, w, h, z, &style, kui_toggle_click, slot);
        slot->root = kbutton_root(slot->button);
        if (slot->button.idx < 0 || slot->root.idx < 0)
            goto fail;
        if (parent.idx >= 0 && kgfx_obj_ref(parent))
            kgfx_obj_set_parent(slot->root, parent);
        slot->label_obj = kui_add_label_obj(slot->root, "", h, 1);
        kui_toggle_sync(slot);

        out_toggle->idx = (int)i;
        *out_root = slot->root;
        return 0;

    fail:
        (void)kui_toggle_destroy((kui_toggle_handle){(int)i});
        return -1;
    }

    return -1;
}

int kui_toggle_destroy(kui_toggle_handle toggle)
{
    kui_toggle_slot *slot = kui_toggle_slot_from_handle(toggle);
    if (!slot)
        return -1;

    if (slot->label_obj.idx >= 0)
        (void)kgfx_obj_destroy(slot->label_obj);
    if (slot->button.idx >= 0)
        (void)kbutton_destroy(slot->button);
    kui_toggle_reset_slot(slot);
    return 0;
}

kgfx_obj_handle kui_toggle_root(kui_toggle_handle toggle)
{
    kui_toggle_slot *slot = kui_toggle_slot_from_handle(toggle);
    return slot ? slot->root : kui_invalid_obj();
}

int kui_toggle_checked(kui_toggle_handle toggle)
{
    kui_toggle_slot *slot = kui_toggle_slot_from_handle(toggle);
    return slot ? (slot->checked ? 1 : 0) : -1;
}

int kui_toggle_set_checked(kui_toggle_handle toggle, uint32_t checked)
{
    kui_toggle_slot *slot = kui_toggle_slot_from_handle(toggle);
    if (!slot)
        return -1;
    slot->checked = checked ? 1u : 0u;
    kui_toggle_sync(slot);
    return 0;
}

int kui_toggle_set_enabled(kui_toggle_handle toggle, uint32_t enabled)
{
    kui_toggle_slot *slot = kui_toggle_slot_from_handle(toggle);
    if (!slot)
        return -1;
    slot->enabled = enabled ? 1u : 0u;
    kbutton_set_enabled(slot->button, slot->enabled);
    return 0;
}

void kui_update_all(void)
{
    kmouse_state mouse = {0};
    uint8_t left_now = 0u;
    uint8_t left_pressed = 0u;

    kmouse_get_state(&mouse);
    left_now = (mouse.buttons & KUI_MOUSE_LEFT) ? 1u : 0u;
    left_pressed = left_now && !(G_prev_buttons & KUI_MOUSE_LEFT);

    if (left_pressed)
    {
        for (uint32_t i = 0u; i < KUI_MAX_DROPDOWNS; ++i)
        {
            kui_dropdown_slot *slot = &G_dropdowns[i];
            if (!slot->used || !slot->open)
                continue;
            if (!kui_point_in_obj(slot->root, mouse.x, mouse.y) &&
                !kui_point_in_obj(slot->panel, mouse.x, mouse.y))
                kui_dropdown_set_open(slot, 0u);
        }
    }

    G_prev_buttons = mouse.buttons;
}
