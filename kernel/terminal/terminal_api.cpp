#include "terminal/terminal.hpp"
#include "terminal/terminal_api.h"

extern "C"
{
#include "kwrappers/ktext.h"
#include "kwrappers/string.h"
}

static const int MAX_TERMINALS = 6;
static Terminal g_terminals[MAX_TERMINALS];
static kfont *g_terminal_font = 0;
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
    g_terminal_font = font;
    g_terminals[0].Initialize(font, "Terminal", 0);
}

extern "C" void terminal_clear(void)
{
    g_terminals[0].Clear();
}

extern "C" void terminal_clear_no_flush(void)
{
    g_terminals[0].ClearNoFlush();
}

extern "C" void terminal_flush_log(void)
{
    g_terminals[0].FlushLog();
}

extern "C" void terminal_print(const char *s)
{
    terminal_capture_feed("", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminals[0].Print(s);
}

extern "C" void terminal_print_inline(const char *s)
{
    terminal_capture_feed("", s, 0u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminals[0].PrintInline(s);
}

extern "C" void terminal_warn(const char *s)
{
    terminal_capture_feed("[WARN] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminals[0].Warn(s);
}

extern "C" void terminal_error(const char *s)
{
    terminal_capture_feed("[ERROR] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminals[0].Error(s);
}

extern "C" void terminal_success(const char *s)
{
    terminal_capture_feed("[SUCCESS] ", s, 1u);
    if (g_capture_sink && !g_capture_mirror)
        return;
    g_terminals[0].Success(s);
}

extern "C" void terminal_update_input(void)
{
    for (int i = 0; i < MAX_TERMINALS; ++i)
    {
        if (!g_terminals[i].Initialized())
            continue;
        g_terminals[i].UpdateScript();
        g_terminals[i].UpdateSacx();
        g_terminals[i].UpdateInput();
    }
}

extern "C" void terminal_toggle_quiet()
{
    g_terminals[0].ToggleQuiet();
}

extern "C" void terminal_set_quiet()
{
    g_terminals[0].SetQuiet();
}

extern "C" void terminal_set_loud()
{
    g_terminals[0].SetLoud();
}

extern "C" void terminal_activate(void)
{
    g_terminals[0].Activate();
}

extern "C" int terminal_visible(void)
{
    return g_terminals[0].Visible();
}

static int terminal_open_script_internal(const char *raw_path, const char *friendly_path, uint32_t flags)
{
    if (!g_terminal_font || !raw_path || !raw_path[0])
        return -1;

    for (int i = 1; i < MAX_TERMINALS; ++i)
    {
        if (!g_terminals[i].Initialized())
            continue;

        if (g_terminals[i].ProgramActive())
            continue;

        if (g_terminals[i].StartProgram(raw_path, friendly_path, flags) == 0)
            return 0;

        if (!g_terminals[i].Visible())
        {
            g_terminals[i].Initialize(g_terminal_font, "SAC Script", i);
            if (g_terminals[i].Initialized() &&
                !g_terminals[i].ProgramActive() &&
                g_terminals[i].StartProgram(raw_path, friendly_path, flags) == 0)
                return 0;
        }
    }

    for (int i = 1; i < MAX_TERMINALS; ++i)
    {
        if (!g_terminals[i].Initialized())
        {
            g_terminals[i].Initialize(g_terminal_font, "SAC Script", i);
            if (g_terminals[i].Initialized() &&
                g_terminals[i].StartProgram(raw_path, friendly_path, flags) == 0)
                return 0;
        }
    }

    g_terminals[0].Error("no free app terminals");

    return -1;
}

extern "C" int terminal_open_script_ex(const char *raw_path, const char *friendly_path, uint32_t flags)
{
    return terminal_open_script_internal(raw_path, friendly_path, flags);
}

extern "C" int terminal_open_program_ex(const char *raw_path, const char *friendly_path, uint32_t flags)
{
    return terminal_open_script_internal(raw_path, friendly_path, flags);
}

extern "C" int terminal_open_script(const char *raw_path, const char *friendly_path)
{
    return terminal_open_script_internal(raw_path, friendly_path, TERMINAL_OPEN_FLAG_NONE);
}

extern "C" int terminal_open_program(const char *raw_path, const char *friendly_path)
{
    return terminal_open_script_internal(raw_path, friendly_path, TERMINAL_OPEN_FLAG_NONE);
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
