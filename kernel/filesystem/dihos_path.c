#include "filesystem/dihos_path.h"

#include "kwrappers/string.h"

static void dihos_path_copy_trunc(char *dst, uint32_t cap, const char *src)
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

static int dihos_path_append_cstr(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0u;
    uint32_t i = 0u;

    if (!dst || !src || cap == 0u)
        return -1;

    len = (uint32_t)strlen(dst);
    while (src[i] && len + i + 1u < cap)
    {
        dst[len + i] = src[i];
        ++i;
    }

    if (src[i] != 0)
        return -1;

    dst[len + i] = 0;
    return 0;
}

int dihos_path_raw_to_friendly(const char *raw, char *out, uint32_t cap)
{
    if (!raw || !out || cap < 2u)
        return -1;

    if (strncmp(raw, "0:/", 3u) != 0)
        return -1;

    if (!raw[3])
    {
        out[0] = '/';
        out[1] = 0;
        return 0;
    }

    out[0] = '/';
    dihos_path_copy_trunc(out + 1u, cap - 1u, raw + 3u);
    return 0;
}

int dihos_path_canonicalize_friendly(const char *source, char *out, uint32_t cap)
{
    uint32_t restore[32];
    uint32_t restore_count = 0u;
    uint32_t out_len = 1u;
    const char *p = source;

    if (!source || !out || cap < 2u || source[0] != '/')
        return -1;

    out[0] = '/';
    out[1] = 0;

    while (*p)
    {
        char segment[64];
        uint32_t seg_len = 0u;
        uint32_t restore_len = out_len;

        while (*p == '/')
            ++p;

        if (!*p)
            break;

        while (*p && *p != '/' && seg_len + 1u < sizeof(segment))
            segment[seg_len++] = *p++;

        while (*p && *p != '/')
            ++p;

        segment[seg_len] = 0;

        if (strcmp(segment, ".") == 0 || segment[0] == 0)
            continue;

        if (strcmp(segment, "..") == 0)
        {
            if (restore_count > 0u)
            {
                out_len = restore[--restore_count];
                out[out_len] = 0;
            }
            continue;
        }

        if (restore_count >= (uint32_t)(sizeof(restore) / sizeof(restore[0])))
            return -1;

        if (out_len > 1u)
        {
            if (out_len + 1u >= cap)
                return -1;
            out[out_len++] = '/';
        }

        if (out_len + seg_len >= cap)
            return -1;

        memcpy(out + out_len, segment, seg_len);
        out_len += seg_len;
        out[out_len] = 0;
        restore[restore_count++] = restore_len;
    }

    return 0;
}

int dihos_path_resolve_friendly(const char *cwd, const char *input, char *out, uint32_t cap)
{
    char joined[DIHOS_PATH_CAP * 2u];
    const char *base = (cwd && cwd[0]) ? cwd : "/";

    if (!out || cap == 0u)
        return -1;

    if (!input || !input[0])
    {
        dihos_path_copy_trunc(joined, sizeof(joined), base);
        return dihos_path_canonicalize_friendly(joined, out, cap);
    }

    if (strncmp(input, "0:/", 3u) == 0)
    {
        if (dihos_path_raw_to_friendly(input, joined, sizeof(joined)) != 0)
            return -1;
        return dihos_path_canonicalize_friendly(joined, out, cap);
    }

    if (input[0] == '/')
        return dihos_path_canonicalize_friendly(input, out, cap);

    joined[0] = 0;
    dihos_path_copy_trunc(joined, sizeof(joined), base);

    if (strcmp(joined, "/") != 0 && dihos_path_append_cstr(joined, sizeof(joined), "/") != 0)
        return -1;

    if (dihos_path_append_cstr(joined, sizeof(joined), input) != 0)
        return -1;

    return dihos_path_canonicalize_friendly(joined, out, cap);
}

int dihos_path_friendly_to_raw(const char *friendly, char *raw, uint32_t cap)
{
    if (!friendly || !raw || cap < 4u || friendly[0] != '/')
        return -1;

    if (strcmp(friendly, "/") == 0)
    {
        dihos_path_copy_trunc(raw, cap, "0:/");
        return 0;
    }

    if ((uint32_t)strlen(friendly) + 3u >= cap)
        return -1;

    raw[0] = '0';
    raw[1] = ':';
    dihos_path_copy_trunc(raw + 2u, cap - 2u, friendly);
    return 0;
}

int dihos_path_resolve_raw(const char *cwd, const char *input,
                           char *friendly, uint32_t friendly_cap,
                           char *raw, uint32_t raw_cap)
{
    if (dihos_path_resolve_friendly(cwd, input, friendly, friendly_cap) != 0)
        return -1;

    return dihos_path_friendly_to_raw(friendly, raw, raw_cap);
}

int dihos_path_join_raw(const char *base, const char *name, char *out, uint32_t cap)
{
    uint32_t base_len = 0u;

    if (!base || !name || !out || cap == 0u)
        return -1;

    base_len = (uint32_t)strlen(base);
    if (base_len + (uint32_t)strlen(name) + 2u >= cap)
        return -1;

    dihos_path_copy_trunc(out, cap, base);
    if (strcmp(base, "0:/") != 0 && dihos_path_append_cstr(out, cap, "/") != 0)
        return -1;
    if (dihos_path_append_cstr(out, cap, name) != 0)
        return -1;
    return 0;
}

int dihos_path_join_friendly(const char *base, const char *name, char *out, uint32_t cap)
{
    char joined[DIHOS_PATH_CAP * 2u];

    if (!base || !name || !out || cap == 0u || base[0] != '/')
        return -1;

    joined[0] = 0;
    dihos_path_copy_trunc(joined, sizeof(joined), base);
    if (strcmp(joined, "/") != 0 && dihos_path_append_cstr(joined, sizeof(joined), "/") != 0)
        return -1;
    if (dihos_path_append_cstr(joined, sizeof(joined), name) != 0)
        return -1;

    return dihos_path_canonicalize_friendly(joined, out, cap);
}
