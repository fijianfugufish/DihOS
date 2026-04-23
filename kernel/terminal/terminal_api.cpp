#include "terminal/terminal.hpp"

extern "C"
{
#include "kwrappers/ktext.h"
}

static Terminal g_terminal;

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
    g_terminal.Print(s);
}

extern "C" void terminal_print_inline(const char *s)
{
    g_terminal.PrintInline(s);
}

extern "C" void terminal_warn(const char *s)
{
    g_terminal.Warn(s);
}

extern "C" void terminal_error(const char *s)
{
    g_terminal.Error(s);
}

extern "C" void terminal_success(const char *s)
{
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
    g_terminal.Print(buf);
}

extern "C" void terminal_print_hex32(uint32_t v)
{
    char buf[11];
    hex_to_str32(v, buf);
    g_terminal.Print(buf);
}

extern "C" void terminal_print_hex8(uint8_t v)
{
    char buf[5];
    hex_to_str8(v, buf);
    g_terminal.Print(buf);
}

extern "C" void terminal_print_inline_hex64(uint64_t v)
{
    char buf[19];
    hex_to_str64(v, buf);
    g_terminal.PrintInline(buf);
}

extern "C" void terminal_print_inline_hex32(uint32_t v)
{
    char buf[11];
    hex_to_str32(v, buf);
    g_terminal.PrintInline(buf);
}

extern "C" void terminal_print_inline_hex8(uint8_t v)
{
    char buf[5];
    hex_to_str8(v, buf);
    g_terminal.PrintInline(buf);
}
