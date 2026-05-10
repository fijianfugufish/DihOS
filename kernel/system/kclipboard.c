#include "system/kclipboard.h"

#ifndef KCLIPBOARD_CAP
#define KCLIPBOARD_CAP 4096u
#endif

static char G_clipboard[KCLIPBOARD_CAP];
static uint32_t G_clipboard_len = 0u;

uint32_t kclipboard_set_text(const char *text, uint32_t len)
{
    uint32_t copy_len = 0u;

    if (!text)
    {
        G_clipboard[0] = 0;
        G_clipboard_len = 0u;
        return 0u;
    }

    copy_len = len;
    if (copy_len >= KCLIPBOARD_CAP)
        copy_len = KCLIPBOARD_CAP - 1u;

    for (uint32_t i = 0u; i < copy_len; ++i)
        G_clipboard[i] = text[i];
    G_clipboard[copy_len] = 0;
    G_clipboard_len = copy_len;
    return copy_len;
}

uint32_t kclipboard_copy_text(char *out, uint32_t cap)
{
    uint32_t copy_len = 0u;

    if (!out || cap == 0u)
        return 0u;

    copy_len = G_clipboard_len;
    if (copy_len >= cap)
        copy_len = cap - 1u;

    for (uint32_t i = 0u; i < copy_len; ++i)
        out[i] = G_clipboard[i];
    out[copy_len] = 0;
    return copy_len;
}

uint32_t kclipboard_length(void)
{
    return G_clipboard_len;
}

