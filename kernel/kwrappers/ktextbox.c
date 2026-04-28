#include "kwrappers/ktextbox.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"

#ifndef KTEXTBOX_MAX
#define KTEXTBOX_MAX 32
#endif

#define KTEXTBOX_TEXT_CAP 256
#define KTEXTBOX_DISPLAY_CAP (KTEXTBOX_TEXT_CAP + 2)
#define KTEXTBOX_MOUSE_LEFT 0x01u
#define KTEXTBOX_CARET_BLINK_FRAMES 30u
#define KTEXTBOX_KEY_REPEAT_DELAY 30u
#define KTEXTBOX_KEY_REPEAT_INTERVAL 3u

typedef struct
{
    int32_t x0, y0, x1, y1;
    uint8_t enabled;
} ktextbox_clip_rect;

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t z;
    ktextbox_clip_rect clip;
    uint8_t valid;
} ktextbox_resolved_rect;

typedef struct
{
    uint8_t used;
    uint8_t enabled;
    uint8_t hovered;
    uint8_t focused;
    kgfx_obj_handle root;
    kgfx_obj_handle text_obj;
    const kfont *font;
    ktextbox_style style;
    ktextbox_on_submit_fn on_submit;
    void *user;
    uint16_t len;
    uint16_t caret;
    uint16_t view_start;
    char text[KTEXTBOX_TEXT_CAP];
    char display[KTEXTBOX_DISPLAY_CAP];
} ktextbox_slot;

static ktextbox_slot G_boxes[KTEXTBOX_MAX];
static uint8_t G_prev_buttons = 0;
static int G_focused_idx = -1;
static uint8_t G_caps_lock = 0;
static uint32_t G_caret_blink_tick = 0;
static uint8_t G_repeat_usage = 0;
static uint32_t G_repeat_frames = 0;

static const uint8_t G_printable_usages[] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_SPACE, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH,
    KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_DOT, KEY_SLASH,
    KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
    KEY_KP_DOT, KEY_KP_PLUS, KEY_KP_MINUS, KEY_KP_ASTERISK, KEY_KP_SLASH
};

static inline int32_t ktextbox_max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline int32_t ktextbox_min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static inline int ktextbox_clip_intersect(ktextbox_clip_rect *dst, const ktextbox_clip_rect *other)
{
    if (!dst || !other || !dst->enabled || !other->enabled)
        return 1;

    dst->x0 = ktextbox_max_i32(dst->x0, other->x0);
    dst->y0 = ktextbox_max_i32(dst->y0, other->y0);
    dst->x1 = ktextbox_min_i32(dst->x1, other->x1);
    dst->y1 = ktextbox_min_i32(dst->y1, other->y1);
    return dst->x0 < dst->x1 && dst->y0 < dst->y1;
}

static void ktextbox_obj_local_origin(const kgfx_obj *o, int32_t *x, int32_t *y)
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

static int ktextbox_parent_clip_bounds(const kgfx_obj *o, int32_t world_x, int32_t world_y, ktextbox_clip_rect *clip)
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

static int ktextbox_resolve_obj(kgfx_obj_handle h, ktextbox_resolved_rect *out, uint32_t depth)
{
    const kfb *fb = 0;
    kgfx_obj *o = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    ktextbox_resolved_rect parent = {0};
    ktextbox_clip_rect parent_bounds = {0};
    kgfx_obj_handle parent_handle;

    if (!out || depth > 32u)
        return 0;

    o = kgfx_obj_ref(h);
    fb = kgfx_info();
    if (!o || !fb)
        return 0;

    ktextbox_obj_local_origin(o, &local_x, &local_y);

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
    if (!ktextbox_resolve_obj(parent_handle, &parent, depth + 1u))
        return 0;

    out->x += parent.x;
    out->y += parent.y;
    out->z += parent.z;
    out->clip = parent.clip;

    if (o->clip_to_parent && ktextbox_parent_clip_bounds(kgfx_obj_ref(parent_handle), parent.x, parent.y, &parent_bounds))
    {
        if (!ktextbox_clip_intersect(&out->clip, &parent_bounds))
            return 0;
    }

    return 1;
}

static int ktextbox_resolve_root_bounds(const ktextbox_slot *slot, ktextbox_resolved_rect *out)
{
    kgfx_obj *o = 0;

    if (!slot || !slot->used || !slot->enabled || !out)
        return 0;

    o = kgfx_obj_ref(slot->root);
    if (!o || !o->visible || o->kind != KGFX_OBJ_RECT)
        return 0;

    if (!ktextbox_resolve_obj(slot->root, out, 0))
        return 0;

    out->clip.x0 = ktextbox_max_i32(out->clip.x0, out->x);
    out->clip.y0 = ktextbox_max_i32(out->clip.y0, out->y);
    out->clip.x1 = ktextbox_min_i32(out->clip.x1, out->x + (int32_t)o->u.rect.w);
    out->clip.y1 = ktextbox_min_i32(out->clip.y1, out->y + (int32_t)o->u.rect.h);

    return out->clip.x0 < out->clip.x1 && out->clip.y0 < out->clip.y1;
}

static void ktextbox_insert_char(ktextbox_slot *slot, char ch)
{
    if (!slot || slot->len >= KTEXTBOX_TEXT_CAP - 1 || slot->caret > slot->len)
        return;

    for (uint16_t i = slot->len; i > slot->caret; --i)
        slot->text[i] = slot->text[i - 1];

    slot->text[slot->caret] = ch;
    slot->len++;
    slot->caret++;
    slot->text[slot->len] = 0;
}

static void ktextbox_backspace(ktextbox_slot *slot)
{
    if (!slot || slot->len == 0 || slot->caret == 0 || slot->caret > slot->len)
        return;

    for (uint16_t i = slot->caret - 1; i < slot->len; ++i)
        slot->text[i] = slot->text[i + 1];

    slot->text[slot->len] = 0;
    slot->len--;
    slot->caret--;
}

static void ktextbox_delete(ktextbox_slot *slot)
{
    if (!slot || slot->caret >= slot->len)
        return;

    for (uint16_t i = slot->caret; i < slot->len; ++i)
        slot->text[i] = slot->text[i + 1];

    slot->text[slot->len] = 0;
    slot->len--;
}

static uint32_t ktextbox_char_width(const ktextbox_slot *slot, char ch)
{
    char glyph[2];
    uint32_t scale = 1u;

    if (!slot || !slot->font)
        return 0u;

    glyph[0] = ch;
    glyph[1] = 0;
    scale = slot->style.text_scale ? slot->style.text_scale : 1u;
    return ktext_measure_line_px(slot->font, glyph, scale, 0);
}

static uint32_t ktextbox_caret_width(const ktextbox_slot *slot)
{
    uint32_t scale = 1u;

    if (!slot || !slot->font || !slot->focused)
        return 0u;

    scale = slot->style.text_scale ? slot->style.text_scale : 1u;
    return ktext_measure_line_px(slot->font, "|", scale, 1);
}

static uint8_t ktextbox_caret_visible(const ktextbox_slot *slot)
{
    if (!slot || !slot->focused)
        return 0u;

    return ((G_caret_blink_tick / KTEXTBOX_CARET_BLINK_FRAMES) & 1u) == 0u ? 1u : 0u;
}

static void ktextbox_reset_caret_blink(void)
{
    G_caret_blink_tick = 0;
}

static void ktextbox_clear_repeat_state(void)
{
    G_repeat_usage = 0;
    G_repeat_frames = 0;
}

static uint8_t ktextbox_key_repeat_trigger(uint8_t usage)
{
    if (kinput_key_pressed(usage))
    {
        G_repeat_usage = usage;
        G_repeat_frames = 0;
        return 1u;
    }

    if (G_repeat_usage != usage)
        return 0u;

    if (!kinput_key_down(usage))
    {
        ktextbox_clear_repeat_state();
        return 0u;
    }

    G_repeat_frames++;
    if (G_repeat_frames < KTEXTBOX_KEY_REPEAT_DELAY)
        return 0u;

    return ((G_repeat_frames - KTEXTBOX_KEY_REPEAT_DELAY) % KTEXTBOX_KEY_REPEAT_INTERVAL) == 0u ? 1u : 0u;
}

static uint8_t ktextbox_shift_down(void)
{
    return (kinput_key_down(KEY_LSHIFT) || kinput_key_down(KEY_RSHIFT)) ? 1u : 0u;
}

static char ktextbox_usage_to_char(uint8_t usage, uint8_t shift, uint8_t caps_lock)
{
    if (usage >= KEY_A && usage <= KEY_Z)
    {
        char ch = (char)('a' + (usage - KEY_A));
        if ((shift ? 1u : 0u) ^ (caps_lock ? 1u : 0u))
            ch = (char)(ch - ('a' - 'A'));
        return ch;
    }

    switch (usage)
    {
    case KEY_1: return shift ? '!' : '1';
    case KEY_2: return shift ? '@' : '2';
    case KEY_3: return shift ? '#' : '3';
    case KEY_4: return shift ? '$' : '4';
    case KEY_5: return shift ? '%' : '5';
    case KEY_6: return shift ? '^' : '6';
    case KEY_7: return shift ? '&' : '7';
    case KEY_8: return shift ? '*' : '8';
    case KEY_9: return shift ? '(' : '9';
    case KEY_0: return shift ? ')' : '0';
    case KEY_SPACE: return ' ';
    case KEY_MINUS: return shift ? '_' : '-';
    case KEY_EQUAL: return shift ? '+' : '=';
    case KEY_LEFTBRACE: return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_BACKSLASH: return shift ? '|' : '\\';
    case KEY_SEMICOLON: return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE: return shift ? '~' : '`';
    case KEY_COMMA: return shift ? '<' : ',';
    case KEY_DOT: return shift ? '>' : '.';
    case KEY_SLASH: return shift ? '?' : '/';
    case KEY_KP_0: return '0';
    case KEY_KP_1: return '1';
    case KEY_KP_2: return '2';
    case KEY_KP_3: return '3';
    case KEY_KP_4: return '4';
    case KEY_KP_5: return '5';
    case KEY_KP_6: return '6';
    case KEY_KP_7: return '7';
    case KEY_KP_8: return '8';
    case KEY_KP_9: return '9';
    case KEY_KP_DOT: return '.';
    case KEY_KP_PLUS: return '+';
    case KEY_KP_MINUS: return '-';
    case KEY_KP_ASTERISK: return '*';
    case KEY_KP_SLASH: return '/';
    default:
        return 0;
    }
}

static void ktextbox_apply_visual(ktextbox_slot *slot)
{
    kgfx_obj *root = 0;
    kgfx_obj *text = 0;
    kcolor fill = black;
    kcolor outline = white;
    kcolor text_color = white;
    uint32_t text_h = 0;

    if (!slot || !slot->used)
        return;

    root = kgfx_obj_ref(slot->root);
    text = kgfx_obj_ref(slot->text_obj);
    if (!root || root->kind != KGFX_OBJ_RECT)
        return;

    fill = slot->style.fill;
    outline = slot->style.outline;
    text_color = slot->style.text_color;

    if (!slot->enabled)
    {
        fill = dim_gray;
        outline = slate_gray;
        text_color = gainsboro;
    }
    else if (slot->focused)
    {
        fill = slot->style.focus_fill;
        outline = slot->style.focus_outline;
    }
    else if (slot->hovered)
    {
        fill = slot->style.hover_fill;
    }

    root->fill = fill;
    root->alpha = slot->style.alpha;
    root->outline = outline;
    root->outline_alpha = slot->style.outline_alpha;
    root->outline_width = slot->style.outline_width;

    if (!text || text->kind != KGFX_OBJ_TEXT)
        return;

    text->u.text.font = slot->font;
    text->u.text.text = slot->display;
    text->u.text.x = (int32_t)slot->style.padding_x;
    text->u.text.scale = slot->style.text_scale ? slot->style.text_scale : 1u;
    text->u.text.char_spacing = 1;
    text->u.text.line_spacing = 0;
    text->u.text.align = KTEXT_ALIGN_LEFT;
    text->fill = text_color;
    text->alpha = 255;
    text->visible = root->visible ? 1u : 0u;
    text->clip_to_parent = 1;
    text->outline_width = 0;

    text_h = slot->font ? ktext_scale_mul_px(slot->font->h, text->u.text.scale) : 0u;
    text->u.text.y = (int32_t)((root->u.rect.h > text_h)
                                   ? (root->u.rect.h - text_h) / 2u
                                   : 0u);
}

static void ktextbox_sync_display(ktextbox_slot *slot)
{
    kgfx_obj *root = 0;
    uint32_t available_px = 0;
    uint32_t caret_px = 0;
    uint32_t used_px = 0;
    uint16_t start = 0;
    uint16_t src = 0;
    uint16_t out_len = 0;
    uint8_t caret_drawn = 0;
    uint8_t caret_visible = 0;

    if (!slot || !slot->used)
        return;

    root = kgfx_obj_ref(slot->root);
    if (root && root->kind == KGFX_OBJ_RECT)
    {
        int32_t inner = (int32_t)root->u.rect.w - ((int32_t)slot->style.padding_x * 2);
        if (inner > 0)
            available_px = (uint32_t)inner;
    }

    caret_px = ktextbox_caret_width(slot);
    caret_visible = ktextbox_caret_visible(slot);

    if (slot->font && available_px > 0u)
    {
        start = slot->view_start;
        if (start > slot->caret)
            start = slot->caret;

        while (start < slot->caret)
        {
            uint32_t width = caret_px;

            for (uint16_t i = start; i < slot->caret; ++i)
                width += ktextbox_char_width(slot, slot->text[i]) + 1u;

            if (width <= available_px)
                break;
            start++;
        }
    }

    slot->view_start = start;
    src = start;

    for (;;)
    {
        if (caret_visible && !caret_drawn && src == slot->caret && out_len < KTEXTBOX_DISPLAY_CAP - 1)
        {
            if (available_px > 0u && out_len > 0 && used_px + caret_px > available_px)
                break;

            slot->display[out_len++] = '|';
            caret_drawn = 1u;
            used_px += caret_px;
        }

        if (src >= slot->len || out_len >= KTEXTBOX_DISPLAY_CAP - 1)
            break;

        if (slot->font && available_px > 0u)
        {
            uint32_t ch_px = ktextbox_char_width(slot, slot->text[src]) + 1u;

            if (out_len > 0 && used_px + ch_px > available_px)
                break;

            used_px += ch_px;
        }

        slot->display[out_len++] = slot->text[src++];
    }

    if (caret_visible && !caret_drawn && out_len < KTEXTBOX_DISPLAY_CAP - 1)
    {
        if (available_px == 0u || out_len == 0 || used_px + caret_px <= available_px)
            slot->display[out_len++] = '|';
    }

    slot->display[out_len] = 0;
    ktextbox_apply_visual(slot);
}

static void ktextbox_place_caret_from_mouse(ktextbox_slot *slot, const ktextbox_resolved_rect *resolved, int32_t mouse_x)
{
    kgfx_obj *root = 0;
    int32_t local_x = 0;
    int32_t advance_x = 0;
    uint32_t caret_px = 0;
    uint16_t idx = 0;

    if (!slot || !resolved || !resolved->valid)
        return;

    root = kgfx_obj_ref(slot->root);
    if (!root || root->kind != KGFX_OBJ_RECT)
        return;

    local_x = mouse_x - resolved->x - (int32_t)slot->style.padding_x;
    if (local_x <= 0)
    {
        slot->caret = slot->view_start;
        ktextbox_reset_caret_blink();
        ktextbox_sync_display(slot);
        return;
    }

    caret_px = ktextbox_caret_width(slot);
    idx = slot->view_start;
    for (;;)
    {
        if (slot->focused && idx == slot->caret && caret_px > 0u)
        {
            int32_t caret_boundary = advance_x + (int32_t)(caret_px / 2u);

            if (local_x < caret_boundary)
            {
                slot->caret = idx;
                ktextbox_reset_caret_blink();
                ktextbox_sync_display(slot);
                return;
            }

            advance_x += (int32_t)caret_px;
        }

        if (idx >= slot->len)
            break;

        uint32_t ch_w = ktextbox_char_width(slot, slot->text[idx]);
        int32_t boundary = advance_x + (int32_t)(ch_w / 2u);

        if (local_x < boundary)
        {
            slot->caret = idx;
            ktextbox_reset_caret_blink();
            ktextbox_sync_display(slot);
            return;
        }

        advance_x += (int32_t)ch_w + 1;
        idx++;
    }

    slot->caret = slot->len;
    ktextbox_reset_caret_blink();
    ktextbox_sync_display(slot);
}

static void ktextbox_focus_slot(int idx)
{
    if (G_focused_idx >= 0 && G_focused_idx < KTEXTBOX_MAX && G_boxes[G_focused_idx].used)
    {
        G_boxes[G_focused_idx].focused = 0;
        ktextbox_clear_repeat_state();
        ktextbox_sync_display(&G_boxes[G_focused_idx]);
    }

    G_focused_idx = -1;

    if (idx >= 0 && idx < KTEXTBOX_MAX && G_boxes[idx].used && G_boxes[idx].enabled)
    {
        G_focused_idx = idx;
        G_boxes[idx].focused = 1;
        ktextbox_clear_repeat_state();
        ktextbox_reset_caret_blink();
        ktextbox_sync_display(&G_boxes[idx]);
    }
}

void ktextbox_init(void)
{
    for (uint32_t i = 0; i < KTEXTBOX_MAX; ++i)
        G_boxes[i] = (ktextbox_slot){0};

    G_prev_buttons = 0;
    G_focused_idx = -1;
    G_caps_lock = 0;
    G_caret_blink_tick = 0;
    ktextbox_clear_repeat_state();
}

ktextbox_handle ktextbox_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                  int32_t z, const kfont *font,
                                  const ktextbox_style *style,
                                  ktextbox_on_submit_fn on_submit, void *user)
{
    ktextbox_handle hnd = {-1};
    ktextbox_style resolved_style = ktextbox_style_default();
    kgfx_obj_handle root = {-1};
    kgfx_obj_handle text = {-1};

    if (!font)
        return hnd;

    if (style)
        resolved_style = *style;
    if (resolved_style.text_scale == 0u)
        resolved_style.text_scale = 1u;

    for (uint32_t i = 0; i < KTEXTBOX_MAX; ++i)
    {
        if (G_boxes[i].used)
            continue;

        root = kgfx_obj_add_rect(x, y, w, h, z, resolved_style.fill, 1);
        if (root.idx < 0)
            return hnd;

        text = kgfx_obj_add_text(font, "",
                                 (int32_t)resolved_style.padding_x,
                                 (int32_t)resolved_style.padding_y,
                                 1,
                                 resolved_style.text_color,
                                 255,
                                 resolved_style.text_scale,
                                 1,
                                 0,
                                 KTEXT_ALIGN_LEFT,
                                 1);
        if (text.idx < 0)
        {
            kgfx_obj_destroy(root);
            return hnd;
        }

        kgfx_obj_set_parent(text, root);

        G_boxes[i] = (ktextbox_slot){0};
        G_boxes[i].used = 1;
        G_boxes[i].enabled = 1;
        G_boxes[i].root = root;
        G_boxes[i].text_obj = text;
        G_boxes[i].font = font;
        G_boxes[i].style = resolved_style;
        G_boxes[i].on_submit = on_submit;
        G_boxes[i].user = user;
        G_boxes[i].caret = 0;
        G_boxes[i].view_start = 0;
        G_boxes[i].text[0] = 0;
        G_boxes[i].display[0] = 0;
        ktextbox_sync_display(&G_boxes[i]);

        hnd.idx = (int)i;
        return hnd;
    }

    return hnd;
}

int ktextbox_destroy(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return -1;

    if (G_focused_idx == h.idx)
        G_focused_idx = -1;

    if (G_boxes[h.idx].text_obj.idx >= 0)
        kgfx_obj_destroy(G_boxes[h.idx].text_obj);
    if (G_boxes[h.idx].root.idx >= 0)
        kgfx_obj_destroy(G_boxes[h.idx].root);

    G_boxes[h.idx] = (ktextbox_slot){0};
    return 0;
}

void ktextbox_update_all(void)
{
    kmouse_state mouse = {0};
    uint8_t left_now = 0;
    uint8_t left_pressed = 0;
    uint8_t needs_blink_refresh = 0;
    int hovered_idx = -1;
    int32_t hovered_z = 0;
    ktextbox_resolved_rect resolved[KTEXTBOX_MAX];

    kmouse_get_state(&mouse);
    G_caret_blink_tick++;
    if (G_focused_idx >= 0)
        needs_blink_refresh = (G_caret_blink_tick % KTEXTBOX_CARET_BLINK_FRAMES) == 0u;

    if (kinput_key_pressed(KEY_CAPSLOCK))
        G_caps_lock = G_caps_lock ? 0u : 1u;

    left_now = (mouse.buttons & KTEXTBOX_MOUSE_LEFT) ? 1u : 0u;
    left_pressed = left_now && !(G_prev_buttons & KTEXTBOX_MOUSE_LEFT);

    for (uint32_t i = 0; i < KTEXTBOX_MAX; ++i)
    {
        int inside = 0;

        resolved[i] = (ktextbox_resolved_rect){0};
        G_boxes[i].hovered = 0;

        if (!ktextbox_resolve_root_bounds(&G_boxes[i], &resolved[i]))
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
    {
        G_boxes[hovered_idx].hovered = 1;
        (void)kmouse_set_cursor(KMOUSE_CURSOR_BEAM);
    }

    if (left_pressed)
    {
        ktextbox_focus_slot(hovered_idx);
        if (hovered_idx >= 0)
            ktextbox_place_caret_from_mouse(&G_boxes[hovered_idx], &resolved[hovered_idx], mouse.x);
    }

    if (G_focused_idx >= 0 && G_focused_idx < KTEXTBOX_MAX && G_boxes[G_focused_idx].used && G_boxes[G_focused_idx].enabled)
    {
        ktextbox_slot *slot = &G_boxes[G_focused_idx];
        uint8_t shift = ktextbox_shift_down();

        if (kinput_key_pressed(KEY_ESCAPE))
        {
            ktextbox_focus_slot(-1);
        }
        else
        {
            if (ktextbox_key_repeat_trigger(KEY_BACKSPACE))
            {
                ktextbox_backspace(slot);
                ktextbox_reset_caret_blink();
            }
            if (ktextbox_key_repeat_trigger(KEY_DELETE))
            {
                ktextbox_delete(slot);
                ktextbox_reset_caret_blink();
            }

            if (ktextbox_key_repeat_trigger(KEY_LEFT) && slot->caret > 0)
            {
                slot->caret--;
                ktextbox_reset_caret_blink();
            }
            if (ktextbox_key_repeat_trigger(KEY_RIGHT) && slot->caret < slot->len)
            {
                slot->caret++;
                ktextbox_reset_caret_blink();
            }
            if (ktextbox_key_repeat_trigger(KEY_HOME))
            {
                slot->caret = 0;
                ktextbox_reset_caret_blink();
            }
            if (ktextbox_key_repeat_trigger(KEY_END))
            {
                slot->caret = slot->len;
                ktextbox_reset_caret_blink();
            }

            if (kinput_key_pressed(KEY_ENTER) || kinput_key_pressed(KEY_KP_ENTER))
            {
                if (slot->on_submit)
                    slot->on_submit((ktextbox_handle){G_focused_idx}, slot->text, slot->user);

                if (G_focused_idx >= 0 && G_focused_idx < KTEXTBOX_MAX && G_boxes[G_focused_idx].used)
                    slot = &G_boxes[G_focused_idx];
                else
                    slot = 0;
            }

            if (slot)
            {
                for (uint32_t i = 0; i < (uint32_t)(sizeof(G_printable_usages) / sizeof(G_printable_usages[0])); ++i)
                {
                    uint8_t usage = G_printable_usages[i];
                    char ch = 0;

                    if (!ktextbox_key_repeat_trigger(usage))
                        continue;

                    ch = ktextbox_usage_to_char(usage, shift, G_caps_lock);
                    if (ch)
                    {
                        ktextbox_insert_char(slot, ch);
                        ktextbox_reset_caret_blink();
                    }
                }

                ktextbox_sync_display(slot);
            }
        }
    }
    else if (needs_blink_refresh)
    {
        for (uint32_t i = 0; i < KTEXTBOX_MAX; ++i)
        {
            if (G_boxes[i].used && G_boxes[i].focused)
                ktextbox_sync_display(&G_boxes[i]);
        }
    }

    for (uint32_t i = 0; i < KTEXTBOX_MAX; ++i)
        if (G_boxes[i].used)
            ktextbox_apply_visual(&G_boxes[i]);

    G_prev_buttons = mouse.buttons;
}

kgfx_obj_handle ktextbox_root(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return (kgfx_obj_handle){-1};
    return G_boxes[h.idx].root;
}

void ktextbox_set_callback(ktextbox_handle h, ktextbox_on_submit_fn on_submit, void *user)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    G_boxes[h.idx].on_submit = on_submit;
    G_boxes[h.idx].user = user;
}

void ktextbox_set_enabled(ktextbox_handle h, uint8_t enabled)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    enabled = enabled ? 1u : 0u;
    if (G_boxes[h.idx].enabled == enabled)
        return;

    G_boxes[h.idx].enabled = enabled;
    if (!G_boxes[h.idx].enabled && G_focused_idx == h.idx)
        G_focused_idx = -1;
    G_boxes[h.idx].hovered = 0;
    G_boxes[h.idx].focused = 0;
    ktextbox_sync_display(&G_boxes[h.idx]);
}

int ktextbox_enabled(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return 0;
    return G_boxes[h.idx].enabled != 0;
}

void ktextbox_set_focus(ktextbox_handle h, uint8_t focused)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    if (focused)
        ktextbox_focus_slot(h.idx);
    else if (G_focused_idx == h.idx)
        ktextbox_focus_slot(-1);
}

int ktextbox_focused(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return 0;
    return G_boxes[h.idx].focused != 0;
}

void ktextbox_set_bounds(ktextbox_handle h, int32_t x, int32_t y, uint32_t w, uint32_t h_px)
{
    kgfx_obj *root = 0;

    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    root = kgfx_obj_ref(G_boxes[h.idx].root);
    if (!root || root->kind != KGFX_OBJ_RECT)
        return;

    root->u.rect.x = x;
    root->u.rect.y = y;
    root->u.rect.w = w;
    root->u.rect.h = h_px;
    ktextbox_sync_display(&G_boxes[h.idx]);
}

void ktextbox_set_font(ktextbox_handle h, const kfont *font)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used || !font)
        return;

    G_boxes[h.idx].font = font;
    ktextbox_sync_display(&G_boxes[h.idx]);
}

void ktextbox_set_text(ktextbox_handle h, const char *text)
{
    uint16_t len = 0;

    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    if (!text)
        text = "";

    while (text[len] && len < KTEXTBOX_TEXT_CAP - 1)
    {
        G_boxes[h.idx].text[len] = text[len];
        len++;
    }

    G_boxes[h.idx].text[len] = 0;
    G_boxes[h.idx].len = len;
    G_boxes[h.idx].caret = len;
    G_boxes[h.idx].view_start = 0;
    ktextbox_clear_repeat_state();
    ktextbox_reset_caret_blink();
    ktextbox_sync_display(&G_boxes[h.idx]);
}

void ktextbox_clear(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return;

    G_boxes[h.idx].len = 0;
    G_boxes[h.idx].caret = 0;
    G_boxes[h.idx].view_start = 0;
    G_boxes[h.idx].text[0] = 0;
    ktextbox_sync_display(&G_boxes[h.idx]);
}

const char *ktextbox_text(ktextbox_handle h)
{
    if (h.idx < 0 || h.idx >= KTEXTBOX_MAX || !G_boxes[h.idx].used)
        return "";
    return G_boxes[h.idx].text;
}
