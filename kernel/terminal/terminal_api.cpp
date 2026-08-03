#include "terminal/terminal.hpp"
#include "terminal/terminal_api.h"

extern "C"
{
#include "kwrappers/colors.h"
#include "kwrappers/ktext.h"
#include "kwrappers/string.h"
#include "system/dihos_time.h"
}

static const int MAX_TERMINALS = 6;
static Terminal g_terminals[MAX_TERMINALS];
static kfont *g_terminal_font = 0;
static terminal_capture_sink_fn g_capture_sink = 0;
static void *g_capture_user = 0;
static uint8_t g_capture_mirror = 0;

static uint8_t g_installfx_active = 0u;
static uint32_t g_installfx_flags = 0u;
static uint64_t g_installfx_start_tick = 0u;
static uint64_t g_installfx_last_frame = 0u;
static char g_donut_chars[8192];
static double g_donut_z[8192];

static double terminal_demo_wrap_pi(double x)
{
    const double pi = 3.14159265358979323846;
    const double two_pi = 6.28318530717958647692;
    while (x > pi)
        x -= two_pi;
    while (x < -pi)
        x += two_pi;
    return x;
}

static double terminal_demo_sin(double x)
{
    double x2 = 0.0;
    x = terminal_demo_wrap_pi(x);
    x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 - (x2 * x2 * x2) / 5040.0);
}

static double terminal_demo_cos(double x)
{
    return terminal_demo_sin(x + 1.57079632679489661923);
}

static void terminal_demo_draw_donut(int32_t left, int32_t top, int32_t w, int32_t h,
                                     double angle_a, double angle_b)
{
    static const char luminance[] = ".,-~:;=!*#$@";
    int cells = w * h;
    double sin_a = terminal_demo_sin(angle_a);
    double cos_a = terminal_demo_cos(angle_a);
    double sin_b = terminal_demo_sin(angle_b);
    double cos_b = terminal_demo_cos(angle_b);
    double k1 = 0.0;

    if (w <= 0 || h <= 0 || cells <= 0 || cells > (int)(sizeof(g_donut_chars) / sizeof(g_donut_chars[0])))
        return;

    for (int i = 0; i < cells; ++i)
    {
        g_donut_chars[i] = ' ';
        g_donut_z[i] = 0.0;
    }

    k1 = (double)w * 5.0 * 3.0 / (8.0 * 3.0);
    for (double theta = 0.0; theta < 6.28318530717958647692; theta += 0.18)
    {
        double costheta = terminal_demo_cos(theta);
        double sintheta = terminal_demo_sin(theta);
        for (double phi = 0.0; phi < 6.28318530717958647692; phi += 0.08)
        {
            double cosphi = terminal_demo_cos(phi);
            double sinphi = terminal_demo_sin(phi);
            double circlex = 2.0 + costheta;
            double circley = sintheta;
            double ooz = 1.0 / (sinphi * circlex * sin_a + circley * cos_a + 5.0);
            double xp = cosphi * circlex * cos_b -
                        (sinphi * circlex * cos_a - circley * sin_a) * sin_b;
            double yp = cosphi * circlex * sin_b +
                        (sinphi * circlex * cos_a - circley * sin_a) * cos_b;
            int x = (int)((double)w / 2.0 + k1 * ooz * xp);
            int y = (int)((double)h / 2.0 - (k1 * 0.5) * ooz * yp);
            int idx = x + y * w;
            double lum = cosphi * costheta * sin_b -
                         cos_a * costheta * sinphi -
                         sin_a * sintheta +
                         cos_b * (cos_a * sintheta - costheta * sin_a * sinphi);

            if (x < 0 || y < 0 || x >= w || y >= h || lum <= 0.0 || ooz <= g_donut_z[idx])
                continue;
            {
                int lum_idx = (int)(lum * 8.0);
                if (lum_idx < 0)
                    lum_idx = 0;
                if (lum_idx > 11)
                    lum_idx = 11;
                g_donut_z[idx] = ooz;
                g_donut_chars[idx] = luminance[lum_idx];
            }
        }
    }

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            char ch = g_donut_chars[x + y * w];
            kcolor fg = ch == ' ' ? dim_gray :
                        ch == '@' || ch == '$' || ch == '#' ? white :
                        ch == '!' || ch == '*' || ch == '=' ? cyan :
                                                               light_steel_blue;
            kcolor bg = ch == ' ' ? KCOLOR_RGB(14, 18, 24) : KCOLOR_RGB(17, 24, 31);
            terminal_visual_put(left + x, top + y, ch, fg, bg);
        }
    }
}

static void terminal_demo_installfx_update(void)
{
    uint64_t now = dihos_time_ticks();
    uint64_t elapsed = 0u;
    uint32_t percent = 0u;
    uint32_t frame = 0u;
    int32_t cols = 0;
    int32_t rows = 0;
    int32_t donut_x = 0;
    int32_t donut_y = 0;
    int32_t donut_w = 0;
    int32_t donut_h = 0;
    int32_t bar_w = 0;

    if (!g_installfx_active)
        return;
    if (now == g_installfx_last_frame)
        return;
    g_installfx_last_frame = now;
    elapsed = now - g_installfx_start_tick;

    if (elapsed >= 540u)
    {
        g_installfx_active = 0u;
        terminal_visual_end();
        terminal_success("CoolThing installed");
        return;
    }

    percent = elapsed >= 480u ? 100u : (uint32_t)((elapsed * 100u) / 480u);
    frame = (uint32_t)(elapsed / 8u);
    cols = terminal_visual_cols();
    rows = terminal_visual_rows();
    if (cols <= 0 || rows <= 0)
        return;

    terminal_visual_clear();
    terminal_visual_text((cols - 20) / 2, 0, "Installing CoolThing", green_yellow, KCOLOR_RGB(14, 18, 24));

    donut_w = cols < 78 ? cols : 78;
    donut_h = rows - 5;
    if (donut_h > 22)
        donut_h = 22;
    if (donut_h < 8)
        donut_h = rows > 3 ? rows - 3 : rows;
    donut_x = (cols - donut_w) / 2;
    donut_y = 2;
    terminal_demo_draw_donut(donut_x, donut_y, donut_w, donut_h,
                             (double)frame * 0.08, (double)frame * 0.04);

    terminal_visual_spinner(0u, 1, rows - 2, frame);
    terminal_visual_text(3, rows - 2, percent < 25u ? "resolving packages" :
                                      percent < 55u ? "copying improbable files" :
                                      percent < 85u ? "tuning terminal sparkle" :
                                                      "finalizing",
                         white, KCOLOR_RGB(14, 18, 24));
    bar_w = cols - 4;
    if (bar_w > 64)
        bar_w = 64;
    terminal_visual_progress(0u, (cols - bar_w) / 2, rows - 1, bar_w, percent,
                             TERMINAL_VISUAL_PROGRESS_BLOCKS);
    terminal_visual_present();
}

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
    terminal_demo_installfx_update();

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

extern "C" void terminal_set_visible(uint32_t visible)
{
    g_terminals[0].SetWindowVisible(visible ? 1u : 0u);
}

extern "C" int terminal_visible(void)
{
    return g_terminals[0].Visible();
}

static int terminal_open_script_internal(const char *raw_path, const char *friendly_path,
                                         const char *arg_raw_path, const char *arg_friendly_path,
                                         uint32_t flags)
{
    uint8_t saw_launchable_slot = 0u;

    if (!g_terminal_font || !raw_path || !raw_path[0])
        return -1;

    for (int i = 1; i < MAX_TERMINALS; ++i)
    {
        if (!g_terminals[i].Initialized())
            continue;

        (void)g_terminals[i].CancelProgramIfClosed();
        if (g_terminals[i].ProgramActive())
            continue;

        saw_launchable_slot = 1u;
        if (g_terminals[i].StartProgramWithArg(raw_path, friendly_path, arg_raw_path, arg_friendly_path, flags) == 0)
            return 0;
    }

    for (int i = 1; i < MAX_TERMINALS; ++i)
    {
        if (g_terminals[i].Initialized())
            (void)g_terminals[i].CancelProgramIfClosed();

        if (!g_terminals[i].Initialized())
        {
            g_terminals[i].Initialize(g_terminal_font, "SAC Script", i);
            saw_launchable_slot = 1u;
            if (g_terminals[i].Initialized() &&
                g_terminals[i].StartProgramWithArg(raw_path, friendly_path, arg_raw_path, arg_friendly_path, flags) == 0)
                return 0;
        }
    }

    g_terminals[0].Error(saw_launchable_slot ? "app launch failed" : "no free app terminals");

    return -1;
}

extern "C" int terminal_open_script_ex(const char *raw_path, const char *friendly_path, uint32_t flags)
{
    return terminal_open_script_internal(raw_path, friendly_path, 0, 0, flags);
}

extern "C" int terminal_open_program_ex(const char *raw_path, const char *friendly_path, uint32_t flags)
{
    return terminal_open_script_internal(raw_path, friendly_path, 0, 0, flags);
}

extern "C" int terminal_open_program_with_arg_ex(const char *raw_path, const char *friendly_path,
                                                 const char *arg_raw_path, const char *arg_friendly_path,
                                                 uint32_t flags)
{
    return terminal_open_script_internal(raw_path, friendly_path, arg_raw_path, arg_friendly_path, flags);
}

extern "C" int terminal_open_script(const char *raw_path, const char *friendly_path)
{
    return terminal_open_script_internal(raw_path, friendly_path, 0, 0, TERMINAL_OPEN_FLAG_NONE);
}

extern "C" int terminal_open_program(const char *raw_path, const char *friendly_path)
{
    return terminal_open_script_internal(raw_path, friendly_path, 0, 0, TERMINAL_OPEN_FLAG_NONE);
}

extern "C" int terminal_open_program_with_arg(const char *raw_path, const char *friendly_path,
                                              const char *arg_raw_path, const char *arg_friendly_path)
{
    return terminal_open_script_internal(raw_path, friendly_path, arg_raw_path, arg_friendly_path,
                                         TERMINAL_OPEN_FLAG_NONE);
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

extern "C" void terminal_visual_begin(const char *title)
{
    g_terminals[0].VisualBegin(title);
}

extern "C" void terminal_visual_begin_ex(const char *title, uint32_t flags, uint32_t rows)
{
    g_terminals[0].VisualBeginEx(title, flags, rows);
}

extern "C" void terminal_visual_end(void)
{
    g_terminals[0].VisualEnd();
}

extern "C" void terminal_visual_clear(void)
{
    g_terminals[0].VisualClear();
}

extern "C" void terminal_visual_put(int32_t x, int32_t y, char ch, kcolor fg, kcolor bg)
{
    g_terminals[0].VisualPut(x, y, ch, fg, bg);
}

extern "C" void terminal_visual_text(int32_t x, int32_t y, const char *text, kcolor fg, kcolor bg)
{
    g_terminals[0].VisualText(x, y, text, fg, bg);
}

extern "C" void terminal_visual_progress(uint32_t id, int32_t x, int32_t y, int32_t w,
                                         uint32_t percent, uint32_t style)
{
    g_terminals[0].VisualProgress(id, x, y, w, percent, style);
}

extern "C" void terminal_visual_spinner(uint32_t id, int32_t x, int32_t y, uint32_t frame)
{
    g_terminals[0].VisualSpinner(id, x, y, frame);
}

extern "C" void terminal_visual_present(void)
{
    g_terminals[0].VisualPresent();
}

extern "C" int32_t terminal_visual_cols(void)
{
    return g_terminals[0].VisualCols();
}

extern "C" int32_t terminal_visual_rows(void)
{
    return g_terminals[0].VisualRows();
}

extern "C" int terminal_demo_installfx_start(uint32_t flags)
{
    if (!g_terminals[0].Initialized())
        return -1;

    g_installfx_active = 1u;
    g_installfx_flags = flags;
    g_installfx_start_tick = dihos_time_ticks();
    g_installfx_last_frame = 0u;
    terminal_visual_begin_ex("Installing CoolThing", g_installfx_flags, 24u);
    terminal_demo_installfx_update();
    return 0;
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
