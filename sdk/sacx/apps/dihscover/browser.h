#pragma once

#include "sacx_api.h"
#include <stdint.h>

#define DIHSCOVER_URL_CAP 768u
#define DIHSCOVER_TITLE_CAP 160u
#define DIHSCOVER_BODY_CAP (2u * 1024u * 1024u)
#define DIHSCOVER_NODE_INITIAL 2048u
#define DIHSCOVER_NODE_MAX 32768u
#define DIHSCOVER_TEXT_INITIAL (512u * 1024u)
#define DIHSCOVER_TEXT_MAX (8u * 1024u * 1024u)
#define DIHSCOVER_PAINT_CAP 96u
#define DIHSCOVER_HISTORY_CAP 32u
#define DIHSCOVER_IMAGE_CAP 12u
#define DIHSCOVER_FORM_BODY_CAP 4096u
#define DIHSCOVER_SCRIPT_CAP 32u

enum browser_tag {
    B_TAG_DOCUMENT, B_TAG_DIV, B_TAG_P, B_TAG_H1, B_TAG_H2, B_TAG_H3,
    B_TAG_A, B_TAG_SPAN, B_TAG_IMG, B_TAG_BUTTON, B_TAG_INPUT, B_TAG_LI,
    B_TAG_PRE, B_TAG_STYLE, B_TAG_SCRIPT, B_TAG_TITLE, B_TAG_BR,
    B_TAG_UL, B_TAG_OL, B_TAG_STRONG, B_TAG_EM, B_TAG_NAV, B_TAG_HEADER,
    B_TAG_FOOTER, B_TAG_FORM, B_TAG_LABEL, B_TAG_TABLE, B_TAG_TR, B_TAG_TD,
    B_TAG_HR, B_TAG_HEAD, B_TAG_META, B_TAG_LINK, B_TAG_TEMPLATE, B_TAG_SVG,
    B_TAG_SELECT
};

enum browser_display { B_DISPLAY_BLOCK, B_DISPLAY_INLINE, B_DISPLAY_FLEX, B_DISPLAY_NONE };

typedef struct browser_style {
    sacx_color color;
    sacx_color background;
    uint16_t font_px;
    uint16_t margin_top, margin_right, margin_bottom, margin_left;
    uint16_t padding_top, padding_right, padding_bottom, padding_left;
    uint16_t border_width;
    uint16_t width_px, height_px;
    uint16_t max_width_px, min_width_px, line_height_px, gap_px;
    sacx_color border_color;
    uint32_t mask;
    uint8_t display;
    uint8_t has_background;
    uint8_t underline;
    uint8_t bold;
    uint8_t italic;
    uint8_t text_align;
    uint8_t flex_direction;
    uint8_t width_percent;
    uint8_t max_width_percent;
    uint8_t flex_wrap;
    uint8_t justify_content;
    uint8_t align_items;
    uint8_t position;
    uint8_t overflow_hidden;
    uint8_t nowrap;
    uint8_t margin_auto_left;
    uint8_t margin_auto_right;
} browser_style;

typedef struct browser_node {
    int32_t parent;
    int32_t first_child;
    int32_t last_child;
    int32_t next_sibling;
    uint16_t tag;
    uint16_t flags;
    uint32_t text_off, text_len;
    uint32_t href_off, src_off, id_off, class_off, onclick_off, inline_style_off, rel_off;
    uint32_t name_off, value_off, type_off, action_off, method_off, for_off;
    uint16_t attr_width, attr_height;
    browser_style style;
    int32_t x, y;
    uint32_t w, h;
    int16_t image_slot;
    int16_t reserved;
} browser_node;

typedef struct browser_document {
    browser_node *nodes;
    char *text;
    uint32_t node_count, node_capacity;
    uint32_t text_used, text_capacity, content_height;
    uint32_t generation;
    uint8_t truncated;
    uint8_t reserved[3];
    char url[DIHSCOVER_URL_CAP];
    char base_url[DIHSCOVER_URL_CAP];
    char title[DIHSCOVER_TITLE_CAP];
} browser_document;

typedef struct browser_history_entry {
    char url[DIHSCOVER_URL_CAP];
    int32_t scroll_y;
} browser_history_entry;

typedef struct browser_image {
    uint8_t used, loading, failed, reserved;
    uint32_t node_index, request, image, object, generation;
    char url[DIHSCOVER_URL_CAP];
} browser_image;

typedef struct browser_paint {
    uint32_t object;
    uint32_t underline_object;
    uint32_t underline_object2;
    uint32_t underline_object3;
    uint32_t background_object;
    int32_t node;
    uint8_t kind;
    uint8_t active;
    char text[256];
} browser_paint;

uint32_t b_strlen(const char *s);
int b_streq(const char *a, const char *b);
int b_starts(const char *s, const char *prefix);
void b_copy(char *dst, uint32_t cap, const char *src);
void b_copy_n(char *dst, uint32_t cap, const char *src, uint32_t n);
void *b_memset(void *dst, int value, __SIZE_TYPE__ count);
void *b_memcpy(void *dst, const void *src, __SIZE_TYPE__ count);
int browser_heap_init(void);
void browser_heap_set_api(const sacx_api *api);
void browser_heap_reset(void);
void *browser_heap_alloc(uint32_t size);
void *browser_heap_realloc(void *ptr, uint32_t size);
void browser_heap_free(void *ptr);
int browser_url_resolve(const char *base, const char *relative, char *out, uint32_t cap);
int browser_url_normalize(const char *input, char *out, uint32_t cap);
int browser_url_unwrap_navigation(const char *url, char *out, uint32_t cap);

void browser_document_reset(browser_document *doc, uint32_t generation, const char *url);
int browser_document_init(browser_document *doc);
void browser_document_release(browser_document *doc);
void browser_document_set_api(const sacx_api *api);
int browser_document_parse(browser_document *doc, const char *html, uint32_t size);
int browser_document_apply_stylesheet(browser_document *doc, const char *css, uint32_t size);
void browser_document_apply_site_defaults(browser_document *doc);
void browser_document_layout(browser_document *doc, uint32_t viewport_w);
int browser_document_make_readable(browser_document *doc);
int browser_document_hit_test(const browser_document *doc, int32_t x, int32_t y);
int browser_document_activate(browser_document *doc, uint32_t node_index, uint32_t *out_form_index);
int browser_document_encode_form(const browser_document *doc, uint32_t form_index,
                                 char *action, uint32_t action_cap, char *method, uint32_t method_cap,
                                 char *body, uint32_t body_cap);
const char *browser_document_string(const browser_document *doc, uint32_t off);
int browser_document_set_text_by_id(browser_document *doc, const char *id, const char *value);
int browser_document_set_style_by_id(browser_document *doc, const char *id, const char *property, const char *value);
uint32_t browser_text_scale(uint16_t font_px);
uint32_t browser_text_wrap(const browser_style *style, const char *src, uint32_t width,
                           char *out, uint32_t cap, uint32_t *out_width);

int browser_scripts_start(browser_document *doc, uint64_t now_ticks);
int browser_scripts_pump(browser_document *doc, uint64_t now_ticks, uint32_t budget);
int browser_scripts_click(browser_document *doc, uint32_t node_index);
void browser_scripts_stop(void);
