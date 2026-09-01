#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "mesart_runtime.h"

/* Minimal C/runtime hooks needed by the first upstream Mesa slice.  Mesart
 * intentionally reports no environment variables, so Mesa's optional debug
 * overrides are inert and cannot change the verified renderer's behaviour. */
static mesart_runtime g_mesa_heap;
static uint8_t g_mesa_heap_ready;

typedef struct mesart_mesa_allocation
{
    size_t bytes;
} mesart_mesa_allocation;

static int mesart_mesa_heap_init(void)
{
    if (g_mesa_heap_ready)
        return 0;
    if (mesart_runtime_init(&g_mesa_heap) != 0)
        return -1;
    g_mesa_heap_ready = 1u;
    return 0;
}

void *memmove(void *destination, const void *source, size_t bytes)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;

    if (out == in || !bytes)
        return destination;
    if (out < in)
        while (bytes--)
            *out++ = *in++;
    else
    {
        out += bytes;
        in += bytes;
        while (bytes--)
            *--out = *--in;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t bytes)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    while (bytes--)
        if (*a++ != *b++)
            return (int)a[-1] - (int)b[-1];
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0u;

    while (text && text[length])
        ++length;
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left && *left == *right)
    {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t bytes)
{
    while (bytes && *left && *left == *right)
    {
        ++left;
        ++right;
        --bytes;
    }
    return bytes ? (unsigned char)*left - (unsigned char)*right : 0;
}

void *malloc(size_t bytes)
{
    mesart_mesa_allocation *allocation;

    if (!bytes || bytes > (size_t)-1 - sizeof(*allocation) ||
        mesart_mesa_heap_init() != 0)
        return 0;
    allocation = (mesart_mesa_allocation *)mesart_runtime_alloc(
        &g_mesa_heap, sizeof(*allocation) + bytes);
    if (!allocation)
        return 0;
    allocation->bytes = bytes;
    return allocation + 1;
}

void *calloc(size_t count, size_t bytes)
{
    size_t total;
    void *memory;

    if (count && bytes > (size_t)-1 / count)
        return 0;
    total = count * bytes;
    memory = malloc(total);
    if (memory)
        memset(memory, 0, total);
    return memory;
}

void *realloc(void *memory, size_t bytes)
{
    void *replacement;

    if (!memory)
        return malloc(bytes);
    if (!bytes)
        return 0;
    const mesart_mesa_allocation *old =
        (const mesart_mesa_allocation *)memory - 1;
    replacement = malloc(bytes);
    if (replacement)
        memcpy(replacement, memory, old->bytes < bytes ? old->bytes : bytes);
    return replacement;
}

void free(void *memory)
{
    (void)memory;
}

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

char *strdup(const char *text)
{
    size_t bytes = strlen(text) + 1u;
    char *copy = (char *)malloc(bytes);

    return copy ? (char *)memcpy(copy, text, bytes) : 0;
}

static int mesart_is_delimiter(char value, const char *delimiters)
{
    while (*delimiters)
        if (value == *delimiters++)
            return 1;
    return 0;
}

char *strtok_r(char *text, const char *delimiters, char **save)
{
    char *at = text ? text : (save ? *save : 0);
    char *begin;

    if (!at || !delimiters || !save)
        return 0;
    while (*at && mesart_is_delimiter(*at, delimiters))
        ++at;
    if (!*at)
    {
        *save = at;
        return 0;
    }
    begin = at;
    while (*at && !mesart_is_delimiter(*at, delimiters))
        ++at;
    if (*at)
        *at++ = 0;
    *save = at;
    return begin;
}

void abort(void)
{
    __builtin_trap();
}

void exit(int status)
{
    (void)status;
    __builtin_trap();
}

const char *debug_get_option(const char *name, const char *dfault)
{
    (void)name;
    return dfault;
}

bool debug_parse_bool_option(const char *text, bool dfault)
{
    (void)text;
    return dfault;
}

int64_t debug_parse_num_option(const char *text, int64_t dfault)
{
    (void)text;
    return dfault;
}

void mesa_log(int level, const char *tag, const char *format, ...)
{
    va_list args;

    (void)level;
    (void)tag;
    (void)format;
    va_start(args, format);
    va_end(args);
}
