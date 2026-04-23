#include "terminal/terminal.hpp"

extern "C"
{
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "kwrappers/colors.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"
}

static char g_log_line[16384];
static int g_log_line_len = 0;
static int g_log_line_open = 0;

static char g_log_pending[262144];
static int g_log_pending_len = 0;

static int terminal_quiet = 0;

static void log_line_clear(void)
{
    g_log_line_len = 0;
    g_log_line_open = 0;
    g_log_line[0] = 0;
}

static void log_pending_clear(void)
{
    g_log_pending_len = 0;
    g_log_pending[0] = 0;
}

static void log_line_append(const char *text)
{
    if (!text)
        return;

    int i = 0;
    while (text[i] && g_log_line_len < (int)sizeof(g_log_line) - 1)
    {
        g_log_line[g_log_line_len++] = text[i++];
    }
    g_log_line[g_log_line_len] = 0;
}

static void log_pending_append_raw(const char *text, int len)
{
    int i = 0;
    while (i < len && g_log_pending_len < (int)sizeof(g_log_pending) - 1)
    {
        g_log_pending[g_log_pending_len++] = text[i++];
    }
    g_log_pending[g_log_pending_len] = 0;
}

static void log_commit_open_line(void)
{
    if (!g_log_line_open || g_log_line_len <= 0)
        return;

    log_pending_append_raw(g_log_line, g_log_line_len);
    log_pending_append_raw("\r\n", 2);

    log_line_clear();
}

static int cstr_len(const char *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static void logbuf_clear(void)
{
    g_log_line_len = 0;
    g_log_line_open = 0;
    g_log_line[0] = 0;
}

static void logbuf_append(const char *text)
{
    if (!text)
        return;

    int i = 0;
    while (text[i] && g_log_line_len < (int)sizeof(g_log_line) - 1)
    {
        g_log_line[g_log_line_len++] = text[i++];
    }

    g_log_line[g_log_line_len] = 0;
}

static void terminal_log_flush_open_line(void)
{
    if (!g_log_line_open || g_log_line_len <= 0)
        return;

    KFile f;
    if (kfile_open(&f, "0:/OS/System/Logs/terminal.txt",
                   KFILE_WRITE | KFILE_CREATE | KFILE_APPEND) == 0)
    {
        uint32_t written = 0;

        kfile_write(&f, g_log_line, (uint32_t)g_log_line_len, &written);
        kfile_write(&f, "\r\n", 2, &written);

        kfile_close(&f);
    }

    logbuf_clear();
}

static void terminal_log_start_line(const char *tag, const char *msg)
{
    log_commit_open_line();

    log_line_append(tag);
    if (msg)
        log_line_append(msg);

    g_log_line_open = 1;
}

static void terminal_log_append_inline(const char *msg)
{
    if (!msg)
        return;

    if (!g_log_line_open)
    {
        log_line_append("[TERMINAL] ");
        g_log_line_open = 1;
    }

    log_line_append(msg);
}

static void terminal_log_flush_pending(void)
{
    log_commit_open_line();

    if (g_log_pending_len <= 0)
        return;

    KFile f;
    if (kfile_open(&f, "0:/OS/System/Logs/terminal.txt",
                   KFILE_WRITE | KFILE_CREATE | KFILE_APPEND) != 0)
        return;

    uint32_t written = 0;
    kfile_write(&f, g_log_pending, (uint32_t)g_log_pending_len, &written);
    kfile_close(&f);

    log_pending_clear();
}

Terminal::Terminal()
{
    ResetState();
}

void Terminal::FlushLog()
{
    terminal_log_flush_pending();
}

void Terminal::ResetState()
{
    x = 1000;
    y = 10;
    z = 20;
    width = 600;
    height = 300;
    scale = 1;
    active = 1;

    padding_x = 8;
    padding_y = 8;
    line_spacing = 2;

    line_count = 0;
    font_ptr = 0;
    w = 0;
    scroll_y = 0;
    text_base_y = 0;

    window.idx = -1;
    text_handle.idx = -1;
    text_ptr = 0;

    text_len = 0;
    text_storage[0] = 0;

    log_line_clear();
    log_pending_clear();
    logbuf_clear();
}

void Terminal::Initialize(kfont *font)
{
    ResetState();
    font_ptr = font;

    kwindow_style style = kwindow_style_default();
    window = kwindow_create(x, y, width, height, z, font_ptr, "Terminal", &style);

    text_handle = kgfx_obj_add_text(
        font_ptr,
        text_storage,
        padding_x,
        padding_y * 2,
        0,
        white,
        255,
        scale,
        1,
        int(padding_y / 2),
        KTEXT_ALIGN_LEFT,
        active);

    kgfx_obj_set_parent(text_handle, kwindow_root(window));

    text_ptr = kgfx_obj_ref(text_handle);
    text_base_y = padding_y * 2;
    scroll_y = 0;

    KFile f;
    if (kfile_open(&f, "0:/OS/System/Logs/terminal.txt",
                   KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) == 0)
    {
        kfile_close(&f);
    }

    log_line_clear();
    log_pending_clear();
    logbuf_clear();
}

static void append_cstr_safe(char *dst, int *dst_len, int cap, const char *src)
{
    if (!dst || !dst_len || !src || cap <= 0)
        return;

    int i = 0;
    while (src[i] && *dst_len < cap - 1)
    {
        dst[*dst_len] = src[i];
        (*dst_len)++;
        i++;
    }
    dst[*dst_len] = 0;
}

void Terminal::Clear()
{
    terminal_log_flush_pending();

    text_len = 0;
    text_storage[0] = 0;
    line_count = 0;

    if (text_ptr)
        text_ptr->u.text.text = text_storage;
}

void Terminal::ClearNoFlush()
{
    text_len = 0;
    text_storage[0] = 0;
    line_count = 0;

    if (text_ptr)
        text_ptr->u.text.text = text_storage;
}

void Terminal::AddLine(const char *text, kcolor color)
{
    if (!font_ptr || !text || !text_ptr)
        return;

    if (text_len > 0 && text_len < TEXT_CAP - 1)
    {
        text_storage[text_len++] = '\n';
        text_storage[text_len] = 0;
    }

    append_cstr_safe(text_storage, &text_len, TEXT_CAP, text);
    text_ptr->u.text.text = text_storage;
    text_ptr->fill = color;

    line_count++;
}

void Terminal::PrintInline(const char *text)
{
    if (terminal_quiet)
        return;

    if (!text || !text_ptr)
        return;

    if (text_len == 0)
    {
        Print(text);
        return;
    }

    append_cstr_safe(text_storage, &text_len, TEXT_CAP, text);
    text_ptr->u.text.text = text_storage;

    terminal_log_append_inline(text);
}

void Terminal::Print(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_start_line("", text);
    AddLine(text, white);
}

void Terminal::Warn(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_start_line("[WARN] ", text);
    AddLine(text, orange);
}

void Terminal::Error(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_start_line("[ERROR] ", text);
    AddLine(text, red);
}

void Terminal::Success(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_start_line("[SUCCESS] ", text);
    AddLine(text, green);
}

void Terminal::ToggleQuiet()
{
    if (terminal_quiet)
        terminal_quiet = 0;
    else
        terminal_quiet = 1;
}

void Terminal::SetQuiet() { terminal_quiet = 1; }

void Terminal::SetLoud() { terminal_quiet = 0; }

void Terminal::UpdateInput()
{
    kmouse_state mouse = {0};
    kgfx_obj *root = 0;

    if (!text_ptr)
        return;

    kmouse_get_state(&mouse);
    if (mouse.wheel == 0)
        return;

    root = kgfx_obj_ref(kwindow_root(window));
    if (!root || root->kind != KGFX_OBJ_RECT || !root->visible)
        return;

    if (mouse.x < root->u.rect.x || mouse.y < root->u.rect.y ||
        mouse.x >= root->u.rect.x + (int32_t)root->u.rect.w ||
        mouse.y >= root->u.rect.y + (int32_t)root->u.rect.h)
        return;

    scroll_y += mouse.wheel * 24;
    text_ptr->u.text.y = text_base_y + scroll_y;
}
