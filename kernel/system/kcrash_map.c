#include "system/kcrash_map.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"

typedef struct kcrash_map_entry
{
    uint64_t address;
    uint32_t text_offset;
} kcrash_map_entry;

static char *g_text;
static kcrash_map_entry *g_entries;
static uint32_t g_count;

static uint64_t parse_hex64(const char *p)
{
    uint64_t value = 0u;
    for (uint32_t i = 0u; i < 16u; ++i)
    {
        char c = p[i];
        uint32_t digit = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') :
                         (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) : 0u;
        value = (value << 4u) | digit;
    }
    return value;
}

static void copy_field(char *out, uint32_t cap, const char **cursor)
{
    uint32_t n = 0u;
    const char *p = *cursor;
    while (*p && *p != '\t' && *p != '\n')
    {
        if (n + 1u < cap)
            out[n++] = *p;
        ++p;
    }
    out[n] = 0;
    if (*p == '\t')
        ++p;
    *cursor = p;
}

int kcrash_map_load(const char *path)
{
    KFile file;
    uint64_t size64;
    uint32_t size, used = 0u, lines = 0u, entry = 0u;

    if (!path || g_text || kfile_open(&file, path, KFILE_READ) != 0)
        return -1;
    size64 = kfile_size(&file);
    if (!size64 || size64 > 0x7fffffffu)
    {
        kfile_close(&file);
        return -1;
    }
    size = (uint32_t)size64;
    g_text = (char *)pmem_alloc_pages(((uint64_t)size + 1u + 4095u) >> 12u);
    if (!g_text)
    {
        kfile_close(&file);
        return -1;
    }
    while (used < size)
    {
        uint32_t read = 0u, want = size - used;
        if (want > 65536u) want = 65536u;
        if (kfile_read(&file, g_text + used, want, &read) != 0 || read != want)
        {
            kfile_close(&file);
            return -1;
        }
        used += read;
    }
    kfile_close(&file);
    g_text[size] = 0;
    for (uint32_t i = 0u; i < size; ++i)
        if (g_text[i] == '\n') ++lines;
    if (!lines)
        return -1;
    g_entries = (kcrash_map_entry *)pmem_alloc_pages(((uint64_t)lines * sizeof(*g_entries) + 4095u) >> 12u);
    if (!g_entries)
        return -1;
    for (uint32_t offset = 0u; offset < size && entry < lines; )
    {
        if (size - offset < 17u || g_text[offset + 16u] != '\t')
            return -1;
        g_entries[entry].address = parse_hex64(g_text + offset);
        g_entries[entry].text_offset = offset;
        ++entry;
        while (offset < size && g_text[offset] != '\n') ++offset;
        if (offset < size) ++offset;
    }
    g_count = entry;
    return g_count ? 0 : -1;
}

int kcrash_map_lookup(uint64_t runtime_pc, uint64_t load_base, kcrash_location *out)
{
    uint64_t address;
    uint32_t lo = 0u, hi = g_count;
    const char *p;
    if (!out || !g_entries || runtime_pc < load_base)
        return -1;
    address = runtime_pc - load_base;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (g_entries[mid].address <= address) lo = mid + 1u; else hi = mid;
    }
    if (!lo)
        return -1;
    p = g_text + g_entries[lo - 1u].text_offset + 17u;
    copy_field(out->function, sizeof(out->function), &p);
    copy_field(out->file, sizeof(out->file), &p);
    out->line = 0u;
    while (*p >= '0' && *p <= '9')
        out->line = out->line * 10u + (uint32_t)(*p++ - '0');
    return out->file[0] ? 0 : -1;
}
