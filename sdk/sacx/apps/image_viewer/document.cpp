#include "editor.h"

const sacx_api *g_api = 0;
editor_document g_doc;

void editor_zero_memory(void *memory, uint64_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (uint64_t i = 0u; i < size; ++i)
        bytes[i] = 0u;
}

static uint8_t lower_ascii(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(c + ('a' - 'A'));
    return c;
}

void editor_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;
    if (!dst || !cap)
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

void editor_append_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0u;
    if (!dst || !cap)
        return;
    while (len < cap && dst[len])
        ++len;
    if (len < cap)
        editor_copy_text(dst + len, cap - len, src);
}

void editor_append_uint(char *dst, uint32_t cap, uint32_t value)
{
    char reverse[12];
    uint32_t count = 0u;
    if (!value)
    {
        editor_append_text(dst, cap, "0");
        return;
    }
    while (value && count < sizeof(reverse))
    {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count)
    {
        char one[2] = {reverse[--count], 0};
        editor_append_text(dst, cap, one);
    }
}

int editor_path_has_extension(const char *path, const char *extension)
{
    uint32_t path_len = 0u;
    uint32_t ext_len = 0u;
    if (!path || !extension)
        return 0;
    while (path[path_len])
        ++path_len;
    while (extension[ext_len])
        ++ext_len;
    if (!ext_len || path_len < ext_len)
        return 0;
    for (uint32_t i = 0u; i < ext_len; ++i)
    {
        if (lower_ascii((uint8_t)path[path_len - ext_len + i]) != lower_ascii((uint8_t)extension[i]))
            return 0;
    }
    return 1;
}

void editor_split_path(const char *path, char *dir, uint32_t dir_cap, char *name, uint32_t name_cap)
{
    uint32_t len = 0u;
    uint32_t slash = 0u;
    if (!path)
        path = "/";
    while (path[len])
    {
        if (path[len] == '/' || path[len] == '\\')
            slash = len;
        ++len;
    }
    if (dir && dir_cap)
    {
        uint32_t copy = slash ? slash : 1u;
        if (copy >= dir_cap)
            copy = dir_cap - 1u;
        for (uint32_t i = 0u; i < copy; ++i)
            dir[i] = path[i];
        dir[copy] = 0;
    }
    if (name && name_cap)
        editor_copy_text(name, name_cap, path + (slash ? slash + 1u : 0u));
}

static uint8_t clamp_u8(int32_t value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return (uint8_t)value;
}

static uint32_t blend_pixel(uint32_t dst, uint32_t src, uint8_t opacity)
{
    uint32_t sa = ((src >> 24) * opacity + 127u) / 255u;
    uint32_t da = dst >> 24;
    uint32_t inv = 255u - sa;
    uint32_t out_a = sa + (da * inv + 127u) / 255u;
    uint32_t sr = (src >> 16) & 0xFFu;
    uint32_t sg = (src >> 8) & 0xFFu;
    uint32_t sb = src & 0xFFu;
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;
    uint32_t r = 0u;
    uint32_t g = 0u;
    uint32_t b = 0u;

    if (!out_a)
        return 0u;
    r = (sr * sa + (dr * da * inv + 127u) / 255u + out_a / 2u) / out_a;
    g = (sg * sa + (dg * da * inv + 127u) / 255u + out_a / 2u) / out_a;
    b = (sb * sa + (db * da * inv + 127u) / 255u + out_a / 2u) / out_a;
    return (out_a << 24) | (r << 16) | (g << 8) | b;
}

static void put_pixel(int32_t x, int32_t y, uint32_t color, uint8_t opacity)
{
    if (x < 0 || y < 0 || x >= (int32_t)g_doc.canvas_w || y >= (int32_t)g_doc.canvas_h)
        return;
    uint32_t *pixel = g_doc.preview_pixels + (uint64_t)(uint32_t)y * g_doc.preview_stride + (uint32_t)x;
    *pixel = blend_pixel(*pixel, color, opacity);
}

static void draw_disc(int32_t cx, int32_t cy, uint32_t radius, uint32_t color, uint8_t opacity)
{
    int32_t r = (int32_t)(radius ? radius : 1u);
    int64_t rr = (int64_t)r * r;
    for (int32_t y = -r; y <= r; ++y)
        for (int32_t x = -r; x <= r; ++x)
            if ((int64_t)x * x + (int64_t)y * y <= rr)
                put_pixel(cx + x, cy + y, color, opacity);
}

static void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      uint32_t stroke, uint32_t color, uint8_t opacity)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    uint32_t radius = stroke > 1u ? stroke / 2u : 0u;

    for (;;)
    {
        if (radius)
            draw_disc(x0, y0, radius, color, opacity);
        else
            put_pixel(x0, y0, color, opacity);
        if (x0 == x1 && y0 == y1)
            break;
        int32_t twice = err * 2;
        if (twice >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (twice <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void ordered_rect(const editor_layer *layer, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h)
{
    int32_t x0 = layer->x0 < layer->x1 ? layer->x0 : layer->x1;
    int32_t y0 = layer->y0 < layer->y1 ? layer->y0 : layer->y1;
    int32_t x1 = layer->x0 > layer->x1 ? layer->x0 : layer->x1;
    int32_t y1 = layer->y0 > layer->y1 ? layer->y0 : layer->y1;
    *x = x0;
    *y = y0;
    *w = (uint32_t)(x1 - x0 + 1);
    *h = (uint32_t)(y1 - y0 + 1);
}

static void draw_rect_layer(const editor_layer *layer)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0u;
    uint32_t h = 0u;
    ordered_rect(layer, &x, &y, &w, &h);
    draw_line(x, y, x + (int32_t)w - 1, y, layer->stroke, layer->color, layer->opacity);
    draw_line(x, y + (int32_t)h - 1, x + (int32_t)w - 1, y + (int32_t)h - 1,
              layer->stroke, layer->color, layer->opacity);
    draw_line(x, y, x, y + (int32_t)h - 1, layer->stroke, layer->color, layer->opacity);
    draw_line(x + (int32_t)w - 1, y, x + (int32_t)w - 1, y + (int32_t)h - 1,
              layer->stroke, layer->color, layer->opacity);
}

static void draw_ellipse_layer(const editor_layer *layer)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t cx = 0;
    int32_t cy = 0;
    ordered_rect(layer, &x, &y, &w, &h);
    rx = (int32_t)w / 2;
    ry = (int32_t)h / 2;
    cx = x + rx;
    cy = y + ry;
    if (rx <= 0 || ry <= 0)
        return;

    int64_t rx2 = (int64_t)rx * rx;
    int64_t ry2 = (int64_t)ry * ry;
    int64_t px = 0;
    int64_t py = 2 * rx2 * ry;
    int64_t px_step = 2 * ry2;
    int64_t py_step = 2 * rx2;
    int64_t decision = ry2 - rx2 * ry + rx2 / 4;
    int32_t ix = 0;
    int32_t iy = ry;
    uint32_t radius = layer->stroke > 1u ? layer->stroke / 2u : 0u;

    while (px < py)
    {
        draw_disc(cx + ix, cy + iy, radius, layer->color, layer->opacity);
        draw_disc(cx - ix, cy + iy, radius, layer->color, layer->opacity);
        draw_disc(cx + ix, cy - iy, radius, layer->color, layer->opacity);
        draw_disc(cx - ix, cy - iy, radius, layer->color, layer->opacity);
        ++ix;
        px += px_step;
        if (decision < 0)
            decision += ry2 + px;
        else
        {
            --iy;
            py -= py_step;
            decision += ry2 + px - py;
        }
    }

    decision = ry2 * (int64_t)(2 * ix + 1) * (2 * ix + 1) / 4 +
               rx2 * (int64_t)(iy - 1) * (iy - 1) - rx2 * ry2;
    while (iy >= 0)
    {
        draw_disc(cx + ix, cy + iy, radius, layer->color, layer->opacity);
        draw_disc(cx - ix, cy + iy, radius, layer->color, layer->opacity);
        draw_disc(cx + ix, cy - iy, radius, layer->color, layer->opacity);
        draw_disc(cx - ix, cy - iy, radius, layer->color, layer->opacity);
        --iy;
        py -= py_step;
        if (decision > 0)
            decision += rx2 - py;
        else
        {
            ++ix;
            px += px_step;
            decision += rx2 - py + px;
        }
    }
}

static void draw_vector_layer(editor_layer *layer)
{
    if (!layer || !layer->visible)
        return;
    if (layer->type == EDITOR_LAYER_ARROW)
    {
        int32_t dx = layer->x1 - layer->x0;
        int32_t dy = layer->y1 - layer->y0;
        draw_line(layer->x0, layer->y0, layer->x1, layer->y1,
                  layer->stroke, layer->color, layer->opacity);
        draw_line(layer->x1, layer->y1,
                  layer->x1 - dx / 4 + dy / 8,
                  layer->y1 - dy / 4 - dx / 8,
                  layer->stroke, layer->color, layer->opacity);
        draw_line(layer->x1, layer->y1,
                  layer->x1 - dx / 4 - dy / 8,
                  layer->y1 - dy / 4 + dx / 8,
                  layer->stroke, layer->color, layer->opacity);
    }
    else if (layer->type == EDITOR_LAYER_RECT)
    {
        draw_rect_layer(layer);
    }
    else if (layer->type == EDITOR_LAYER_ELLIPSE)
    {
        draw_ellipse_layer(layer);
    }
    else if (layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER)
    {
        for (uint32_t p = 1u; p < layer->point_count; ++p)
        {
            editor_point *a = &g_doc.points[layer->point_start + p - 1u];
            editor_point *b = &g_doc.points[layer->point_start + p];
            draw_line(a->x, a->y, b->x, b->y, layer->stroke, layer->color, layer->opacity);
        }
    }
    else if (layer->type == EDITOR_LAYER_TEXT && layer->text[0] &&
             SACX_API_HAS(g_api, img_draw_text))
    {
        sacx_color color = {(uint8_t)(layer->color >> 16),
                            (uint8_t)(layer->color >> 8),
                            (uint8_t)layer->color};
        (void)g_api->img_draw_text(g_doc.preview, layer->x0, layer->y0, layer->text,
                                   color, layer->opacity, layer->stroke ? layer->stroke : 2u);
    }
}

static void copy_preview_to_scratch(void)
{
    uint64_t count = (uint64_t)g_doc.canvas_w * g_doc.canvas_h;
    for (uint64_t i = 0u; i < count; ++i)
        g_doc.scratch_pixels[i] = g_doc.preview_pixels[i];
}

static uint32_t averaged_pixel(uint64_t aa, uint64_t ar, uint64_t ag, uint64_t ab, uint32_t count)
{
    if (!count)
        return 0u;
    return ((uint32_t)(aa / count) << 24) |
           ((uint32_t)(ar / count) << 16) |
           ((uint32_t)(ag / count) << 8) |
           (uint32_t)(ab / count);
}

static void pixel_sum_add(uint32_t pixel, uint64_t *aa, uint64_t *ar, uint64_t *ag, uint64_t *ab)
{
    *aa += pixel >> 24;
    *ar += (pixel >> 16) & 0xFFu;
    *ag += (pixel >> 8) & 0xFFu;
    *ab += pixel & 0xFFu;
}

static void pixel_sum_remove(uint32_t pixel, uint64_t *aa, uint64_t *ar, uint64_t *ag, uint64_t *ab)
{
    *aa -= pixel >> 24;
    *ar -= (pixel >> 16) & 0xFFu;
    *ag -= (pixel >> 8) & 0xFFu;
    *ab -= pixel & 0xFFu;
}

static void apply_blur_layer(editor_layer *layer)
{
    int32_t region_x = 0;
    int32_t region_y = 0;
    uint32_t region_w = 0u;
    uint32_t region_h = 0u;
    uint32_t radius = layer->effect_strength ? layer->effect_strength : 4u;
    if (radius > 32u)
        radius = 32u;
    ordered_rect(layer, &region_x, &region_y, &region_w, &region_h);

    /* Horizontal running box pass: original scratch -> preview workspace. */
    for (uint32_t y = 0u; y < g_doc.canvas_h; ++y)
    {
        uint64_t aa = 0u, ar = 0u, ag = 0u, ab = 0u;
        uint32_t count = radius + 1u < g_doc.canvas_w ? radius + 1u : g_doc.canvas_w;
        for (uint32_t x = 0u; x < count; ++x)
            pixel_sum_add(g_doc.scratch_pixels[(uint64_t)y * g_doc.scratch_stride + x],
                          &aa, &ar, &ag, &ab);
        for (uint32_t x = 0u; x < g_doc.canvas_w; ++x)
        {
            g_doc.preview_pixels[(uint64_t)y * g_doc.preview_stride + x] =
                averaged_pixel(aa, ar, ag, ab, count);
            if (x >= radius)
            {
                pixel_sum_remove(g_doc.scratch_pixels[(uint64_t)y * g_doc.scratch_stride + x - radius],
                                 &aa, &ar, &ag, &ab);
                --count;
            }
            if (x + radius + 1u < g_doc.canvas_w)
            {
                pixel_sum_add(g_doc.scratch_pixels[(uint64_t)y * g_doc.scratch_stride + x + radius + 1u],
                              &aa, &ar, &ag, &ab);
                ++count;
            }
        }
    }

    int32_t first_x = region_x < 0 ? 0 : region_x;
    int32_t last_x = region_x + (int32_t)region_w;
    int32_t first_y = region_y < 0 ? 0 : region_y;
    int32_t last_y = region_y + (int32_t)region_h;
    if (last_x > (int32_t)g_doc.canvas_w)
        last_x = (int32_t)g_doc.canvas_w;
    if (last_y > (int32_t)g_doc.canvas_h)
        last_y = (int32_t)g_doc.canvas_h;

    /* Vertical running pass. Store final blended pixels back in scratch. */
    for (int32_t x = first_x; x < last_x; ++x)
    {
        uint64_t aa = 0u, ar = 0u, ag = 0u, ab = 0u;
        uint32_t count = radius + 1u < g_doc.canvas_h ? radius + 1u : g_doc.canvas_h;
        for (uint32_t y = 0u; y < count; ++y)
            pixel_sum_add(g_doc.preview_pixels[(uint64_t)y * g_doc.preview_stride + (uint32_t)x],
                          &aa, &ar, &ag, &ab);
        for (uint32_t y = 0u; y < g_doc.canvas_h; ++y)
        {
            if ((int32_t)y >= first_y && (int32_t)y < last_y)
            {
                uint64_t index = (uint64_t)y * g_doc.scratch_stride + (uint32_t)x;
                uint32_t blurred = averaged_pixel(aa, ar, ag, ab, count);
                g_doc.scratch_pixels[index] =
                    blend_pixel(g_doc.scratch_pixels[index], blurred, layer->opacity);
            }
            if (y >= radius)
            {
                pixel_sum_remove(g_doc.preview_pixels[(uint64_t)(y - radius) * g_doc.preview_stride +
                                                      (uint32_t)x],
                                 &aa, &ar, &ag, &ab);
                --count;
            }
            if (y + radius + 1u < g_doc.canvas_h)
            {
                pixel_sum_add(g_doc.preview_pixels[(uint64_t)(y + radius + 1u) * g_doc.preview_stride +
                                                   (uint32_t)x],
                              &aa, &ar, &ag, &ab);
                ++count;
            }
        }
    }

    for (uint32_t y = 0u; y < g_doc.canvas_h; ++y)
        for (uint32_t x = 0u; x < g_doc.canvas_w; ++x)
            g_doc.preview_pixels[(uint64_t)y * g_doc.preview_stride + x] =
                g_doc.scratch_pixels[(uint64_t)y * g_doc.scratch_stride + x];
}

static void apply_effect_rows(editor_layer *layer, uint32_t first_row, uint32_t end_row)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0u;
    uint32_t h = 0u;

    if (!layer || !layer->visible)
        return;
    if (layer->type == EDITOR_LAYER_ADJUSTMENT)
    {
        for (uint32_t row = first_row; row < end_row; ++row)
        {
            for (uint32_t column = 0u; column < g_doc.canvas_w; ++column)
            {
                uint64_t p = (uint64_t)row * g_doc.preview_stride + column;
                uint32_t pixel = g_doc.preview_pixels[p];
                int32_t r = (int32_t)((pixel >> 16) & 0xFFu);
                int32_t g = (int32_t)((pixel >> 8) & 0xFFu);
                int32_t b = (int32_t)(pixel & 0xFFu);
                int32_t gray = (r * 77 + g * 150 + b * 29) >> 8;
                int32_t contrast = layer->contrast + 100;
                r = ((r - 128) * contrast) / 100 + 128 + layer->brightness * 255 / 100;
                g = ((g - 128) * contrast) / 100 + 128 + layer->brightness * 255 / 100;
                b = ((b - 128) * contrast) / 100 + 128 + layer->brightness * 255 / 100;
                r = gray + (r - gray) * (layer->saturation + 100) / 100;
                g = gray + (g - gray) * (layer->saturation + 100) / 100;
                b = gray + (b - gray) * (layer->saturation + 100) / 100;
                if (layer->grayscale)
                    r = g = b = gray;
                uint32_t adjusted = (pixel & 0xFF000000u) |
                                    ((uint32_t)clamp_u8(r) << 16) |
                                    ((uint32_t)clamp_u8(g) << 8) |
                                    clamp_u8(b);
                g_doc.preview_pixels[p] = blend_pixel(pixel, adjusted, layer->opacity);
            }
        }
        return;
    }

    if (layer->type != EDITOR_LAYER_BLUR &&
        layer->type != EDITOR_LAYER_PIXELATE &&
        layer->type != EDITOR_LAYER_REDACT)
        return;

    ordered_rect(layer, &x, &y, &w, &h);
    if (layer->type == EDITOR_LAYER_REDACT)
    {
        for (uint32_t row = first_row; row < end_row; ++row)
        {
            if ((int32_t)row < y || (int32_t)row >= y + (int32_t)h)
                continue;
            for (uint32_t ix = 0u; ix < w; ++ix)
                put_pixel(x + (int32_t)ix, (int32_t)row, 0xFF000000u, layer->opacity);
        }
        return;
    }

    if (layer->type == EDITOR_LAYER_PIXELATE)
    {
        uint32_t block = layer->effect_strength ? layer->effect_strength : 10u;
        if (block < 2u)
            block = 2u;
        for (uint32_t row = first_row; row < end_row; ++row)
        {
            if ((int32_t)row < y || (int32_t)row >= y + (int32_t)h)
                continue;
            for (uint32_t ix = 0u; ix < w; ++ix)
            {
                int32_t px = x + (int32_t)ix;
                int32_t sx = x + (int32_t)((ix / block) * block);
                int32_t sy = y + ((int32_t)row - y) / (int32_t)block * (int32_t)block;
                if (px < 0 || sx < 0 || sy < 0 || px >= (int32_t)g_doc.canvas_w ||
                    sx >= (int32_t)g_doc.canvas_w || sy >= (int32_t)g_doc.canvas_h)
                    continue;
                uint32_t *target = g_doc.preview_pixels + (uint64_t)row * g_doc.preview_stride + (uint32_t)px;
                uint32_t sample = g_doc.scratch_pixels[(uint64_t)(uint32_t)sy * g_doc.scratch_stride + (uint32_t)sx];
                *target = blend_pixel(*target, sample, layer->opacity);
            }
        }
        return;
    }

}

static void snapshot_store(editor_snapshot *snapshot)
{
    if (!snapshot)
        return;
    snapshot->layer_count = g_doc.layer_count;
    snapshot->selected_layer = g_doc.selected_layer;
    snapshot->point_count = g_doc.point_count;
    snapshot->crop_x = g_doc.crop_x;
    snapshot->crop_y = g_doc.crop_y;
    snapshot->crop_w = g_doc.crop_w;
    snapshot->crop_h = g_doc.crop_h;
    snapshot->resize_w = g_doc.resize_w;
    snapshot->resize_h = g_doc.resize_h;
    snapshot->rotation = g_doc.rotation;
    snapshot->flip_x = g_doc.flip_x;
    snapshot->flip_y = g_doc.flip_y;
    for (uint32_t i = 0u; i < EDITOR_MAX_LAYERS; ++i)
        snapshot->layers[i] = g_doc.layers[i];
}

static void snapshot_restore(const editor_snapshot *snapshot)
{
    if (!snapshot)
        return;
    g_doc.layer_count = snapshot->layer_count;
    g_doc.selected_layer = snapshot->selected_layer;
    g_doc.point_count = snapshot->point_count;
    g_doc.crop_x = snapshot->crop_x;
    g_doc.crop_y = snapshot->crop_y;
    g_doc.crop_w = snapshot->crop_w;
    g_doc.crop_h = snapshot->crop_h;
    g_doc.resize_w = snapshot->resize_w;
    g_doc.resize_h = snapshot->resize_h;
    g_doc.rotation = snapshot->rotation;
    g_doc.flip_x = snapshot->flip_x;
    g_doc.flip_y = snapshot->flip_y;
    for (uint32_t i = 0u; i < EDITOR_MAX_LAYERS; ++i)
        g_doc.layers[i] = snapshot->layers[i];
    document_request_render();
}

static void collect_image_handle(uint32_t *handles, uint32_t *count, uint32_t cap, uint32_t handle)
{
    if (!handle || !handles || !count)
        return;
    for (uint32_t i = 0u; i < *count; ++i)
        if (handles[i] == handle)
            return;
    if (*count < cap)
        handles[(*count)++] = handle;
}

void document_dispose(editor_document *document)
{
    enum
    {
        MAX_OWNED_IMAGES = EDITOR_MAX_HISTORY * EDITOR_MAX_RASTER_LAYERS +
                           EDITOR_MAX_RASTER_LAYERS + 2u
    };
    static uint32_t handles[MAX_OWNED_IMAGES];
    uint32_t handle_count = 0u;

    if (!document)
        return;
    for (uint32_t i = 0u; i < document->layer_count; ++i)
        if (document->layers[i].type == EDITOR_LAYER_RASTER)
            collect_image_handle(handles, &handle_count, MAX_OWNED_IMAGES, document->layers[i].image);
    for (uint32_t h = 0u; h < document->history_count && h < EDITOR_MAX_HISTORY; ++h)
        for (uint32_t i = 0u; i < document->history[h].layer_count && i < EDITOR_MAX_LAYERS; ++i)
            if (document->history[h].layers[i].type == EDITOR_LAYER_RASTER)
                collect_image_handle(handles, &handle_count, MAX_OWNED_IMAGES,
                                     document->history[h].layers[i].image);
    collect_image_handle(handles, &handle_count, MAX_OWNED_IMAGES, document->preview);
    collect_image_handle(handles, &handle_count, MAX_OWNED_IMAGES, document->scratch);
    if (g_api && g_api->img_destroy)
        for (uint32_t i = 0u; i < handle_count; ++i)
            (void)g_api->img_destroy(handles[i]);
    editor_zero_memory(document, sizeof(*document));
    document->selected_layer = -1;
}

void document_reset(void)
{
    document_dispose(&g_doc);
}

uint32_t document_raster_count(void)
{
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < g_doc.layer_count; ++i)
        if (g_doc.layers[i].type == EDITOR_LAYER_RASTER)
            ++count;
    return count;
}

int document_add_raster(uint32_t image, int32_t x, int32_t y)
{
    editor_layer *layer = 0;
    uint32_t w = 0u;
    uint32_t h = 0u;
    if (!g_api || !image || g_doc.layer_count >= EDITOR_MAX_LAYERS ||
        document_raster_count() >= EDITOR_MAX_RASTER_LAYERS)
        return -1;
    if (g_api->img_size(image, &w, &h) != 0 ||
        !SACX_API_HAS(g_api, img_pixels))
        return -1;
    layer = &g_doc.layers[g_doc.layer_count++];
    *layer = (editor_layer){0};
    layer->type = EDITOR_LAYER_RASTER;
    layer->image = image;
    layer->raster_w = w;
    layer->raster_h = h;
    layer->x0 = x;
    layer->y0 = y;
    layer->x1 = x + (int32_t)w - 1;
    layer->y1 = y + (int32_t)h - 1;
    layer->visible = 1u;
    layer->opacity = 255u;
    if (g_api->img_pixels(image, &layer->pixels, &layer->stride) != 0)
    {
        --g_doc.layer_count;
        return -1;
    }
    g_doc.selected_layer = (int32_t)g_doc.layer_count - 1;
    return 0;
}

int document_open_image(uint32_t image, const char *raw_path, const char *friendly_path)
{
    uint32_t w = 0u;
    uint32_t h = 0u;
    document_reset();
    if (!g_api || !image || g_api->img_size(image, &w, &h) != 0 || !w || !h)
        return -1;
    g_doc.canvas_w = w;
    g_doc.canvas_h = h;
    g_doc.crop_w = w;
    g_doc.crop_h = h;
    if (document_add_raster(image, 0, 0) != 0 ||
        !SACX_API_HAS(g_api, img_create) ||
        g_api->img_create(w, h, 0u, &g_doc.preview) != 0 ||
        g_api->img_create(w, h, 0u, &g_doc.scratch) != 0 ||
        g_api->img_pixels(g_doc.preview, &g_doc.preview_pixels, &g_doc.preview_stride) != 0 ||
        g_api->img_pixels(g_doc.scratch, &g_doc.scratch_pixels, &g_doc.scratch_stride) != 0)
        return -1;
    editor_copy_text(g_doc.source_raw, sizeof(g_doc.source_raw), raw_path);
    editor_copy_text(g_doc.source_friendly, sizeof(g_doc.source_friendly), friendly_path);
    document_request_render();
    g_doc.dirty = 0u;
    g_doc.history_count = 1u;
    g_doc.history_pos = 0u;
    snapshot_store(&g_doc.history[0]);
    return 0;
}

int document_add_layer(uint32_t type)
{
    editor_layer *layer = 0;
    if (g_doc.layer_count >= EDITOR_MAX_LAYERS || type < EDITOR_LAYER_TEXT || type > EDITOR_LAYER_ADJUSTMENT)
        return -1;
    layer = &g_doc.layers[g_doc.layer_count++];
    *layer = (editor_layer){0};
    layer->type = type;
    layer->visible = 1u;
    layer->opacity = 255u;
    layer->color = 0xFFFF3B30u;
    layer->stroke = (type == EDITOR_LAYER_TEXT) ? 2u : 4u;
    layer->effect_strength = (type == EDITOR_LAYER_PIXELATE) ? 12u : 4u;
    if (type == EDITOR_LAYER_HIGHLIGHTER)
    {
        layer->opacity = 15u;
        layer->color = 0xFFFFD60Au;
        layer->stroke = 32u;
    }
    layer->x0 = 0;
    layer->y0 = 0;
    layer->x1 = 1;
    layer->y1 = 1;
    if (type == EDITOR_LAYER_TEXT)
        editor_copy_text(layer->text, sizeof(layer->text), "Text");
    g_doc.selected_layer = (int32_t)g_doc.layer_count - 1;
    return g_doc.selected_layer;
}

void document_delete_selected(void)
{
    if (g_doc.selected_layer < 0 || (uint32_t)g_doc.selected_layer >= g_doc.layer_count)
        return;
    for (uint32_t i = (uint32_t)g_doc.selected_layer; i + 1u < g_doc.layer_count; ++i)
        g_doc.layers[i] = g_doc.layers[i + 1u];
    --g_doc.layer_count;
    if (!g_doc.layer_count)
        g_doc.selected_layer = -1;
    else if ((uint32_t)g_doc.selected_layer >= g_doc.layer_count)
        g_doc.selected_layer = (int32_t)g_doc.layer_count - 1;
    document_commit();
}

void document_move_selected(int direction)
{
    int32_t index = g_doc.selected_layer;
    int32_t other = index + direction;
    if (index < 0 || other < 0 || (uint32_t)other >= g_doc.layer_count)
        return;
    editor_layer temp = g_doc.layers[index];
    g_doc.layers[index] = g_doc.layers[other];
    g_doc.layers[other] = temp;
    g_doc.selected_layer = other;
    document_commit();
}

void document_commit(void)
{
    if (!g_doc.canvas_w || !g_doc.canvas_h)
        return;
    if (g_doc.history_pos + 1u >= EDITOR_MAX_HISTORY)
    {
        for (uint32_t i = 1u; i < EDITOR_MAX_HISTORY; ++i)
            g_doc.history[i - 1u] = g_doc.history[i];
        g_doc.history_pos = EDITOR_MAX_HISTORY - 2u;
        g_doc.history_count = EDITOR_MAX_HISTORY - 1u;
    }
    ++g_doc.history_pos;
    g_doc.history_count = g_doc.history_pos + 1u;
    snapshot_store(&g_doc.history[g_doc.history_pos]);
    g_doc.dirty = 1u;
    document_request_render();
}

void document_history_reset(void)
{
    g_doc.history_count = 1u;
    g_doc.history_pos = 0u;
    snapshot_store(&g_doc.history[0]);
}

int document_undo(void)
{
    if (!g_doc.history_count || g_doc.history_pos == 0u)
        return 0;
    --g_doc.history_pos;
    snapshot_restore(&g_doc.history[g_doc.history_pos]);
    g_doc.dirty = 1u;
    return 1;
}

int document_redo(void)
{
    if (g_doc.history_pos + 1u >= g_doc.history_count)
        return 0;
    ++g_doc.history_pos;
    snapshot_restore(&g_doc.history[g_doc.history_pos]);
    g_doc.dirty = 1u;
    return 1;
}

void document_request_render(void)
{
    g_doc.dirty_x0 = 0;
    g_doc.dirty_y0 = 0;
    g_doc.dirty_x1 = g_doc.canvas_w ? (int32_t)g_doc.canvas_w - 1 : 0;
    g_doc.dirty_y1 = g_doc.canvas_h ? (int32_t)g_doc.canvas_h - 1 : 0;
    g_doc.dirty_bounds_valid = g_doc.canvas_w && g_doc.canvas_h ? 1u : 0u;
    g_doc.render_layer = 0u;
    g_doc.render_row = 0u;
    g_doc.render_pending = g_doc.preview_pixels ? 1u : 0u;
}

void document_request_render_rect(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h || !g_doc.canvas_w || !g_doc.canvas_h)
        return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int64_t right = (int64_t)x + w - 1;
    int64_t bottom = (int64_t)y + h - 1;
    int32_t x1 = right >= (int64_t)g_doc.canvas_w ? (int32_t)g_doc.canvas_w - 1 : (int32_t)right;
    int32_t y1 = bottom >= (int64_t)g_doc.canvas_h ? (int32_t)g_doc.canvas_h - 1 : (int32_t)bottom;
    if (x1 < x0 || y1 < y0)
        return;
    if (!g_doc.dirty_bounds_valid)
    {
        g_doc.dirty_x0 = x0;
        g_doc.dirty_y0 = y0;
        g_doc.dirty_x1 = x1;
        g_doc.dirty_y1 = y1;
        g_doc.dirty_bounds_valid = 1u;
    }
    else
    {
        if (x0 < g_doc.dirty_x0) g_doc.dirty_x0 = x0;
        if (y0 < g_doc.dirty_y0) g_doc.dirty_y0 = y0;
        if (x1 > g_doc.dirty_x1) g_doc.dirty_x1 = x1;
        if (y1 > g_doc.dirty_y1) g_doc.dirty_y1 = y1;
    }

    /*
     * Layer ordering and adjustment layers can make a local edit affect later
     * pixels globally, so rendering restarts in order while retaining the
     * precise invalidation bounds for future partial-composite paths.
     */
    g_doc.render_layer = 0u;
    g_doc.render_row = 0u;
    g_doc.render_pending = g_doc.preview_pixels ? 1u : 0u;
}

int document_render_step(uint32_t row_budget)
{
    if (!g_doc.render_pending || !g_doc.preview_pixels)
        return 0;
    if (!row_budget)
        row_budget = 1u;

    while (row_budget && g_doc.render_pending)
    {
        if (g_doc.render_layer == 0u)
        {
            uint32_t rows = g_doc.canvas_h - g_doc.render_row;
            if (rows > row_budget)
                rows = row_budget;
            for (uint32_t y = g_doc.render_row; y < g_doc.render_row + rows; ++y)
                for (uint32_t x = 0u; x < g_doc.canvas_w; ++x)
                    g_doc.preview_pixels[(uint64_t)y * g_doc.preview_stride + x] = 0u;
            g_doc.render_row += rows;
            row_budget -= rows;
            if (g_doc.render_row == g_doc.canvas_h)
            {
                g_doc.render_layer = 1u;
                g_doc.render_row = 0u;
            }
            continue;
        }

        if (g_doc.render_layer > g_doc.layer_count)
        {
            if (SACX_API_HAS(g_api, img_touch))
                (void)g_api->img_touch(g_doc.preview);
            g_doc.render_pending = 0u;
            g_doc.dirty_bounds_valid = 0u;
            break;
        }

        editor_layer *layer = &g_doc.layers[g_doc.render_layer - 1u];
        if (!layer->visible)
        {
            ++g_doc.render_layer;
            g_doc.render_row = 0u;
            continue;
        }

        if (layer->type == EDITOR_LAYER_RASTER)
        {
            int32_t rx = 0;
            int32_t ry = 0;
            uint32_t rw = 0u;
            uint32_t rh = 0u;
            ordered_rect(layer, &rx, &ry, &rw, &rh);
            uint32_t rows = g_doc.canvas_h - g_doc.render_row;
            if (rows > row_budget)
                rows = row_budget;
            for (uint32_t y = g_doc.render_row; y < g_doc.render_row + rows; ++y)
            {
                int32_t dy = (int32_t)y - ry;
                if (!layer->pixels || dy < 0 || dy >= (int32_t)rh)
                    continue;
                uint32_t sy = (uint32_t)((uint64_t)(uint32_t)dy * layer->raster_h / rh);
                int32_t first_x = rx < 0 ? 0 : rx;
                int32_t last_x = rx + (int32_t)rw;
                if (last_x > (int32_t)g_doc.canvas_w)
                    last_x = (int32_t)g_doc.canvas_w;
                for (int32_t x = first_x; x < last_x; ++x)
                {
                    uint32_t sx = (uint32_t)((uint64_t)(uint32_t)(x - rx) * layer->raster_w / rw);
                    uint32_t source = layer->pixels[(uint64_t)sy * layer->stride + sx];
                    uint32_t *target = g_doc.preview_pixels + (uint64_t)y * g_doc.preview_stride + (uint32_t)x;
                    *target = blend_pixel(*target, source, layer->opacity);
                }
            }
            g_doc.render_row += rows;
            row_budget -= rows;
        }
        else if (layer->type == EDITOR_LAYER_ADJUSTMENT ||
                 layer->type == EDITOR_LAYER_BLUR ||
                 layer->type == EDITOR_LAYER_PIXELATE ||
                 layer->type == EDITOR_LAYER_REDACT)
        {
            if (g_doc.render_row == 0u &&
                (layer->type == EDITOR_LAYER_BLUR || layer->type == EDITOR_LAYER_PIXELATE))
                copy_preview_to_scratch();
            if (layer->type == EDITOR_LAYER_BLUR)
            {
                apply_blur_layer(layer);
                g_doc.render_row = g_doc.canvas_h;
                --row_budget;
            }
            else
            {
                uint32_t rows = g_doc.canvas_h - g_doc.render_row;
                if (rows > row_budget)
                    rows = row_budget;
                apply_effect_rows(layer, g_doc.render_row, g_doc.render_row + rows);
                g_doc.render_row += rows;
                row_budget -= rows;
            }
        }
        else
        {
            draw_vector_layer(layer);
            g_doc.render_row = g_doc.canvas_h;
            --row_budget;
        }

        if (g_doc.render_row == g_doc.canvas_h)
        {
            ++g_doc.render_layer;
            g_doc.render_row = 0u;
        }
    }

    return g_doc.render_pending ? 1 : 0;
}

void document_render_now(void)
{
    while (document_render_step(256u))
    {
    }
}

int document_export_flattened(uint32_t *out_image)
{
    uint32_t image = 0u;
    uint32_t output_w = 0u;
    uint32_t output_h = 0u;

    document_render_now();
    if (!out_image || document_export_begin(&image, &output_w, &output_h) != 0)
        return -1;
    if (document_export_step(image, 0u, output_h) != 0)
    {
        g_api->img_destroy(image);
        return -1;
    }
    (void)g_api->img_touch(image);
    *out_image = image;
    return 0;
}

int document_export_begin(uint32_t *out_image, uint32_t *out_w, uint32_t *out_h)
{
    uint32_t transformed_w = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_h : g_doc.crop_w;
    uint32_t transformed_h = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_w : g_doc.crop_h;
    uint32_t output_w = g_doc.resize_w ? g_doc.resize_w : transformed_w;
    uint32_t output_h = g_doc.resize_h ? g_doc.resize_h : transformed_h;
    uint32_t image = 0u;

    if (!out_image || !out_w || !out_h || !output_w || !output_h ||
        !SACX_API_HAS(g_api, img_create))
        return -1;
    if (g_api->img_create(output_w, output_h, 0u, &image) != 0)
        return -1;
    *out_image = image;
    *out_w = output_w;
    *out_h = output_h;
    return 0;
}

int document_export_step(uint32_t image, uint32_t first_row, uint32_t row_count)
{
    uint32_t transformed_w = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_h : g_doc.crop_w;
    uint32_t transformed_h = (g_doc.rotation == 90u || g_doc.rotation == 270u) ? g_doc.crop_w : g_doc.crop_h;
    uint32_t output_w = g_doc.resize_w ? g_doc.resize_w : transformed_w;
    uint32_t output_h = g_doc.resize_h ? g_doc.resize_h : transformed_h;
    uint32_t *pixels = 0;
    uint32_t stride = 0u;
    uint32_t end_row = first_row + row_count;

    if (!image || first_row >= output_h || !row_count ||
        g_api->img_pixels(image, &pixels, &stride) != 0)
        return -1;
    if (end_row < first_row || end_row > output_h)
        end_row = output_h;
    for (uint32_t y = first_row; y < end_row; ++y)
    {
        for (uint32_t x = 0u; x < output_w; ++x)
        {
            uint32_t scaled_x = (uint32_t)((uint64_t)x * transformed_w / output_w);
            uint32_t scaled_y = (uint32_t)((uint64_t)y * transformed_h / output_h);
            uint32_t tx = g_doc.flip_x ? transformed_w - 1u - scaled_x : scaled_x;
            uint32_t ty = g_doc.flip_y ? transformed_h - 1u - scaled_y : scaled_y;
            uint32_t sx = tx;
            uint32_t sy = ty;
            if (g_doc.rotation == 90u)
            {
                sx = ty;
                sy = g_doc.crop_h - 1u - tx;
            }
            else if (g_doc.rotation == 180u)
            {
                sx = g_doc.crop_w - 1u - tx;
                sy = g_doc.crop_h - 1u - ty;
            }
            else if (g_doc.rotation == 270u)
            {
                sx = g_doc.crop_w - 1u - ty;
                sy = tx;
            }
            pixels[(uint64_t)y * stride + x] =
                g_doc.preview_pixels[(uint64_t)(g_doc.crop_y + (int32_t)sy) * g_doc.preview_stride +
                                     (uint32_t)(g_doc.crop_x + (int32_t)sx)];
        }
    }
    return 0;
}

int document_hit_test(int32_t x, int32_t y)
{
    for (int32_t i = (int32_t)g_doc.layer_count - 1; i >= 0; --i)
    {
        editor_layer *layer = &g_doc.layers[i];
        int32_t rx = 0;
        int32_t ry = 0;
        uint32_t rw = 0u;
        uint32_t rh = 0u;
        if (!layer->visible)
            continue;
        if (layer->type == EDITOR_LAYER_ADJUSTMENT)
            continue;
        if (layer->type == EDITOR_LAYER_PEN || layer->type == EDITOR_LAYER_HIGHLIGHTER)
        {
            for (uint32_t p = 0u; p < layer->point_count; ++p)
            {
                editor_point *point = &g_doc.points[layer->point_start + p];
                if (x >= point->x - 8 && x <= point->x + 8 && y >= point->y - 8 && y <= point->y + 8)
                    return i;
            }
            continue;
        }
        ordered_rect(layer, &rx, &ry, &rw, &rh);
        if (x >= rx - 6 && y >= ry - 6 && x < rx + (int32_t)rw + 6 && y < ry + (int32_t)rh + 6)
            return i;
    }
    return -1;
}

const char *document_layer_name(uint32_t type)
{
    switch (type)
    {
    case EDITOR_LAYER_RASTER: return "Raster";
    case EDITOR_LAYER_TEXT: return "Text";
    case EDITOR_LAYER_ARROW: return "Arrow";
    case EDITOR_LAYER_RECT: return "Rectangle";
    case EDITOR_LAYER_ELLIPSE: return "Ellipse";
    case EDITOR_LAYER_PEN: return "Pen";
    case EDITOR_LAYER_HIGHLIGHTER: return "Highlighter";
    case EDITOR_LAYER_BLUR: return "Blur";
    case EDITOR_LAYER_PIXELATE: return "Pixelate";
    case EDITOR_LAYER_REDACT: return "Redact";
    case EDITOR_LAYER_ADJUSTMENT: return "Adjustment";
    default: return "Layer";
    }
}
