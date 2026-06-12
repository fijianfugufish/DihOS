#include <stddef.h>
#include <stdint.h>

#include "kwrappers/kimg.h"
#include "kwrappers/kfile.h"
#include "kwrappers/string.h"
#include "memory/pmem.h"
#include "system/kbusy.h"

typedef struct
{
    uint64_t pages;
    uint32_t size;
} kimg_write_alloc_hdr;

static void *kimg_write_malloc(size_t size)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;
    kimg_write_alloc_hdr *hdr = 0;

    if (!size || size > 0x7FFFFFFFu)
        return 0;
    bytes = (uint64_t)size + sizeof(*hdr);
    pages = (bytes + 4095ull) >> 12;
    hdr = (kimg_write_alloc_hdr *)pmem_alloc_pages(pages);
    if (!hdr)
        return 0;
    hdr->pages = pages;
    hdr->size = (uint32_t)size;
    return hdr + 1;
}

static void kimg_write_free(void *ptr)
{
    kimg_write_alloc_hdr *hdr = 0;
    if (!ptr)
        return;
    hdr = (kimg_write_alloc_hdr *)ptr - 1;
    if (hdr->pages)
        pmem_free_pages(hdr, hdr->pages);
}

static void *kimg_write_realloc(void *ptr, size_t new_size)
{
    kimg_write_alloc_hdr *old_hdr = 0;
    void *replacement = 0;
    uint32_t copy_size = 0u;

    if (!ptr)
        return kimg_write_malloc(new_size);
    if (!new_size)
    {
        kimg_write_free(ptr);
        return 0;
    }

    old_hdr = (kimg_write_alloc_hdr *)ptr - 1;
    replacement = kimg_write_malloc(new_size);
    if (!replacement)
        return 0;
    copy_size = old_hdr->size < (uint32_t)new_size ? old_hdr->size : (uint32_t)new_size;
    memcpy(replacement, ptr, copy_size);
    kimg_write_free(ptr);
    return replacement;
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_ASSERT(x) ((void)0)
#define STBIW_MALLOC(sz) kimg_write_malloc(sz)
#define STBIW_REALLOC(p, sz) kimg_write_realloc((p), (sz))
#define STBIW_FREE(p) kimg_write_free(p)
#include "third_party/stb_image_write.h"

typedef struct
{
    KFile file;
    uint8_t failed;
    uint32_t bytes_since_pump;
} kimg_writer;

static int writer_write(kimg_writer *writer, const void *data, uint32_t size)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t total = 0u;

    if (!writer || writer->failed || (!data && size))
        return -1;

    while (total < size)
    {
        uint32_t written = 0u;
        if (kfile_write(&writer->file, src + total, size - total, &written) != 0 || written == 0u)
        {
            writer->failed = 1u;
            return -1;
        }
        total += written;
        writer->bytes_since_pump += written;
        if (writer->bytes_since_pump >= 65536u)
        {
            writer->bytes_since_pump = 0u;
            kbusy_pump();
        }
    }
    return 0;
}

static void stbi_writer_callback(void *context, void *data, int size)
{
    kimg_writer *writer = (kimg_writer *)context;
    if (!writer || size < 0)
        return;
    (void)writer_write(writer, data, (uint32_t)size);
}

static void write_u32be(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void write_u32le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void write_u16le(uint8_t out[2], uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
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

static void adler32_update(uint32_t *a, uint32_t *b, const uint8_t *data, uint32_t size)
{
    uint32_t av = *a;
    uint32_t bv = *b;

    for (uint32_t i = 0u; i < size; ++i)
    {
        av += data[i];
        if (av >= 65521u)
            av -= 65521u;
        bv += av;
        if (bv >= 65521u)
            bv %= 65521u;
    }
    *a = av;
    *b = bv;
}

static int png_write_chunk(kimg_writer *writer, const char type[4], const uint8_t *data, uint32_t size)
{
    uint8_t length_bytes[4];
    uint8_t crc_bytes[4];
    uint32_t crc = 0xFFFFFFFFu;

    write_u32be(length_bytes, size);
    crc = crc32_update(crc, (const uint8_t *)type, 4u);
    if (data && size)
        crc = crc32_update(crc, data, size);
    crc = ~crc;
    write_u32be(crc_bytes, crc);

    if (writer_write(writer, length_bytes, sizeof(length_bytes)) != 0 ||
        writer_write(writer, type, 4u) != 0 ||
        (size && writer_write(writer, data, size) != 0) ||
        writer_write(writer, crc_bytes, sizeof(crc_bytes)) != 0)
        return -1;
    return 0;
}

static int png_write_idat(const kimg *img, kimg_writer *writer)
{
    uint64_t row_bytes64 = (uint64_t)img->w * 4ull + 1ull;
    uint64_t data_size64 = 2ull + (uint64_t)img->h * (5ull + row_bytes64) + 4ull;
    uint8_t length_bytes[4];
    uint8_t crc_bytes[4];
    uint8_t zlib_header[2] = {0x78u, 0x01u};
    uint8_t block_header[5];
    uint8_t converted[4096];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t adler_a = 1u;
    uint32_t adler_b = 0u;

    if (row_bytes64 > 65535ull || data_size64 > 0xFFFFFFFFull)
        return -1;

    write_u32be(length_bytes, (uint32_t)data_size64);
    crc = crc32_update(crc, (const uint8_t *)"IDAT", 4u);
    if (writer_write(writer, length_bytes, 4u) != 0 ||
        writer_write(writer, "IDAT", 4u) != 0 ||
        writer_write(writer, zlib_header, 2u) != 0)
        return -1;
    crc = crc32_update(crc, zlib_header, 2u);

    for (uint32_t y = 0u; y < img->h; ++y)
    {
        uint16_t row_len = (uint16_t)row_bytes64;
        uint16_t row_nlen = (uint16_t)~row_len;
        uint8_t filter = 0u;
        const uint32_t *row = img->px + (uint64_t)y * img->w;
        uint32_t converted_used = 0u;

        block_header[0] = (y + 1u == img->h) ? 0x01u : 0x00u;
        write_u16le(block_header + 1u, row_len);
        write_u16le(block_header + 3u, row_nlen);
        if (writer_write(writer, block_header, 5u) != 0 ||
            writer_write(writer, &filter, 1u) != 0)
            return -1;
        crc = crc32_update(crc, block_header, 5u);
        crc = crc32_update(crc, &filter, 1u);
        adler32_update(&adler_a, &adler_b, &filter, 1u);

        for (uint32_t x = 0u; x < img->w; ++x)
        {
            uint32_t pixel = row[x];
            converted[converted_used++] = (uint8_t)(pixel >> 16);
            converted[converted_used++] = (uint8_t)(pixel >> 8);
            converted[converted_used++] = (uint8_t)pixel;
            converted[converted_used++] = (uint8_t)(pixel >> 24);

            if (converted_used == sizeof(converted) || x + 1u == img->w)
            {
                if (writer_write(writer, converted, converted_used) != 0)
                    return -1;
                crc = crc32_update(crc, converted, converted_used);
                adler32_update(&adler_a, &adler_b, converted, converted_used);
                converted_used = 0u;
            }
        }
    }

    {
        uint8_t adler_bytes[4];
        write_u32be(adler_bytes, (adler_b << 16) | adler_a);
        if (writer_write(writer, adler_bytes, 4u) != 0)
            return -1;
        crc = crc32_update(crc, adler_bytes, 4u);
    }

    write_u32be(crc_bytes, ~crc);
    return writer_write(writer, crc_bytes, 4u);
}

static int kimg_write_png(const kimg *img, kimg_writer *writer)
{
    static const uint8_t signature[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    uint8_t ihdr[13];

    write_u32be(ihdr + 0u, img->w);
    write_u32be(ihdr + 4u, img->h);
    ihdr[8] = 8u;
    ihdr[9] = 6u;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;

    if (writer_write(writer, signature, sizeof(signature)) != 0 ||
        png_write_chunk(writer, "IHDR", ihdr, sizeof(ihdr)) != 0 ||
        png_write_idat(img, writer) != 0 ||
        png_write_chunk(writer, "IEND", 0, 0u) != 0)
        return -1;
    return 0;
}

static int kimg_write_bmp(const kimg *img, kimg_writer *writer)
{
    uint64_t pixel_bytes64 = (uint64_t)img->w * (uint64_t)img->h * 4ull;
    uint64_t file_size64 = 54ull + pixel_bytes64;
    uint8_t header[54];
    uint8_t converted[4096];

    if (file_size64 > 0xFFFFFFFFull)
        return -1;
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    write_u32le(header + 2u, (uint32_t)file_size64);
    write_u32le(header + 10u, 54u);
    write_u32le(header + 14u, 40u);
    write_u32le(header + 18u, img->w);
    write_u32le(header + 22u, (uint32_t)(-(int32_t)img->h));
    write_u16le(header + 26u, 1u);
    write_u16le(header + 28u, 32u);
    write_u32le(header + 34u, (uint32_t)pixel_bytes64);

    if (writer_write(writer, header, sizeof(header)) != 0)
        return -1;

    for (uint32_t y = 0u; y < img->h; ++y)
    {
        const uint32_t *row = img->px + (uint64_t)y * img->w;
        uint32_t used = 0u;
        for (uint32_t x = 0u; x < img->w; ++x)
        {
            uint32_t pixel = row[x];
            converted[used++] = (uint8_t)pixel;
            converted[used++] = (uint8_t)(pixel >> 8);
            converted[used++] = (uint8_t)(pixel >> 16);
            converted[used++] = (uint8_t)(pixel >> 24);
            if (used == sizeof(converted) || x + 1u == img->w)
            {
                if (writer_write(writer, converted, used) != 0)
                    return -1;
                used = 0u;
            }
        }
    }
    return 0;
}

static int kimg_write_jpeg(const kimg *img, kimg_writer *writer, uint32_t quality)
{
    uint64_t rgb_bytes = (uint64_t)img->w * (uint64_t)img->h * 3ull;
    uint64_t pages = 0u;
    uint8_t *rgb = 0;

    if (rgb_bytes == 0u || rgb_bytes > 256ull * 1024ull * 1024ull)
        return -1;
    pages = (rgb_bytes + 4095ull) >> 12;
    rgb = (uint8_t *)pmem_alloc_pages(pages);
    if (!rgb)
        return -1;

    for (uint32_t y = 0u; y < img->h; ++y)
    {
        for (uint32_t x = 0u; x < img->w; ++x)
        {
            uint64_t i = (uint64_t)y * img->w + x;
            uint32_t pixel = img->px[i];
            uint32_t alpha = pixel >> 24;
            uint32_t inv = 255u - alpha;
            rgb[i * 3ull + 0ull] = (uint8_t)((((pixel >> 16) & 0xFFu) * alpha + 255u * inv + 127u) / 255u);
            rgb[i * 3ull + 1ull] = (uint8_t)((((pixel >> 8) & 0xFFu) * alpha + 255u * inv + 127u) / 255u);
            rgb[i * 3ull + 2ull] = (uint8_t)(((pixel & 0xFFu) * alpha + 255u * inv + 127u) / 255u);
        }
        if ((y & 31u) == 31u)
            kbusy_pump();
    }

    if (quality < 1u)
        quality = 90u;
    if (quality > 100u)
        quality = 100u;
    if (!stbi_write_jpg_to_func(stbi_writer_callback, writer, (int)img->w, (int)img->h,
                                3, rgb, (int)quality))
        writer->failed = 1u;

    pmem_free_pages(rgb, pages);
    return writer->failed ? -1 : 0;
}

int kimg_save(const kimg *img, const char *path, uint32_t format, uint32_t quality)
{
    kimg_writer writer;
    int rc = -1;

    if (!img || !img->px || !img->w || !img->h || !path || !path[0])
        return -1;
    if (img->w > 8192u || img->h > 8192u)
        return -1;

    memset(&writer, 0, sizeof(writer));
    if (kfile_open(&writer.file, path, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
        return -1;

    kbusy_begin();
    if (format == KIMG_FORMAT_PNG)
        rc = kimg_write_png(img, &writer);
    else if (format == KIMG_FORMAT_JPEG)
        rc = kimg_write_jpeg(img, &writer, quality);
    else if (format == KIMG_FORMAT_BMP)
        rc = kimg_write_bmp(img, &writer);

    kfile_close(&writer.file);
    kbusy_end();
    if (writer.failed)
        rc = -1;
    return rc;
}
