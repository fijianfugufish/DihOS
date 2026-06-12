#pragma once

#include <stddef.h>
#include <stdint.h>
#include "sacx_api.h"

#define EDITOR_MAX_LAYERS 128u
#define EDITOR_MAX_RASTER_LAYERS 16u
#define EDITOR_MAX_POINTS 32768u
#define EDITOR_MAX_HISTORY 100u
#define EDITOR_TEXT_CAP 96u
#define EDITOR_PATH_CAP 256u

#define SACX_API_HAS(api, member) \
    ((api) && (api)->struct_size >= offsetof(sacx_api, member) + sizeof((api)->member) && (api)->member)

enum editor_layer_type
{
    EDITOR_LAYER_RASTER = 1,
    EDITOR_LAYER_TEXT,
    EDITOR_LAYER_ARROW,
    EDITOR_LAYER_RECT,
    EDITOR_LAYER_ELLIPSE,
    EDITOR_LAYER_PEN,
    EDITOR_LAYER_HIGHLIGHTER,
    EDITOR_LAYER_BLUR,
    EDITOR_LAYER_PIXELATE,
    EDITOR_LAYER_REDACT,
    EDITOR_LAYER_ADJUSTMENT,
};

enum editor_tool
{
    EDITOR_TOOL_SELECT = 0,
    EDITOR_TOOL_CROP,
    EDITOR_TOOL_TEXT,
    EDITOR_TOOL_ARROW,
    EDITOR_TOOL_RECT,
    EDITOR_TOOL_ELLIPSE,
    EDITOR_TOOL_PEN,
    EDITOR_TOOL_HIGHLIGHTER,
    EDITOR_TOOL_BLUR,
    EDITOR_TOOL_PIXELATE,
    EDITOR_TOOL_REDACT,
    EDITOR_TOOL_ADJUSTMENT,
    EDITOR_TOOL_COUNT,
};

typedef struct editor_point
{
    int32_t x;
    int32_t y;
} editor_point;

typedef struct editor_layer
{
    uint32_t type;
    uint32_t image;
    uint32_t *pixels;
    uint32_t stride;
    uint32_t raster_w;
    uint32_t raster_h;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint32_t point_start;
    uint32_t point_count;
    uint32_t color;
    uint32_t stroke;
    int32_t brightness;
    int32_t contrast;
    int32_t saturation;
    uint32_t effect_strength;
    uint8_t visible;
    uint8_t locked;
    uint8_t opacity;
    uint8_t grayscale;
    char text[EDITOR_TEXT_CAP];
} editor_layer;

typedef struct editor_snapshot
{
    editor_layer layers[EDITOR_MAX_LAYERS];
    uint32_t layer_count;
    int32_t selected_layer;
    uint32_t point_count;
    int32_t crop_x;
    int32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    uint32_t resize_w;
    uint32_t resize_h;
    uint32_t rotation;
    uint8_t flip_x;
    uint8_t flip_y;
} editor_snapshot;

typedef struct editor_document
{
    editor_layer layers[EDITOR_MAX_LAYERS];
    editor_point points[EDITOR_MAX_POINTS];
    editor_snapshot history[EDITOR_MAX_HISTORY];
    uint32_t layer_count;
    int32_t selected_layer;
    uint32_t point_count;
    uint32_t canvas_w;
    uint32_t canvas_h;
    int32_t crop_x;
    int32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    uint32_t resize_w;
    uint32_t resize_h;
    uint32_t rotation;
    uint8_t flip_x;
    uint8_t flip_y;
    uint32_t preview;
    uint32_t scratch;
    uint32_t *preview_pixels;
    uint32_t *scratch_pixels;
    uint32_t preview_stride;
    uint32_t scratch_stride;
    uint32_t render_layer;
    uint32_t render_row;
    int32_t dirty_x0;
    int32_t dirty_y0;
    int32_t dirty_x1;
    int32_t dirty_y1;
    uint8_t dirty_bounds_valid;
    uint8_t render_pending;
    uint8_t dirty;
    uint32_t history_count;
    uint32_t history_pos;
    char project_raw[EDITOR_PATH_CAP];
    char project_friendly[EDITOR_PATH_CAP];
    char source_raw[EDITOR_PATH_CAP];
    char source_friendly[EDITOR_PATH_CAP];
} editor_document;

extern const sacx_api *g_api;
extern editor_document g_doc;

void editor_copy_text(char *dst, uint32_t cap, const char *src);
void editor_zero_memory(void *memory, uint64_t size);
void editor_append_text(char *dst, uint32_t cap, const char *src);
void editor_append_uint(char *dst, uint32_t cap, uint32_t value);
int editor_path_has_extension(const char *path, const char *extension);
void editor_split_path(const char *path, char *dir, uint32_t dir_cap, char *name, uint32_t name_cap);

void document_reset(void);
void document_dispose(editor_document *document);
int document_open_image(uint32_t image, const char *raw_path, const char *friendly_path);
int document_add_raster(uint32_t image, int32_t x, int32_t y);
int document_add_layer(uint32_t type);
void document_delete_selected(void);
void document_move_selected(int direction);
void document_commit(void);
void document_history_reset(void);
int document_undo(void);
int document_redo(void);
void document_request_render(void);
void document_request_render_rect(int32_t x, int32_t y, uint32_t w, uint32_t h);
int document_render_step(uint32_t row_budget);
void document_render_now(void);
int document_export_flattened(uint32_t *out_image);
int document_export_begin(uint32_t *out_image, uint32_t *out_w, uint32_t *out_h);
int document_export_step(uint32_t image, uint32_t first_row, uint32_t row_count);
int document_hit_test(int32_t x, int32_t y);
uint32_t document_raster_count(void);
const char *document_layer_name(uint32_t type);

int project_save(const char *raw_path, const char *friendly_path);
int project_load(const char *raw_path, const char *friendly_path);
int project_export(const char *raw_path, uint32_t format, uint32_t quality);
int project_export_begin_async(const char *raw_path, uint32_t format, uint32_t quality);
int project_export_step_async(uint32_t row_budget, uint32_t *out_progress);
int project_export_active(void);
