#include <cstdio>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "../sdk/sacx/apps/image_viewer/editor.h"

namespace
{
struct mock_image
{
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector<uint32_t> pixels;
};

static mock_image images[4096];
static FILE *files[64];
static uint64_t ticks = 1;
static int failures = 0;
static const std::filesystem::path test_root =
    std::filesystem::temp_directory_path() /
    ("image_editor_test_" + std::to_string(
         std::chrono::high_resolution_clock::now().time_since_epoch().count()));

static std::string host_path(const char *path)
{
    std::string value = path ? path : "";
    if (value.rfind("0:/", 0) == 0)
        return (test_root / value.substr(3)).string();
    return value;
}

static void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int mock_file_open(const char *path, uint32_t flags, uint32_t *out)
{
    std::string translated = host_path(path);
    std::filesystem::create_directories(std::filesystem::path(translated).parent_path());
    const char *mode = "rb";
    if (flags & SACX_FILE_WRITE)
    {
        if (flags & SACX_FILE_APPEND)
            mode = "ab+";
        else if (flags & SACX_FILE_TRUNC)
            mode = "wb+";
        else
            mode = "rb+";
    }
    FILE *file = std::fopen(translated.c_str(), mode);
    if (!file && (flags & SACX_FILE_CREATE))
        file = std::fopen(translated.c_str(), "wb+");
    if (!file)
        return -1;
    for (uint32_t i = 0; i < 64; ++i)
    {
        if (!files[i])
        {
            files[i] = file;
            *out = i + 1;
            return 0;
        }
    }
    std::fclose(file);
    return -1;
}

static int mock_file_read(uint32_t handle, void *buf, uint32_t size, uint32_t *out)
{
    if (!handle || handle > 64 || !files[handle - 1])
        return -1;
    *out = (uint32_t)std::fread(buf, 1, size, files[handle - 1]);
    return std::ferror(files[handle - 1]) ? -1 : 0;
}

static int mock_file_write(uint32_t handle, const void *buf, uint32_t size, uint32_t *out)
{
    if (!handle || handle > 64 || !files[handle - 1])
        return -1;
    *out = (uint32_t)std::fwrite(buf, 1, size, files[handle - 1]);
    return *out == size ? 0 : -1;
}

static int mock_file_seek(uint32_t handle, uint64_t offset)
{
    if (!handle || handle > 64 || !files[handle - 1] || offset > 0x7FFFFFFFu)
        return -1;
    return std::fseek(files[handle - 1], (long)offset, SEEK_SET);
}

static uint64_t mock_file_size(uint32_t handle)
{
    if (!handle || handle > 64 || !files[handle - 1])
        return 0;
    long position = std::ftell(files[handle - 1]);
    std::fseek(files[handle - 1], 0, SEEK_END);
    long size = std::ftell(files[handle - 1]);
    std::fseek(files[handle - 1], position, SEEK_SET);
    return size < 0 ? 0u : (uint64_t)size;
}

static int mock_file_close(uint32_t handle)
{
    if (!handle || handle > 64 || !files[handle - 1])
        return -1;
    std::fclose(files[handle - 1]);
    files[handle - 1] = nullptr;
    return 0;
}

static int mock_file_unlink(const char *path)
{
    return std::remove(host_path(path).c_str());
}

static int mock_file_rename(const char *source, const char *destination)
{
    std::string src = host_path(source);
    std::string dst = host_path(destination);
    return std::rename(src.c_str(), dst.c_str());
}

static uint64_t mock_ticks()
{
    return ticks++;
}

static int mock_img_create(uint32_t w, uint32_t h, uint32_t argb, uint32_t *out)
{
    if (!w || !h || !out)
        return -1;
    for (uint32_t i = 0; i < 4096; ++i)
    {
        if (!images[i].w)
        {
            images[i].w = w;
            images[i].h = h;
            images[i].pixels.assign((size_t)w * h, argb);
            *out = i + 1;
            return 0;
        }
    }
    return -1;
}

static int mock_img_destroy(uint32_t handle)
{
    if (!handle || handle > 4096 || !images[handle - 1].w)
        return -1;
    images[handle - 1] = mock_image{};
    return 0;
}

static int mock_img_size(uint32_t handle, uint32_t *w, uint32_t *h)
{
    if (!handle || handle > 4096 || !images[handle - 1].w)
        return -1;
    *w = images[handle - 1].w;
    *h = images[handle - 1].h;
    return 0;
}

static int mock_img_pixels(uint32_t handle, uint32_t **pixels, uint32_t *stride)
{
    if (!handle || handle > 4096 || !images[handle - 1].w)
        return -1;
    *pixels = images[handle - 1].pixels.data();
    *stride = images[handle - 1].w;
    return 0;
}

static int mock_img_touch(uint32_t)
{
    return 0;
}

static int mock_img_draw_text(uint32_t, int32_t, int32_t, const char *,
                              sacx_color, uint8_t, uint32_t)
{
    return 0;
}

static int mock_img_save(uint32_t handle, const char *path, uint32_t, uint32_t)
{
    if (!handle || handle > 4096 || !images[handle - 1].w)
        return -1;
    std::string translated = host_path(path);
    std::filesystem::create_directories(std::filesystem::path(translated).parent_path());
    FILE *file = std::fopen(translated.c_str(), "wb");
    if (!file)
        return -1;
    const uint32_t header[3] = {0x474D4954u, images[handle - 1].w, images[handle - 1].h};
    bool ok = std::fwrite(header, sizeof(header), 1, file) == 1 &&
              std::fwrite(images[handle - 1].pixels.data(), sizeof(uint32_t),
                          images[handle - 1].pixels.size(), file) == images[handle - 1].pixels.size();
    std::fclose(file);
    return ok ? 0 : -1;
}

static int mock_img_load(const char *path, uint32_t *out)
{
    FILE *file = std::fopen(host_path(path).c_str(), "rb");
    uint32_t header[3];
    if (!file || std::fread(header, sizeof(header), 1, file) != 1 || header[0] != 0x474D4954u ||
        mock_img_create(header[1], header[2], 0u, out) != 0)
    {
        if (file)
            std::fclose(file);
        return -1;
    }
    mock_image &image = images[*out - 1];
    bool ok = std::fread(image.pixels.data(), sizeof(uint32_t), image.pixels.size(), file) ==
              image.pixels.size();
    std::fclose(file);
    if (!ok)
    {
        mock_img_destroy(*out);
        return -1;
    }
    return 0;
}

static sacx_api make_api()
{
    sacx_api api{};
    api.abi_version = SACX_API_ABI_VERSION;
    api.struct_size = sizeof(api);
    api.time_ticks = mock_ticks;
    api.file_open = mock_file_open;
    api.file_read = mock_file_read;
    api.file_write = mock_file_write;
    api.file_seek = mock_file_seek;
    api.file_size = mock_file_size;
    api.file_close = mock_file_close;
    api.file_unlink = mock_file_unlink;
    api.file_rename = mock_file_rename;
    api.img_load = mock_img_load;
    api.img_destroy = mock_img_destroy;
    api.img_size = mock_img_size;
    api.img_create = mock_img_create;
    api.img_pixels = mock_img_pixels;
    api.img_touch = mock_img_touch;
    api.img_save = mock_img_save;
    api.img_draw_text = mock_img_draw_text;
    return api;
}

static void render_all()
{
    while (document_render_step(8u))
    {
    }
}

static uint32_t new_image(uint32_t w, uint32_t h, const std::vector<uint32_t> &pixels)
{
    uint32_t handle = 0;
    check(mock_img_create(w, h, 0u, &handle) == 0, "create test image");
    images[handle - 1].pixels = pixels;
    return handle;
}

static void test_blending_and_order()
{
    uint32_t black = new_image(1, 1, {0xFF000000u});
    check(document_open_image(black, "", "") == 0, "open base image");
    uint32_t white = new_image(1, 1, {0xFFFFFFFFu});
    check(document_add_raster(white, 0, 0) == 0, "add white raster");
    g_doc.layers[g_doc.selected_layer].opacity = 128u;
    document_commit();
    render_all();
    uint32_t pixel = g_doc.preview_pixels[0];
    check(((pixel >> 16) & 0xFFu) >= 127u && ((pixel >> 16) & 0xFFu) <= 129u,
          "alpha compositing");

    uint32_t red = new_image(1, 1, {0xFFFF0000u});
    uint32_t blue = new_image(1, 1, {0xFF0000FFu});
    check(document_add_raster(red, 0, 0) == 0, "add red raster");
    document_commit();
    check(document_add_raster(blue, 0, 0) == 0, "add blue raster");
    document_commit();
    render_all();
    check(g_doc.preview_pixels[0] == 0xFF0000FFu, "top layer wins");
    document_move_selected(-1);
    render_all();
    check(g_doc.preview_pixels[0] == 0xFFFF0000u, "layer reorder changes composition");
}

static void test_transforms_and_undo()
{
    uint32_t source = new_image(2, 1, {0xFFFF0000u, 0xFF00FF00u});
    check(document_open_image(source, "", "") == 0, "open transform image");
    render_all();
    g_doc.rotation = 90u;
    uint32_t flattened = 0;
    check(document_export_flattened(&flattened) == 0, "export rotated image");
    check(images[flattened - 1].w == 1u && images[flattened - 1].h == 2u,
          "rotation swaps dimensions");
    check(images[flattened - 1].pixels[0] == 0xFFFF0000u &&
              images[flattened - 1].pixels[1] == 0xFF00FF00u,
          "rotation maps pixels");
    mock_img_destroy(flattened);
    g_doc.resize_w = 2u;
    g_doc.resize_h = 4u;
    check(document_export_flattened(&flattened) == 0, "export resized image");
    check(images[flattened - 1].w == 2u && images[flattened - 1].h == 4u,
          "non-destructive resize controls export dimensions");
    mock_img_destroy(flattened);

    int layer = document_add_layer(EDITOR_LAYER_RECT);
    check(layer >= 0, "add undo layer");
    g_doc.layers[layer].x0 = 3;
    document_commit();
    g_doc.layers[layer].x0 = 9;
    document_commit();
    check(document_undo() == 1 && g_doc.layers[layer].x0 == 3, "undo coalesced edit");
    check(document_redo() == 1 && g_doc.layers[layer].x0 == 9, "redo coalesced edit");

    g_doc.dirty_bounds_valid = 0u;
    document_request_render_rect(1, 0, 1u, 1u);
    document_request_render_rect(-2, -2, 3u, 3u);
    check(g_doc.dirty_bounds_valid && g_doc.dirty_x0 == 0 && g_doc.dirty_y0 == 0 &&
              g_doc.dirty_x1 == 1 && g_doc.dirty_y1 == 0,
          "dirty rectangle union and clipping");
}

static void test_effects_and_crop()
{
    uint32_t source = new_image(4, 2, {
        0xFF100000u, 0xFF200000u, 0xFF300000u, 0xFF400000u,
        0xFF500000u, 0xFF600000u, 0xFF700000u, 0xFF800000u,
    });
    check(document_open_image(source, "", "") == 0, "open pixelate image");
    int pixelate = document_add_layer(EDITOR_LAYER_PIXELATE);
    check(pixelate >= 0, "add pixelate layer");
    g_doc.layers[pixelate].x0 = 0;
    g_doc.layers[pixelate].y0 = 0;
    g_doc.layers[pixelate].x1 = 3;
    g_doc.layers[pixelate].y1 = 1;
    g_doc.layers[pixelate].effect_strength = 2u;
    document_commit();
    document_render_now();
    check(g_doc.preview_pixels[0] == 0xFF100000u &&
              g_doc.preview_pixels[1] == 0xFF100000u &&
              g_doc.preview_pixels[4] == 0xFF100000u,
          "pixelate block size controls sampled pixels");
    check(g_doc.preview_pixels[2] == 0xFF300000u &&
              g_doc.preview_pixels[3] == 0xFF300000u &&
              g_doc.preview_pixels[6] == 0xFF300000u,
          "pixelate preserves separate blocks");

    source = new_image(3, 1, {0xFF000000u, 0xFFFFFFFFu, 0xFF000000u});
    check(document_open_image(source, "", "") == 0, "open blur image");
    int blur = document_add_layer(EDITOR_LAYER_BLUR);
    check(blur >= 0, "add blur layer");
    g_doc.layers[blur].x0 = 0;
    g_doc.layers[blur].y0 = 0;
    g_doc.layers[blur].x1 = 2;
    g_doc.layers[blur].y1 = 0;
    g_doc.layers[blur].effect_strength = 1u;
    document_commit();
    document_render_now();
    uint32_t center = (g_doc.preview_pixels[1] >> 16) & 0xFFu;
    check(center >= 84u && center <= 86u, "blur radius produces box average");

    source = new_image(3, 2, {
        0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu,
        0xFFFFFFFFu, 0xFF808080u, 0xFF000000u,
    });
    check(document_open_image(source, "", "") == 0, "open crop image");
    g_doc.crop_x = 1;
    g_doc.crop_y = 0;
    g_doc.crop_w = 2u;
    g_doc.crop_h = 2u;
    document_commit();
    uint32_t flattened = 0u;
    check(document_export_flattened(&flattened) == 0, "export cropped image");
    check(images[flattened - 1].w == 2u && images[flattened - 1].h == 2u,
          "crop controls exported dimensions");
    check(images[flattened - 1].pixels[0] == 0xFF00FF00u &&
              images[flattened - 1].pixels[1] == 0xFF0000FFu &&
              images[flattened - 1].pixels[2] == 0xFF808080u &&
              images[flattened - 1].pixels[3] == 0xFF000000u,
          "crop exports selected pixels");
    mock_img_destroy(flattened);
}

static void test_highlighter_defaults_and_async_export()
{
    const char *path = "0:/async-export.png";
    uint32_t source = new_image(3, 2, {
        0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu,
        0xFFFFFFFFu, 0xFF808080u, 0xFF000000u,
    });
    check(document_open_image(source, "", "") == 0, "open async export image");
    int layer = document_add_layer(EDITOR_LAYER_HIGHLIGHTER);
    check(layer >= 0, "add highlighter");
    check(g_doc.layers[layer].color == 0xFFFFD60Au &&
              g_doc.layers[layer].stroke == 20u &&
              g_doc.layers[layer].opacity == 110u,
          "highlighter defaults are thick yellow and transparent");
    document_commit();

    check(project_export_begin_async(path, SACX_IMG_FORMAT_PNG, 90u) == 0,
          "begin async export");
    check(project_export_active(), "async export reports active");
    uint32_t previous_progress = 0u;
    uint32_t steps = 0u;
    int rc = 1;
    while (rc > 0 && steps < 64u)
    {
        uint32_t progress = 0u;
        rc = project_export_step_async(1u, &progress);
        check(progress >= previous_progress, "async export progress is monotonic");
        previous_progress = progress;
        ++steps;
    }
    check(rc == 0 && !project_export_active(), "async export completes");
    check(steps > 3u, "async export yields across multiple steps");
    check(std::filesystem::exists(host_path(path)), "async export replaces destination");
}

static void test_project_round_trip_and_corruption()
{
    const char *path = "0:/roundtrip.dimg";
    uint32_t source = new_image(2, 2, {
        0xFFFF0000u, 0xFF00FF00u,
        0xFF0000FFu, 0xFFFFFFFFu,
    });
    check(document_open_image(source, "0:/source.png", "/source.png") == 0, "open project image");
    int layer = document_add_layer(EDITOR_LAYER_RECT);
    g_doc.layers[layer].x0 = 0;
    g_doc.layers[layer].y0 = 0;
    g_doc.layers[layer].x1 = 1;
    g_doc.layers[layer].y1 = 1;
    g_doc.resize_w = 4u;
    g_doc.resize_h = 4u;
    document_commit();
    uint32_t saved_layers = g_doc.layer_count;
    check(project_save(path, "/roundtrip.dimg") == 0, "save dimg");
    check(document_add_layer(EDITOR_LAYER_ARROW) >= 0, "mutate after save");
    document_commit();
    check(project_load(path, "/roundtrip.dimg") == 0, "load dimg");
    check(g_doc.layer_count == saved_layers && g_doc.resize_w == 4u &&
              g_doc.resize_h == 4u && g_doc.dirty == 0u,
          "dimg round trip");

    check(document_add_layer(EDITOR_LAYER_ELLIPSE) >= 0, "prepare preserved document");
    document_commit();
    uint32_t preserved_layers = g_doc.layer_count;
    std::string translated = host_path(path);
    FILE *file = std::fopen(translated.c_str(), "rb+");
    check(file != nullptr, "open dimg for corruption");
    if (file)
    {
        std::fseek(file, -1, SEEK_END);
        int byte = std::fgetc(file);
        std::fseek(file, -1, SEEK_END);
        std::fputc(byte ^ 0x5Au, file);
        std::fclose(file);
    }
    check(project_load(path, "/roundtrip.dimg") != 0, "reject corrupt dimg");
    check(g_doc.layer_count == preserved_layers, "corrupt load preserves current document");
}
}

int main()
{
    static sacx_api api = make_api();
    g_api = &api;
    document_reset();
    test_blending_and_order();
    test_transforms_and_undo();
    test_effects_and_crop();
    test_highlighter_defaults_and_async_export();
    test_project_round_trip_and_corruption();
    document_reset();
    std::filesystem::remove_all(test_root);
    if (failures)
    {
        std::fprintf(stderr, "%d image editor test(s) failed\n", failures);
        return 1;
    }
    std::puts("image editor tests passed");
    return 0;
}
