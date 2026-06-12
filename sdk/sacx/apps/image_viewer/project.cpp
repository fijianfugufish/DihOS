#include "editor.h"

#define DIMG_MAGIC 0x474D4944u /* DIMG */
#define DIMG_VERSION 1u
#define DIMG_CHUNK_DOC 0x20434F44u /* DOC  */
#define DIMG_CHUNK_LAYERS 0x5259414Cu /* LAYR */
#define DIMG_CHUNK_POINTS 0x544E4F50u /* PONT */
#define DIMG_CHUNK_PNG 0x53474E50u /* PNGS */

#pragma pack(push, 1)
typedef struct dimg_header
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t file_size;
    uint32_t crc32;
} dimg_header;

typedef struct dimg_chunk_header
{
    uint32_t type;
    uint32_t size;
} dimg_chunk_header;

typedef struct dimg_document_disk
{
    uint32_t canvas_w;
    uint32_t canvas_h;
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
    uint8_t reserved[2];
} dimg_document_disk;

typedef struct dimg_layer_disk
{
    uint32_t type;
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
} dimg_layer_disk;
#pragma pack(pop)

static editor_document G_previous_document;

enum async_export_phase
{
    ASYNC_EXPORT_IDLE = 0,
    ASYNC_EXPORT_START_WAIT,
    ASYNC_EXPORT_PREPARE,
    ASYNC_EXPORT_RENDER,
    ASYNC_EXPORT_FLATTEN_BEGIN,
    ASYNC_EXPORT_FLATTEN,
    ASYNC_EXPORT_ENCODE_WAIT,
    ASYNC_EXPORT_ENCODE,
};

typedef struct async_export_job
{
    uint32_t phase;
    uint32_t image;
    uint32_t width;
    uint32_t height;
    uint32_t next_row;
    uint32_t format;
    uint32_t quality;
    char raw_path[EDITOR_PATH_CAP];
    char temp_path[EDITOR_PATH_CAP];
} async_export_job;

static async_export_job G_export_job;

static int text_equal(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a && *b)
    {
        if (*a++ != *b++)
            return 0;
    }
    return *a == *b;
}

static int file_exists(const char *path)
{
    uint32_t file = 0u;
    if (!path || g_api->file_open(path, SACX_FILE_READ, &file) != 0)
        return 0;
    g_api->file_close(file);
    return 1;
}

static int replace_file(const char *temporary, const char *destination)
{
    char backup[EDITOR_PATH_CAP];
    int had_destination = file_exists(destination);

    editor_copy_text(backup, sizeof(backup), destination);
    editor_append_text(backup, sizeof(backup), ".__dimg_backup");
    if (text_equal(backup, destination))
        return -1;
    (void)g_api->file_unlink(backup);
    if (had_destination && g_api->file_rename(destination, backup) != 0)
        return -1;
    if (g_api->file_rename(temporary, destination) != 0)
    {
        if (had_destination)
            (void)g_api->file_rename(backup, destination);
        return -1;
    }
    if (had_destination)
        (void)g_api->file_unlink(backup);
    return 0;
}

static int file_write_all(uint32_t file, const void *data, uint32_t size)
{
    uint32_t total = 0u;
    const uint8_t *bytes = (const uint8_t *)data;
    while (total < size)
    {
        uint32_t written = 0u;
        if (g_api->file_write(file, bytes + total, size - total, &written) != 0 || !written)
            return -1;
        total += written;
    }
    return 0;
}

static int file_read_all(uint32_t file, void *data, uint32_t size)
{
    uint32_t total = 0u;
    uint8_t *bytes = (uint8_t *)data;
    while (total < size)
    {
        uint32_t read = 0u;
        if (g_api->file_read(file, bytes + total, size - total, &read) != 0 || !read)
            return -1;
        total += read;
    }
    return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0u; i < size; ++i)
    {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
    }
    return crc;
}

static int project_crc(const char *path, uint32_t *out_crc)
{
    uint32_t file = 0u;
    uint64_t size = 0u;
    uint64_t offset = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buffer[4096];

    if (!path || !out_crc || g_api->file_open(path, SACX_FILE_READ, &file) != 0)
        return -1;
    size = g_api->file_size(file);
    while (offset < size)
    {
        uint32_t want = size - offset > sizeof(buffer) ? sizeof(buffer) : (uint32_t)(size - offset);
        uint32_t read = 0u;
        if (g_api->file_read(file, buffer, want, &read) != 0 || read != want)
        {
            g_api->file_close(file);
            return -1;
        }
        for (uint32_t i = 0u; i < read; ++i)
        {
            uint64_t absolute = offset + i;
            if (absolute >= offsetof(dimg_header, crc32) &&
                absolute < offsetof(dimg_header, crc32) + sizeof(uint32_t))
                buffer[i] = 0u;
        }
        crc = crc32_update(crc, buffer, read);
        offset += read;
    }
    g_api->file_close(file);
    *out_crc = ~crc;
    return 0;
}

static void layer_to_disk(const editor_layer *source, dimg_layer_disk *disk)
{
    *disk = (dimg_layer_disk){0};
    disk->type = source->type;
    disk->raster_w = source->raster_w;
    disk->raster_h = source->raster_h;
    disk->x0 = source->x0;
    disk->y0 = source->y0;
    disk->x1 = source->x1;
    disk->y1 = source->y1;
    disk->point_start = source->point_start;
    disk->point_count = source->point_count;
    disk->color = source->color;
    disk->stroke = source->stroke;
    disk->brightness = source->brightness;
    disk->contrast = source->contrast;
    disk->saturation = source->saturation;
    disk->effect_strength = source->effect_strength;
    disk->visible = source->visible;
    disk->locked = source->locked;
    disk->opacity = source->opacity;
    disk->grayscale = source->grayscale;
    editor_copy_text(disk->text, sizeof(disk->text), source->text);
}

static void layer_from_disk(const dimg_layer_disk *disk, editor_layer *layer)
{
    *layer = (editor_layer){0};
    layer->type = disk->type;
    layer->raster_w = disk->raster_w;
    layer->raster_h = disk->raster_h;
    layer->x0 = disk->x0;
    layer->y0 = disk->y0;
    layer->x1 = disk->x1;
    layer->y1 = disk->y1;
    layer->point_start = disk->point_start;
    layer->point_count = disk->point_count;
    layer->color = disk->color;
    layer->stroke = disk->stroke;
    layer->brightness = disk->brightness;
    layer->contrast = disk->contrast;
    layer->saturation = disk->saturation;
    layer->effect_strength = disk->effect_strength;
    layer->visible = disk->visible;
    layer->locked = disk->locked;
    layer->opacity = disk->opacity;
    layer->grayscale = disk->grayscale;
    for (uint32_t i = 0u; i + 1u < sizeof(layer->text); ++i)
        layer->text[i] = disk->text[i];
    layer->text[sizeof(layer->text) - 1u] = 0;
}

static void temporary_png_path(char *path, uint32_t cap, uint32_t layer_index)
{
    editor_copy_text(path, cap, "0:/.__dimg_");
    editor_append_uint(path, cap, (uint32_t)g_api->time_ticks());
    editor_append_text(path, cap, "_");
    editor_append_uint(path, cap, layer_index);
    editor_append_text(path, cap, ".png");
}

static int write_png_chunk(uint32_t project_file, uint32_t layer_index, uint32_t image)
{
    char png_path[EDITOR_PATH_CAP];
    uint32_t png_file = 0u;
    uint64_t png_size = 0u;
    dimg_chunk_header chunk;
    uint8_t buffer[4096];

    temporary_png_path(png_path, sizeof(png_path), layer_index);
    (void)g_api->file_unlink(png_path);
    if (g_api->img_save(image, png_path, SACX_IMG_FORMAT_PNG, 100u) != 0 ||
        g_api->file_open(png_path, SACX_FILE_READ, &png_file) != 0)
        return -1;
    png_size = g_api->file_size(png_file);
    if (!png_size || png_size > 0xFFFFFF00ull)
    {
        g_api->file_close(png_file);
        g_api->file_unlink(png_path);
        return -1;
    }

    chunk.type = DIMG_CHUNK_PNG;
    chunk.size = (uint32_t)png_size + sizeof(uint32_t);
    if (file_write_all(project_file, &chunk, sizeof(chunk)) != 0 ||
        file_write_all(project_file, &layer_index, sizeof(layer_index)) != 0)
    {
        g_api->file_close(png_file);
        g_api->file_unlink(png_path);
        return -1;
    }

    uint64_t remaining = png_size;
    while (remaining)
    {
        uint32_t want = remaining > sizeof(buffer) ? sizeof(buffer) : (uint32_t)remaining;
        uint32_t read = 0u;
        if (g_api->file_read(png_file, buffer, want, &read) != 0 || read != want ||
            file_write_all(project_file, buffer, read) != 0)
        {
            g_api->file_close(png_file);
            g_api->file_unlink(png_path);
            return -1;
        }
        remaining -= read;
    }

    g_api->file_close(png_file);
    g_api->file_unlink(png_path);
    return 0;
}

int project_save(const char *raw_path, const char *friendly_path)
{
    char temp_path[EDITOR_PATH_CAP];
    uint32_t file = 0u;
    dimg_header header = {DIMG_MAGIC, DIMG_VERSION, sizeof(dimg_header), 0u, 0u, 0u};
    dimg_chunk_header chunk;
    dimg_document_disk document;
    uint32_t crc = 0u;

    if (!raw_path || !raw_path[0] || !SACX_API_HAS(g_api, img_save))
        return -1;
    editor_copy_text(temp_path, sizeof(temp_path), raw_path);
    editor_append_text(temp_path, sizeof(temp_path), ".tmp");
    if (text_equal(temp_path, raw_path))
        return -1;
    (void)g_api->file_unlink(temp_path);
    if (g_api->file_open(temp_path, SACX_FILE_WRITE | SACX_FILE_CREATE | SACX_FILE_TRUNC, &file) != 0)
        return -1;
    if (file_write_all(file, &header, sizeof(header)) != 0)
        goto fail;

    document.canvas_w = g_doc.canvas_w;
    document.canvas_h = g_doc.canvas_h;
    document.layer_count = g_doc.layer_count;
    document.selected_layer = g_doc.selected_layer;
    document.point_count = g_doc.point_count;
    document.crop_x = g_doc.crop_x;
    document.crop_y = g_doc.crop_y;
    document.crop_w = g_doc.crop_w;
    document.crop_h = g_doc.crop_h;
    document.resize_w = g_doc.resize_w;
    document.resize_h = g_doc.resize_h;
    document.rotation = g_doc.rotation;
    document.flip_x = g_doc.flip_x;
    document.flip_y = g_doc.flip_y;
    document.reserved[0] = document.reserved[1] = 0u;
    chunk.type = DIMG_CHUNK_DOC;
    chunk.size = sizeof(document);
    if (file_write_all(file, &chunk, sizeof(chunk)) != 0 ||
        file_write_all(file, &document, sizeof(document)) != 0)
        goto fail;

    chunk.type = DIMG_CHUNK_LAYERS;
    chunk.size = g_doc.layer_count * sizeof(dimg_layer_disk);
    if (file_write_all(file, &chunk, sizeof(chunk)) != 0)
        goto fail;
    for (uint32_t i = 0u; i < g_doc.layer_count; ++i)
    {
        dimg_layer_disk disk;
        layer_to_disk(&g_doc.layers[i], &disk);
        if (file_write_all(file, &disk, sizeof(disk)) != 0)
            goto fail;
    }

    chunk.type = DIMG_CHUNK_POINTS;
    chunk.size = g_doc.point_count * sizeof(editor_point);
    if (file_write_all(file, &chunk, sizeof(chunk)) != 0 ||
        (chunk.size && file_write_all(file, g_doc.points, chunk.size) != 0))
        goto fail;

    for (uint32_t i = 0u; i < g_doc.layer_count; ++i)
    {
        if (g_doc.layers[i].type == EDITOR_LAYER_RASTER &&
            write_png_chunk(file, i, g_doc.layers[i].image) != 0)
            goto fail;
    }

    {
        uint64_t size = g_api->file_size(file);
        if (size > 0xFFFFFFFFull)
            goto fail;
        header.file_size = (uint32_t)size;
    }
    if (g_api->file_seek(file, 0u) != 0 || file_write_all(file, &header, sizeof(header)) != 0)
        goto fail;
    g_api->file_close(file);
    file = 0u;

    if (project_crc(temp_path, &crc) != 0 ||
        g_api->file_open(temp_path, SACX_FILE_WRITE, &file) != 0)
        goto fail_closed;
    header.crc32 = crc;
    if (g_api->file_seek(file, 0u) != 0 || file_write_all(file, &header, sizeof(header)) != 0)
        goto fail;
    g_api->file_close(file);
    file = 0u;

    if (replace_file(temp_path, raw_path) != 0)
        goto fail_closed;
    editor_copy_text(g_doc.project_raw, sizeof(g_doc.project_raw), raw_path);
    editor_copy_text(g_doc.project_friendly, sizeof(g_doc.project_friendly), friendly_path ? friendly_path : raw_path);
    g_doc.dirty = 0u;
    return 0;

fail:
    if (file)
        g_api->file_close(file);
fail_closed:
    g_api->file_unlink(temp_path);
    return -1;
}

static int read_png_chunk(uint32_t project_file, uint32_t chunk_size)
{
    uint32_t layer_index = 0u;
    char png_path[EDITOR_PATH_CAP];
    uint32_t png_file = 0u;
    uint32_t image = 0u;
    uint8_t buffer[4096];

    if (chunk_size < sizeof(layer_index) ||
        file_read_all(project_file, &layer_index, sizeof(layer_index)) != 0 ||
        layer_index >= g_doc.layer_count)
        return -1;
    chunk_size -= sizeof(layer_index);
    temporary_png_path(png_path, sizeof(png_path), layer_index);
    (void)g_api->file_unlink(png_path);
    if (g_api->file_open(png_path, SACX_FILE_WRITE | SACX_FILE_CREATE | SACX_FILE_TRUNC, &png_file) != 0)
        return -1;
    while (chunk_size)
    {
        uint32_t amount = chunk_size > sizeof(buffer) ? sizeof(buffer) : chunk_size;
        if (file_read_all(project_file, buffer, amount) != 0 ||
            file_write_all(png_file, buffer, amount) != 0)
        {
            g_api->file_close(png_file);
            g_api->file_unlink(png_path);
            return -1;
        }
        chunk_size -= amount;
    }
    g_api->file_close(png_file);
    if (g_api->img_load(png_path, &image) != 0)
    {
        g_api->file_unlink(png_path);
        return -1;
    }
    g_api->file_unlink(png_path);
    g_doc.layers[layer_index].image = image;
    if (g_api->img_pixels(image, &g_doc.layers[layer_index].pixels,
                          &g_doc.layers[layer_index].stride) != 0)
    {
        g_api->img_destroy(image);
        g_doc.layers[layer_index].image = 0u;
        return -1;
    }
    return 0;
}

int project_load(const char *raw_path, const char *friendly_path)
{
    uint32_t file = 0u;
    uint64_t file_size = 0u;
    uint64_t offset = 0u;
    dimg_header header;
    uint32_t crc = 0u;
    uint8_t have_document = 0u;
    uint8_t have_layers = 0u;
    uint8_t have_points = 0u;
    uint8_t switched_document = 0u;
    uint8_t raster_loaded[EDITOR_MAX_LAYERS] = {0};

    if (!raw_path || g_api->file_open(raw_path, SACX_FILE_READ, &file) != 0)
        return -1;
    file_size = g_api->file_size(file);
    if (file_size < sizeof(header) || file_size > 0xFFFFFFFFull ||
        file_read_all(file, &header, sizeof(header)) != 0)
        goto fail;
    if (header.magic != DIMG_MAGIC || header.version != DIMG_VERSION ||
        header.header_size != sizeof(header) || header.file_size != file_size)
        goto fail;
    g_api->file_close(file);
    file = 0u;
    if (project_crc(raw_path, &crc) != 0 || crc != header.crc32 ||
        g_api->file_open(raw_path, SACX_FILE_READ, &file) != 0 ||
        g_api->file_seek(file, sizeof(header)) != 0)
        goto fail;

    G_previous_document = g_doc;
    editor_zero_memory(&g_doc, sizeof(g_doc));
    g_doc.selected_layer = -1;
    switched_document = 1u;
    offset = sizeof(header);
    while (offset + sizeof(dimg_chunk_header) <= file_size)
    {
        dimg_chunk_header chunk;
        uint64_t next = 0u;
        if (file_read_all(file, &chunk, sizeof(chunk)) != 0)
            goto fail;
        offset += sizeof(chunk);
        next = offset + chunk.size;
        if (next > file_size)
            goto fail;

        if (chunk.type == DIMG_CHUNK_DOC)
        {
            dimg_document_disk disk;
            if (have_document || chunk.size != sizeof(disk) || file_read_all(file, &disk, sizeof(disk)) != 0 ||
                !disk.canvas_w || !disk.canvas_h || disk.canvas_w > 8192u || disk.canvas_h > 8192u ||
                disk.layer_count > EDITOR_MAX_LAYERS || disk.point_count > EDITOR_MAX_POINTS ||
                (disk.selected_layer < -1 || disk.selected_layer >= (int32_t)disk.layer_count) ||
                (disk.rotation != 0u && disk.rotation != 90u &&
                 disk.rotation != 180u && disk.rotation != 270u) ||
                disk.crop_x < 0 || disk.crop_y < 0 || !disk.crop_w || !disk.crop_h ||
                (uint64_t)(uint32_t)disk.crop_x + disk.crop_w > disk.canvas_w ||
                (uint64_t)(uint32_t)disk.crop_y + disk.crop_h > disk.canvas_h ||
                disk.resize_w > 8192u || disk.resize_h > 8192u ||
                (!!disk.resize_w != !!disk.resize_h))
                goto fail;
            g_doc.canvas_w = disk.canvas_w;
            g_doc.canvas_h = disk.canvas_h;
            g_doc.layer_count = disk.layer_count;
            g_doc.selected_layer = disk.selected_layer;
            g_doc.point_count = disk.point_count;
            g_doc.crop_x = disk.crop_x;
            g_doc.crop_y = disk.crop_y;
            g_doc.crop_w = disk.crop_w;
            g_doc.crop_h = disk.crop_h;
            g_doc.resize_w = disk.resize_w;
            g_doc.resize_h = disk.resize_h;
            g_doc.rotation = disk.rotation;
            g_doc.flip_x = disk.flip_x;
            g_doc.flip_y = disk.flip_y;
            have_document = 1u;
        }
        else if (chunk.type == DIMG_CHUNK_LAYERS)
        {
            if (!have_document || have_layers ||
                chunk.size != g_doc.layer_count * sizeof(dimg_layer_disk))
                goto fail;
            uint32_t raster_count = 0u;
            for (uint32_t i = 0u; i < g_doc.layer_count; ++i)
            {
                dimg_layer_disk disk;
                if (file_read_all(file, &disk, sizeof(disk)) != 0 ||
                    disk.type < EDITOR_LAYER_RASTER || disk.type > EDITOR_LAYER_ADJUSTMENT ||
                    disk.point_start > g_doc.point_count ||
                    disk.point_count > g_doc.point_count - disk.point_start)
                    goto fail;
                if (disk.type == EDITOR_LAYER_RASTER &&
                    (++raster_count > EDITOR_MAX_RASTER_LAYERS || !disk.raster_w || !disk.raster_h ||
                     disk.raster_w > 8192u || disk.raster_h > 8192u))
                    goto fail;
                layer_from_disk(&disk, &g_doc.layers[i]);
            }
            have_layers = 1u;
        }
        else if (chunk.type == DIMG_CHUNK_POINTS)
        {
            if (!have_document || have_points ||
                chunk.size != g_doc.point_count * sizeof(editor_point) ||
                (chunk.size && file_read_all(file, g_doc.points, chunk.size) != 0))
                goto fail;
            have_points = 1u;
        }
        else if (chunk.type == DIMG_CHUNK_PNG)
        {
            uint64_t png_offset = offset;
            uint32_t layer_index = 0u;
            if (!have_layers || chunk.size < sizeof(layer_index) ||
                file_read_all(file, &layer_index, sizeof(layer_index)) != 0 ||
                layer_index >= g_doc.layer_count ||
                g_doc.layers[layer_index].type != EDITOR_LAYER_RASTER ||
                raster_loaded[layer_index] ||
                g_api->file_seek(file, png_offset) != 0 ||
                read_png_chunk(file, chunk.size) != 0)
                goto fail;
            {
                uint32_t image_w = 0u;
                uint32_t image_h = 0u;
                if (g_api->img_size(g_doc.layers[layer_index].image, &image_w, &image_h) != 0 ||
                    image_w != g_doc.layers[layer_index].raster_w ||
                    image_h != g_doc.layers[layer_index].raster_h)
                    goto fail;
            }
            raster_loaded[layer_index] = 1u;
        }
        else if (g_api->file_seek(file, next) != 0)
        {
            goto fail;
        }
        offset = next;
    }
    g_api->file_close(file);
    file = 0u;
    if (offset != file_size || !have_document || !have_layers ||
        (g_doc.point_count && !have_points))
        goto fail;
    for (uint32_t i = 0u; i < g_doc.layer_count; ++i)
        if (g_doc.layers[i].type == EDITOR_LAYER_RASTER && !raster_loaded[i])
            goto fail;
    if (
        g_api->img_create(g_doc.canvas_w, g_doc.canvas_h, 0u, &g_doc.preview) != 0 ||
        g_api->img_create(g_doc.canvas_w, g_doc.canvas_h, 0u, &g_doc.scratch) != 0 ||
        g_api->img_pixels(g_doc.preview, &g_doc.preview_pixels, &g_doc.preview_stride) != 0 ||
        g_api->img_pixels(g_doc.scratch, &g_doc.scratch_pixels, &g_doc.scratch_stride) != 0)
        goto fail;
    editor_copy_text(g_doc.project_raw, sizeof(g_doc.project_raw), raw_path);
    editor_copy_text(g_doc.project_friendly, sizeof(g_doc.project_friendly), friendly_path ? friendly_path : raw_path);
    document_history_reset();
    g_doc.dirty = 0u;
    document_request_render();
    document_dispose(&G_previous_document);
    return 0;

fail:
    if (file)
        g_api->file_close(file);
    if (switched_document)
    {
        document_dispose(&g_doc);
        g_doc = G_previous_document;
        editor_zero_memory(&G_previous_document, sizeof(G_previous_document));
    }
    return -1;
}

int project_export(const char *raw_path, uint32_t format, uint32_t quality)
{
    char temp_path[EDITOR_PATH_CAP];
    uint32_t flattened = 0u;
    int rc = -1;
    if (!raw_path || !raw_path[0] || document_export_flattened(&flattened) != 0)
        return -1;
    editor_copy_text(temp_path, sizeof(temp_path), raw_path);
    editor_append_text(temp_path, sizeof(temp_path), ".tmp");
    if (text_equal(temp_path, raw_path))
    {
        g_api->img_destroy(flattened);
        return -1;
    }
    (void)g_api->file_unlink(temp_path);
    rc = g_api->img_save(flattened, temp_path, format, quality);
    g_api->img_destroy(flattened);
    if (rc == 0)
        rc = replace_file(temp_path, raw_path);
    if (rc != 0)
        (void)g_api->file_unlink(temp_path);
    return rc;
}

int project_export_begin_async(const char *raw_path, uint32_t format, uint32_t quality)
{
    if (G_export_job.phase != ASYNC_EXPORT_IDLE || !raw_path || !raw_path[0] ||
        !SACX_API_HAS(g_api, img_save))
        return -1;
    editor_zero_memory(&G_export_job, sizeof(G_export_job));
    editor_copy_text(G_export_job.raw_path, sizeof(G_export_job.raw_path), raw_path);
    editor_copy_text(G_export_job.temp_path, sizeof(G_export_job.temp_path), raw_path);
    editor_append_text(G_export_job.temp_path, sizeof(G_export_job.temp_path), ".tmp");
    if (text_equal(G_export_job.temp_path, G_export_job.raw_path))
        return -1;
    G_export_job.format = format;
    G_export_job.quality = quality;
    G_export_job.phase = ASYNC_EXPORT_START_WAIT;
    return 0;
}

int project_export_step_async(uint32_t row_budget, uint32_t *out_progress)
{
    int rc = 1;
    if (out_progress)
        *out_progress = 0u;
    if (G_export_job.phase == ASYNC_EXPORT_IDLE)
        return 0;
    if (!row_budget)
        row_budget = 1u;

    if (G_export_job.phase == ASYNC_EXPORT_START_WAIT)
    {
        G_export_job.phase = ASYNC_EXPORT_PREPARE;
        return 1;
    }
    if (G_export_job.phase == ASYNC_EXPORT_PREPARE)
    {
        (void)g_api->file_unlink(G_export_job.temp_path);
        G_export_job.phase = ASYNC_EXPORT_RENDER;
        if (out_progress)
            *out_progress = 5u;
        return 1;
    }
    else if (G_export_job.phase == ASYNC_EXPORT_RENDER)
    {
        if (g_doc.render_pending && document_render_step(row_budget))
        {
            if (out_progress)
                *out_progress = 10u;
            return 1;
        }
        G_export_job.phase = ASYNC_EXPORT_FLATTEN_BEGIN;
        if (out_progress)
            *out_progress = 10u;
        return 1;
    }
    else if (G_export_job.phase == ASYNC_EXPORT_FLATTEN_BEGIN)
    {
        if (document_export_begin(&G_export_job.image, &G_export_job.width,
                                  &G_export_job.height) != 0)
            goto fail;
        G_export_job.phase = ASYNC_EXPORT_FLATTEN;
        if (out_progress)
            *out_progress = 15u;
        return 1;
    }
    else if (G_export_job.phase == ASYNC_EXPORT_FLATTEN)
    {
        uint32_t rows = G_export_job.height - G_export_job.next_row;
        if (rows > row_budget)
            rows = row_budget;
        if (document_export_step(G_export_job.image, G_export_job.next_row, rows) != 0)
            goto fail;
        G_export_job.next_row += rows;
        if (out_progress)
            *out_progress = 15u + (uint32_t)((uint64_t)G_export_job.next_row * 65u /
                                             G_export_job.height);
        if (G_export_job.next_row < G_export_job.height)
            return 1;
        (void)g_api->img_touch(G_export_job.image);
        G_export_job.phase = ASYNC_EXPORT_ENCODE_WAIT;
        return 1;
    }
    else if (G_export_job.phase == ASYNC_EXPORT_ENCODE_WAIT)
    {
        G_export_job.phase = ASYNC_EXPORT_ENCODE;
        if (out_progress)
            *out_progress = 85u;
        return 1;
    }
    else if (G_export_job.phase == ASYNC_EXPORT_ENCODE)
    {
        if (out_progress)
            *out_progress = 90u;
        rc = g_api->img_save(G_export_job.image, G_export_job.temp_path,
                             G_export_job.format, G_export_job.quality);
        if (rc == 0)
            rc = replace_file(G_export_job.temp_path, G_export_job.raw_path);
        if (rc != 0)
            goto fail;
        g_api->img_destroy(G_export_job.image);
        editor_zero_memory(&G_export_job, sizeof(G_export_job));
        if (out_progress)
            *out_progress = 100u;
        return 0;
    }

    return 1;

fail:
    if (G_export_job.image)
        g_api->img_destroy(G_export_job.image);
    (void)g_api->file_unlink(G_export_job.temp_path);
    editor_zero_memory(&G_export_job, sizeof(G_export_job));
    return -1;
}

int project_export_active(void)
{
    return G_export_job.phase != ASYNC_EXPORT_IDLE;
}
