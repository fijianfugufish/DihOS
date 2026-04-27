#include "terminal/terminal.hpp"
#include "terminal/terminal_api.h"

extern "C"
{
#include "kwrappers/ktext.h"
#include "kwrappers/string.h"
}

static Terminal g_terminal;
static terminal_capture_sink_fn g_capture_sink = 0;
static void *g_capture_user = 0;
static uint8_t g_capture_mirror = 0;

static void terminal_capture_feed(const char *prefix, const char *text, uint8_t append_newline)
{
    uint32_t text_len = 0u;

    if (!g_capture_sink)
        return;

    if (prefix && prefix[0])
        g_capture_sink(prefix, (uint32_t)strlen(prefix), g_capture_user);
    if (text && text[0])
    {
        text_len = (uint32_t)strlen(text);
        g_capture_sink(text, text_len, g_capture_user);
    }
    if (append_newline && (!text || text_len == 0u || text[text_len - 1u] != '\n'))
        g_capture_sink("\n", 1u, g_capture_user);
}

extern "C" void terminal_initialize(kfont *font)
{
    g_terminal.Initialize(font);
}

extern "C" void terminal_clear(void)
{
    g_terminal.Clear();
}

extern "C" void terminal_clear_no_flush(void)
{
    g_terminal.ClearNoFlush();
}

extern "C" void terminal_flush_log(void)
{
    g_terminal.FlushLog();
}

extern "C" void terminal_print(const char *s)
{
    terminal_capture_feed("", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminal.Print(s);
}

extern "C" void terminal_print_inline(const char *s)
{
    terminal_capture_feed("", s, 0u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminal.PrintInline(s);
}

extern "C" void terminal_warn(const char *s)
{
    terminal_capture_feed("[WARN] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminal.Warn(s);
}

extern "C" void terminal_error(const char *s)
{
    terminal_capture_feed("[ERROR] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminal.Error(s);
}

extern "C" void terminal_success(const char *s)
{
    terminal_capture_feed("[SUCCESS] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminal.Success(s);
}

extern "C" void terminal_update_input(void)
{
    g_terminal.UpdateInput();
}

extern "C" void terminal_toggle_quiet()
{
    g_terminal.ToggleQuiet();
}

extern "C" void terminal_set_quiet()
{
    g_terminal.SetQuiet();
}

extern "C" void terminal_set_loud()
{
    g_terminal.SetLoud();
}

extern "C" void terminal_capture_begin(uint8_t mirror_to_terminal, terminal_capture_sink_fn sink, void *user)
{
    g_capture_mirror = mirror_to_terminal ? 1u : 0u;
    g_capture_sink = sink;
    g_capture_user = user;
}

extern "C" void terminal_capture_end(void)
{
    g_capture_mirror = 0u;
    g_capture_sink = 0;
    g_capture_user = 0;
}

static void hex_to_str64(uint64_t v, char *out)
{
    const char *hex = "0123456789ABCDEF";

    out[0] = '0';
    out[1] = 'x';

    for (int i = 0; i < 16; i++)
    {
        int shift = (15 - i) * 4;
        out[2 + i] = hex[(v >> shift) & 0xF];
    }

    out[18] = 0;
}

static void hex_to_str32(uint32_t v, char *out)
{
    const char *hex = "0123456789ABCDEF";

    out[0] = '0';
    out[1] = 'x';

    for (int i = 0; i < 8; i++)
    {
        int shift = (7 - i) * 4;
        out[2 + i] = hex[(v >> shift) & 0xF];
    }

    out[10] = 0;
}

static void hex_to_str8(uint8_t v, char *out)
{
    const char *hex = "0123456789ABCDEF";

    out[0] = '0';
    out[1] = 'x';
    out[2] = hex[(v >> 4) & 0xF];
    out[3] = hex[v & 0xF];
    out[4] = 0;
}

extern "C" void terminal_print_hex64(uint64_t v)
{
    char buf[19];
    hex_to_str64(v, buf);
    terminal_print(buf);
}

extern "C" void terminal_print_hex32(uint32_t v)
{
    char buf[11];
    hex_to_str32(v, buf);
    terminal_print(buf);
}

extern "C" void terminal_print_hex8(uint32_t v)
{
    char buf[5];
    hex_to_str8((uint8_t)v, buf);
    terminal_print(buf);
}

extern "C" void terminal_print_inline_hex64(uint64_t v)
{
    char buf[19];
    hex_to_str64(v, buf);
    terminal_print_inline(buf);
}

extern "C" void terminal_print_inline_hex32(uint32_t v)
{
    char buf[11];
    hex_to_str32(v, buf);
    terminal_print_inline(buf);
}

extern "C" void terminal_print_inline_hex8(uint32_t v)
{
    char buf[5];
    hex_to_str8((uint8_t)v, buf);
    terminal_print_inline(buf);
}
