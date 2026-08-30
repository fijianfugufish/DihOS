#include <stdbool.h>
#include <stdlib.h>

#include "dihscover.h"
#include "utils/errors.h"
#include "utils/utils.h"

#include "netsurf/bitmap.h"
#include "netsurf/content.h"
#include "netsurf/plotters.h"
#include "content/content_factory.h"
#include "content/content_protected.h"
#include "desktop/bitmap.h"
#include "desktop/gui_internal.h"
#include "image/image.h"

#ifdef __cplusplus
extern "C" {
#endif
extern const sacx_api *dihscover_netsurf_api(void);
extern void *memcpy(void *dst, const void *src, __SIZE_TYPE__ count);
#ifdef __cplusplus
}
#endif

/* DihOS already has safe in-memory decoders for PNG, JPEG, WebP and SVG.
 * This content handler bridges their ARGB output into NetSurf's bitmap API. */
typedef struct dihos_image_content {
    struct content base;
    struct bitmap *bitmap;
} dihos_image_content;

static nserror dihos_image_create(const struct content_handler *handler,
        lwc_string *mime, const struct http_parameter *params,
        struct llcache_handle *llcache, const char *fallback_charset,
        bool quirks, struct content **out)
{
    dihos_image_content *image = calloc(1, sizeof(*image));
    nserror error;
    if (!image) return NSERROR_NOMEM;
    error = content__init(&image->base, handler, mime, params, llcache,
            fallback_charset, quirks);
    if (error != NSERROR_OK) { free(image); return error; }
    *out = &image->base;
    return NSERROR_OK;
}

static bool dihos_image_complete(struct content *content)
{
    dihos_image_content *image = (dihos_image_content *)content;
    const sacx_api *api = dihscover_netsurf_api();
    const uint8_t *data;
    size_t size;
    uint32_t decoded = 0, width = 0, height = 0, stride = 0;
    uint32_t *pixels = 0, *target;

    data = content__get_source_data(content, &size);
    if (!api || !data || !size || size > 16u * 1024u * 1024u ||
            !SACX_API_HAS(api, img_load_memory) || !SACX_API_HAS(api, img_size) ||
            !SACX_API_HAS(api, img_pixels) ||
            api->img_load_memory(data, (uint32_t)size, &decoded) != 0 ||
            api->img_size(decoded, &width, &height) != 0 || !width || !height ||
            width > 8192u || height > 8192u ||
            api->img_pixels(decoded, &pixels, &stride) != 0 || !pixels || stride < width) {
        if (decoded && api && SACX_API_HAS(api, img_destroy)) (void)api->img_destroy(decoded);
        content_broadcast_error(content, NSERROR_UNKNOWN, "DihOS could not decode image");
        return false;
    }

    image->bitmap = guit->bitmap->create((int)width, (int)height, BITMAP_CLEAR);
    if (!image->bitmap) {
        (void)api->img_destroy(decoded);
        content_broadcast_error(content, NSERROR_NOMEM, NULL);
        return false;
    }
    target = (uint32_t *)guit->bitmap->get_buffer(image->bitmap);
    if (!target) {
        guit->bitmap->destroy(image->bitmap); image->bitmap = NULL;
        (void)api->img_destroy(decoded);
        content_broadcast_error(content, NSERROR_NOMEM, NULL);
        return false;
    }
    for (uint32_t row = 0; row < height; ++row)
        memcpy(target + (uint64_t)row * width, pixels + (uint64_t)row * stride,
                (size_t)width * sizeof(uint32_t));
    (void)api->img_destroy(decoded);

    content->width = (int)width;
    content->height = (int)height;
    content->size += width * height * 4u;
    content_set_ready(content);
    content_set_done(content);
    return true;
}

static bool dihos_image_redraw(struct content *content,
        struct content_redraw_data *data, const struct rect *clip,
        const struct redraw_context *ctx)
{
    dihos_image_content *image = (dihos_image_content *)content;
    return image->bitmap && image_bitmap_plot(image->bitmap, data, clip, ctx);
}

static void dihos_image_destroy(struct content *content)
{
    dihos_image_content *image = (dihos_image_content *)content;
    if (image->bitmap) guit->bitmap->destroy(image->bitmap);
}

static content_type dihos_image_type(void) { return CONTENT_IMAGE; }
static bool dihos_image_opaque(struct content *content) { (void)content; return false; }

static const struct content_handler dihos_image_handler = {
    .create = dihos_image_create,
    .data_complete = dihos_image_complete,
    .destroy = dihos_image_destroy,
    .redraw = dihos_image_redraw,
    .type = dihos_image_type,
    .is_opaque = dihos_image_opaque,
};

static const char *dihos_image_types[] = {
    "image/png", "image/jpeg", "image/jpg", "image/webp", "image/svg+xml",
    "image/x-icon", "image/vnd.microsoft.icon"
};

CONTENT_FACTORY_REGISTER_TYPES(dihos_image, dihos_image_types, dihos_image_handler);
