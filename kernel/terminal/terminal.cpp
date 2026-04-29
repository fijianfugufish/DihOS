#include "terminal/terminal.hpp"
#include "terminal/terminal_api.h"
#include "shell/dihos_shell.h"
#include "apps/sacx_runtime.h"

extern "C"
{
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "kwrappers/colors.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kinput.h"
#include "kwrappers/string.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/kmouse.h"
}

static char g_log_line[16384];
static int g_log_line_len = 0;
static int g_log_line_open = 0;

static char g_log_pending[262144];
static int g_log_pending_len = 0;

static int terminal_quiet = 0;

static char lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (char)(ch - 'A' + 'a');
    return ch;
}

static int has_extension(const char *path, const char *ext)
{
    uint32_t path_len = 0u;
    uint32_t ext_len = 0u;

    if (!path || !ext)
        return 0;

    path_len = (uint32_t)strlen(path);
    ext_len = (uint32_t)strlen(ext);
    if (path_len < ext_len || ext_len == 0u)
        return 0;

    path += path_len - ext_len;
    for (uint32_t i = 0u; i < ext_len; ++i)
        if (lower_ascii(path[i]) != lower_ascii(ext[i]))
            return 0;
    return 1;
}

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

static void log_line_append_char(char ch)
{
    if (g_log_line_len >= (int)sizeof(g_log_line) - 1)
        return;

    g_log_line[g_log_line_len++] = ch;
    g_log_line[g_log_line_len] = 0;
}

static void log_line_append_cstr(const char *text)
{
    if (!text)
        return;

    while (*text)
        log_line_append_char(*text++);
}

static void log_pending_append_raw(const char *text, int len)
{
    int i = 0;

    while (text && i < len && g_log_pending_len < (int)sizeof(g_log_pending) - 1)
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

static void terminal_log_write_text(const char *prefix, const char *text, int force_new_line)
{
    if (force_new_line)
        log_commit_open_line();

    if (!text)
        return;

    while (*text)
    {
        if (*text == '\n')
        {
            log_commit_open_line();
            ++text;
            continue;
        }

        if (!g_log_line_open)
        {
            if (prefix)
                log_line_append_cstr(prefix);
            g_log_line_open = 1;
        }

        log_line_append_char(*text++);
    }
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

static void terminal_input_submit(ktextbox_handle textbox, const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    (void)textbox;

    if (!terminal)
        return;

    terminal->SubmitInput(text);
}

static void terminal_session_print(const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->Print(text);
}

static void terminal_session_console_visible(uint8_t visible, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->SetWindowVisible(visible ? 1u : 0u);
}

static void terminal_session_print_inline(const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->PrintInline(text);
}

static void terminal_session_warn(const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->Warn(text);
}

static void terminal_session_error(const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->Error(text);
}

static void terminal_session_success(const char *text, void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->Success(text);
}

static void terminal_session_clear(void *user)
{
    Terminal *terminal = (Terminal *)user;
    if (terminal)
        terminal->ClearNoFlush();
}

static void terminal_i32_to_dec(int value, char *out, uint32_t cap)
{
    char tmp[16];
    uint32_t len = 0u;
    uint32_t i = 0u;
    uint32_t magnitude = 0u;

    if (!out || cap == 0u)
        return;

    if (value < 0)
    {
        out[0] = '-';
        if (cap <= 1u)
            return;
        out++;
        cap--;
        magnitude = (uint32_t)(-value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    if (magnitude == 0u)
    {
        if (cap > 1u)
        {
            out[0] = '0';
            out[1] = 0;
        }
        else
        {
            out[0] = 0;
        }
        return;
    }

    while (magnitude && len < sizeof(tmp))
    {
        tmp[len++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }

    while (len > 0u && i + 1u < cap)
        out[i++] = tmp[--len];
    out[i] = 0;
}

static kcolor rgb(uint8_t r, uint8_t g, uint8_t b)
{
    kcolor c = {r, g, b};
    return c;
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
    initialized = 0;
    slot_index = 0;
    x = 1000;
    y = 10;
    z = 20;
    width = 600;
    height = 300;
    scale = 1;
    active = 1;

    padding_x = 8;
    padding_y = 8;
    line_spacing = padding_y / 2;
    line_height_px = 1;
    content_x = padding_x;
    content_y = padding_y;
    content_height = 1;
    view_top_line = 0;
    line_open = 0;
    line_count = 0;
    visible_handle_count = 0;
    last_root_w = 0;
    last_root_h = 0;
    prev_mouse_buttons = 0;
    font_ptr = 0;
    window_style = kwindow_style_default();

    window_style.body_fill = rgb(14, 18, 24);
    window_style.body_outline = rgb(98, 122, 166);
    window_style.titlebar_fill = rgb(26, 35, 48);
    window_style.title_color = rgb(239, 243, 248);
    window_style.close_button_style.fill = rgb(128, 46, 46);
    window_style.close_button_style.hover_fill = rgb(154, 58, 58);
    window_style.close_button_style.pressed_fill = rgb(99, 35, 35);
    window_style.close_button_style.outline = rgb(255, 225, 225);
    window_style.fullscreen_button_style.fill = rgb(214, 168, 64);
    window_style.fullscreen_button_style.hover_fill = rgb(232, 186, 82);
    window_style.fullscreen_button_style.pressed_fill = rgb(182, 138, 50);
    window_style.fullscreen_button_style.outline = rgb(254, 246, 222);
    window_style.title_scale = 2u;

    window.idx = -1;
    input_box.idx = -1;
    script_active = 0u;
    script_done_reported = 0u;
    sacx_active = 0u;
    sacx_done_reported = 0u;
    sacx_task_id = 0u;
    memset(&shell, 0, sizeof(shell));
    memset(&script, 0, sizeof(script));

    for (int i = 0; i < LINE_HISTORY_MAX; ++i)
    {
        lines[i].len = 0;
        lines[i].color = white;
        lines[i].text[0] = 0;
    }

    for (int i = 0; i < MAX_VISIBLE_LINES; ++i)
        visible_handles[i].idx = -1;

    log_line_clear();
    log_pending_clear();
}

int Terminal::VisibleLineCapacity() const
{
    int capacity = 1;

    if (line_height_px > 0)
        capacity = (content_height + line_height_px - 1) / line_height_px;

    if (capacity < 1)
        capacity = 1;
    if (capacity > MAX_VISIBLE_LINES)
        capacity = MAX_VISIBLE_LINES;
    return capacity;
}

int Terminal::HistoryVisibleCapacity() const
{
    int capacity = VisibleLineCapacity() - 1;

    if (capacity < 0)
        capacity = 0;
    return capacity;
}

int Terminal::MaxTopLine() const
{
    int capacity = HistoryVisibleCapacity();

    if (line_count <= capacity)
        return 0;
    return line_count - capacity;
}

int Terminal::IsAtBottom() const
{
    return view_top_line >= MaxTopLine();
}

void Terminal::ScrollToBottom()
{
    view_top_line = MaxTopLine();
}

void Terminal::LayoutInputBox(int placeholder_y)
{
    kgfx_obj *root = 0;
    const char *prompt = 0;
    uint32_t prompt_px = 0u;
    uint32_t input_w = 1u;
    uint32_t input_h = 1u;
    int32_t input_x = content_x;

    if (input_box.idx < 0 || !font_ptr)
        return;

    root = kgfx_obj_ref(kwindow_root(window));
    if (!root || root->kind != KGFX_OBJ_RECT)
        return;

    if (sacx_active)
    {
        prompt = "sacx> ";
    }
    else if (script_active)
    {
        if (dihos_script_waiting_input(&script))
            prompt = dihos_script_input_prompt(&script);
        else
            prompt = "";
    }
    else
    {
        prompt = dihos_shell_session_prompt(&shell);
    }
    if (prompt && prompt[0])
        prompt_px = ktext_measure_line_px(font_ptr, prompt, (uint32_t)scale, 1);

    if (root->u.rect.w > (uint32_t)(padding_x * 2) + prompt_px)
        input_w = root->u.rect.w - (uint32_t)(padding_x * 2) - prompt_px;
    input_h = (line_height_px > 0) ? (uint32_t)line_height_px : 1u;
    input_x += (int32_t)prompt_px;

    ktextbox_set_font(input_box, font_ptr);
    ktextbox_set_bounds(input_box, input_x, placeholder_y, input_w, input_h);
}

int Terminal::SyncLayoutFromWindow()
{
    kgfx_obj *root = 0;
    int changed = 0;
    int old_history_capacity = HistoryVisibleCapacity();
    int old_visible_count = line_count - view_top_line;
    int old_bottom_line = -1;
    int new_root_w = 0;
    int new_root_h = 0;
    int new_line_height = 1;
    int new_content_x = padding_x;
    int new_content_y = padding_y;
    int new_content_height = 1;

    root = kgfx_obj_ref(kwindow_root(window));
    if (!root || root->kind != KGFX_OBJ_RECT)
        return 0;

    if (old_visible_count < 0)
        old_visible_count = 0;
    if (old_visible_count > old_history_capacity)
        old_visible_count = old_history_capacity;
    if (old_visible_count > 0)
        old_bottom_line = view_top_line + old_visible_count - 1;

    new_root_w = (int)root->u.rect.w;
    new_root_h = (int)root->u.rect.h;
    new_line_height = font_ptr ? (int)ktext_line_height(font_ptr, (uint32_t)scale, line_spacing) : 1;
    new_content_y = (int)window_style.titlebar_height + padding_y;
    new_content_height = new_root_h - (int)window_style.titlebar_height - (padding_y * 2);

    if (new_line_height < 1)
        new_line_height = 1;
    if (new_content_height < 1)
        new_content_height = 1;

    if (last_root_w != new_root_w || last_root_h != new_root_h ||
        line_height_px != new_line_height ||
        content_x != new_content_x || content_y != new_content_y ||
        content_height != new_content_height)
    {
        changed = 1;
    }

    last_root_w = new_root_w;
    last_root_h = new_root_h;
    line_height_px = new_line_height;
    content_x = new_content_x;
    content_y = new_content_y;
    content_height = new_content_height;

    if (changed)
    {
        if (old_bottom_line >= 0)
        {
            int history_capacity = HistoryVisibleCapacity();
            int new_top = 0;

            if (history_capacity > 0)
                new_top = old_bottom_line - history_capacity + 1;
            else
                new_top = line_count;

            if (new_top < 0)
                new_top = 0;
            if (new_top > MaxTopLine())
                new_top = MaxTopLine();
            view_top_line = new_top;
        }
        else if (view_top_line > MaxTopLine())
        {
            view_top_line = MaxTopLine();
        }
    }
    else if (view_top_line > MaxTopLine())
    {
        view_top_line = MaxTopLine();
    }

    return changed;
}

void Terminal::DropOldestLine()
{
    if (line_count <= 0)
        return;

    for (int i = 1; i < line_count; ++i)
        lines[i - 1] = lines[i];

    --line_count;
    lines[line_count].len = 0;
    lines[line_count].color = white;
    lines[line_count].text[0] = 0;

    if (view_top_line > 0)
        --view_top_line;

    if (line_count <= 0)
        line_open = 0;
}

int Terminal::EnsureEditableLine(kcolor color)
{
    TerminalLine *line = 0;

    if (line_open && line_count > 0)
        return line_count - 1;

    if (line_count >= LINE_HISTORY_MAX)
        DropOldestLine();

    if (line_count >= LINE_HISTORY_MAX)
        return -1;

    line = &lines[line_count++];
    line->len = 0;
    line->color = color;
    line->text[0] = 0;
    line_open = 1;
    return line_count - 1;
}

int Terminal::EnsureVisibleHandle(int slot_idx)
{
    while (visible_handle_count <= slot_idx && visible_handle_count < MAX_VISIBLE_LINES)
    {
        kgfx_obj_handle handle;
        kgfx_obj *obj = 0;

        handle = kgfx_obj_add_text(font_ptr, "",
                                   content_x, content_y, 0,
                                   white, 255,
                                   (uint32_t)scale,
                                   1, 0,
                                   KTEXT_ALIGN_LEFT,
                                   active ? 1u : 0u);
        if (handle.idx < 0)
            return 0;

        kgfx_obj_set_parent(handle, kwindow_root(window));
        obj = kgfx_obj_ref(handle);
        if (obj && obj->kind == KGFX_OBJ_TEXT)
        {
            obj->outline_width = 0;
            obj->clip_to_parent = 1;
        }

        visible_handles[visible_handle_count++] = handle;
    }

    return visible_handle_count > slot_idx;
}

void Terminal::RefreshVisibleLines()
{
    const char *prompt = 0;
    int total_slots = 0;
    int history_capacity = 0;
    int visible_count = 0;
    int placeholder_slot = 0;
    int first_history_slot = 0;
    int base_y = 0;
    int placeholder_y = 0;

    if (!font_ptr)
        return;

    (void)SyncLayoutFromWindow();
    if (sacx_active)
    {
        prompt = "sacx> ";
    }
    else if (script_active)
    {
        if (dihos_script_waiting_input(&script))
            prompt = dihos_script_input_prompt(&script);
        else
            prompt = "";
    }
    else
    {
        prompt = dihos_shell_session_prompt(&shell);
    }

    if (input_box.idx >= 0)
    {
        uint8_t allow_input = ((!script_active || dihos_script_waiting_input(&script)) && !sacx_active) ? 1u : 0u;
        ktextbox_set_enabled(input_box, allow_input);
        if (!allow_input)
        {
            ktextbox_clear(input_box);
            if (ktextbox_focused(input_box))
                ktextbox_set_focus(input_box, 0u);
        }
    }

    if (view_top_line < 0)
        view_top_line = 0;
    if (view_top_line > MaxTopLine())
        view_top_line = MaxTopLine();

    total_slots = VisibleLineCapacity();
    history_capacity = HistoryVisibleCapacity();
    placeholder_slot = total_slots - 1;
    base_y = content_y + content_height - (total_slots * line_height_px);
    placeholder_y = base_y + (placeholder_slot * line_height_px);
    visible_count = line_count - view_top_line;
    if (visible_count < 0)
        visible_count = 0;
    if (visible_count > history_capacity)
        visible_count = history_capacity;
    first_history_slot = placeholder_slot - visible_count;

    if (placeholder_slot >= 0)
        (void)EnsureVisibleHandle(placeholder_slot);

    for (int i = 0; i < visible_handle_count; ++i)
    {
        kgfx_obj *obj = kgfx_obj_ref(visible_handles[i]);
        if (obj)
            obj->visible = 0;
    }

    for (int i = 0; i < visible_count; ++i)
    {
        kgfx_obj *obj = 0;
        TerminalLine *line = 0;
        int slot_idx = first_history_slot + i;

        if (!EnsureVisibleHandle(slot_idx))
            break;

        obj = kgfx_obj_ref(visible_handles[slot_idx]);
        if (!obj || obj->kind != KGFX_OBJ_TEXT)
            break;

        line = &lines[view_top_line + i];
        obj->u.text.font = font_ptr;
        obj->u.text.text = line->text;
        obj->u.text.x = content_x;
        obj->u.text.y = base_y + (slot_idx * line_height_px);
        obj->u.text.scale = (uint32_t)scale;
        obj->u.text.char_spacing = 1;
        obj->u.text.line_spacing = 0;
        obj->u.text.align = KTEXT_ALIGN_LEFT;
        obj->fill = line->color;
        obj->alpha = 255;
        obj->visible = active ? 1u : 0u;
        obj->clip_to_parent = 1;
    }

    if (placeholder_slot >= 0 && EnsureVisibleHandle(placeholder_slot))
    {
        kgfx_obj *obj = kgfx_obj_ref(visible_handles[placeholder_slot]);
        if (obj && obj->kind == KGFX_OBJ_TEXT)
        {
            obj->u.text.font = font_ptr;
            obj->u.text.text = prompt ? prompt : "";
            obj->u.text.x = content_x;
            obj->u.text.y = base_y + (placeholder_slot * line_height_px);
            obj->u.text.scale = (uint32_t)scale;
            obj->u.text.char_spacing = 1;
            obj->u.text.line_spacing = 0;
            obj->u.text.align = KTEXT_ALIGN_LEFT;
            obj->fill = yellow_green;
            obj->alpha = 255;
            obj->visible = active ? 1u : 0u;
            obj->clip_to_parent = 1;
        }
    }

    LayoutInputBox(placeholder_y);
}

void Terminal::Initialize(kfont *font, const char *title, int new_slot_index)
{
    dihos_shell_io io;

    ResetState();
    slot_index = new_slot_index;
    x = 1000 - (new_slot_index * 34);
    y = 10 + (new_slot_index * 34);
    if (x < 40)
        x = 40 + (new_slot_index * 12);
    if (y > 180)
        y = 30 + (new_slot_index * 18);

    memset(&io, 0, sizeof(io));
    io.print = terminal_session_print;
    io.print_inline = terminal_session_print_inline;
    io.warn = terminal_session_warn;
    io.error = terminal_session_error;
    io.success = terminal_session_success;
    io.clear = terminal_session_clear;
    io.user = this;
    dihos_shell_session_init(&shell, &io);

    font_ptr = font;
    window = kwindow_create(x, y, width, height, z + new_slot_index, font_ptr, title ? title : "Terminal", &window_style);
    if (window.idx < 0)
        return;

    initialized = 1;

    if (font_ptr)
    {
        ktextbox_style input_style = ktextbox_style_default();
        input_style.fill = window_style.body_fill;
        input_style.hover_fill = window_style.body_fill;
        input_style.focus_fill = window_style.body_fill;
        input_style.outline = window_style.body_fill;
        input_style.focus_outline = window_style.body_fill;
        input_style.outline_width = 0;
        input_style.padding_x = 0;
        input_style.padding_y = 0;
        input_style.text_scale = (uint32_t)scale;
        input_box = ktextbox_add_rect(0, 0, 10, 10, 2, font_ptr, &input_style,
                                      terminal_input_submit, this);
        if (input_box.idx >= 0)
        {
            kgfx_obj_set_parent(ktextbox_root(input_box), kwindow_root(window));
            ktextbox_set_focus(input_box, 1);
        }
    }

    (void)SyncLayoutFromWindow();
    RefreshVisibleLines();

    KFile f;
    if (slot_index == 0 &&
        kfile_open(&f, "0:/OS/System/Logs/terminal.txt",
                   KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) == 0)
    {
        kfile_close(&f);
    }

    log_line_clear();
    log_pending_clear();
}

void Terminal::Clear()
{
    terminal_log_flush_pending();
    line_count = 0;
    line_open = 0;
    view_top_line = 0;
    if (input_box.idx >= 0)
        ktextbox_clear(input_box);
    RefreshVisibleLines();
}

void Terminal::ClearNoFlush()
{
    line_count = 0;
    line_open = 0;
    view_top_line = 0;
    if (input_box.idx >= 0)
        ktextbox_clear(input_box);
    RefreshVisibleLines();
}

void Terminal::SubmitInput(const char *text)
{
    const char *prompt = dihos_shell_session_prompt(&shell);
    const char *script_prompt = 0;
    char echoed[DIHOS_SHELL_PROMPT_CAP + DIHOS_SHELL_LINE_CAP];
    char submitted[DIHOS_SHELL_LINE_CAP];

    echoed[0] = 0;
    submitted[0] = 0;
    if (text)
        strncpy(submitted, text, sizeof(submitted) - 1u);
    submitted[sizeof(submitted) - 1u] = 0;

    if (input_box.idx >= 0)
        ktextbox_clear(input_box);

    if (script_active && dihos_script_waiting_input(&script))
    {
        ksb builder;
        script_prompt = dihos_script_input_prompt(&script);
        ksb_init(&builder, echoed, sizeof(echoed));
        if (script_prompt && script_prompt[0])
            ksb_puts(&builder, script_prompt);
        ksb_puts(&builder, submitted);
        Print(echoed);

        (void)dihos_script_submit_input(&script, submitted);
        RefreshVisibleLines();
        return;
    }

    if (sacx_active)
    {
        Print("sacx app is running; shell input is disabled in this terminal");
        RefreshVisibleLines();
        return;
    }

    if (!submitted[0])
    {
        RefreshVisibleLines();
        return;
    }

    {
        ksb builder;
        ksb_init(&builder, echoed, sizeof(echoed));
        if (prompt && prompt[0])
            ksb_puts(&builder, prompt);
        ksb_puts(&builder, submitted);
    }

    Print(echoed);

    (void)dihos_shell_session_execute_line(&shell, submitted);
    RefreshVisibleLines();
}

void Terminal::WriteStyledText(const char *text, kcolor color, int force_new_line)
{
    int was_at_bottom = 0;

    if (!font_ptr || !text)
        return;

    (void)SyncLayoutFromWindow();
    was_at_bottom = IsAtBottom();

    if (force_new_line)
        line_open = 0;

    while (*text)
    {
        int idx = 0;
        TerminalLine *line = 0;

        if (*text == '\n')
        {
            line_open = 0;
            ++text;
            continue;
        }

        idx = EnsureEditableLine(color);
        if (idx < 0)
            break;

        line = &lines[idx];
        while (*text && *text != '\n')
        {
            if (line->len < LINE_TEXT_CAP - 1)
            {
                line->text[line->len++] = *text;
                line->text[line->len] = 0;
            }
            ++text;
        }

        if (*text == '\n')
        {
            line_open = 0;
            ++text;
        }
    }

    if (was_at_bottom)
        ScrollToBottom();

    RefreshVisibleLines();
}

void Terminal::PrintInline(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_write_text("", text, 0);
    WriteStyledText(text, white, 0);
}

void Terminal::Print(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_write_text("", text, 1);
    WriteStyledText(text, white, 1);
}

void Terminal::Warn(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_write_text("[WARN] ", text, 1);
    WriteStyledText(text, orange, 1);
}

void Terminal::Error(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_write_text("[ERROR] ", text, 1);
    WriteStyledText(text, red, 1);
}

void Terminal::Success(const char *text)
{
    if (terminal_quiet)
        return;

    terminal_log_write_text("[SUCCESS] ", text, 1);
    WriteStyledText(text, green, 1);
}

void Terminal::ToggleQuiet()
{
    terminal_quiet = terminal_quiet ? 0 : 1;
}

void Terminal::SetQuiet() { terminal_quiet = 1; }

void Terminal::SetLoud() { terminal_quiet = 0; }

void Terminal::Activate()
{
    if (window.idx < 0)
        return;

    kwindow_set_visible(window, 1);
    (void)kwindow_raise(window);

    if (input_box.idx >= 0)
        ktextbox_set_focus(input_box, 1);

    if (SyncLayoutFromWindow())
        RefreshVisibleLines();
}

void Terminal::SetWindowVisible(uint32_t visible)
{
    if (window.idx < 0)
        return;

    kwindow_set_visible(window, visible ? 1u : 0u);
    if (input_box.idx >= 0)
        ktextbox_set_focus(input_box, visible ? 1u : 0u);
}

int Terminal::Visible() const
{
    if (window.idx < 0)
        return 0;
    return kwindow_visible(window);
}

int Terminal::Initialized() const
{
    return initialized;
}

int Terminal::ProgramActive() const
{
    return (script_active || sacx_active) ? 1 : 0;
}

int Terminal::ScriptActive() const
{
    return script_active ? 1 : 0;
}

int Terminal::StartProgram(const char *raw_path, const char *friendly_path, uint32_t launch_flags)
{
    return StartProgramWithArg(raw_path, friendly_path, 0, 0, launch_flags);
}

int Terminal::StartProgramWithArg(const char *raw_path, const char *friendly_path,
                                  const char *arg_raw_path, const char *arg_friendly_path,
                                  uint32_t launch_flags)
{
    uint32_t task_id = 0u;
    sacx_runtime_io io = {0};
    char title[96];
    ksb builder;
    uint8_t no_window = (launch_flags & TERMINAL_OPEN_FLAG_NO_WINDOW) ? 1u : 0u;

    if (!raw_path || !raw_path[0])
        return -1;

    if (has_extension(raw_path, ".sac"))
        return StartScript(raw_path, friendly_path, launch_flags);

    if (!has_extension(raw_path, ".sacx"))
        return -1;

    if (!initialized || window.idx < 0)
        return -1;

    ksb_init(&builder, title, sizeof(title));
    ksb_puts(&builder, "SACX App");
    if (friendly_path && friendly_path[0])
    {
        ksb_puts(&builder, ": ");
        ksb_puts(&builder, friendly_path);
    }
    kwindow_set_title(window, title);

    ClearNoFlush();
    PrintInline("launching ");
    Print(friendly_path && friendly_path[0] ? friendly_path : raw_path);

    io.print = terminal_session_print;
    io.set_console_visible = terminal_session_console_visible;
    io.user = this;
    if (sacx_runtime_launch_ex(raw_path, friendly_path, arg_raw_path, arg_friendly_path, &io, &task_id) != 0)
    {
        Error("unable to launch sacx program");
        sacx_active = 0u;
        sacx_done_reported = 1u;
        sacx_task_id = 0u;
        if (no_window)
            SetWindowVisible(0u);
        else
            Activate();
        return -1;
    }

    script_active = 0u;
    script_done_reported = 1u;
    sacx_active = 1u;
    sacx_done_reported = 0u;
    sacx_task_id = task_id;
    if (no_window)
        SetWindowVisible(0u);
    else
        Activate();
    return 0;
}

int Terminal::StartScript(const char *raw_path, const char *friendly_path, uint32_t launch_flags)
{
    char title[96];
    ksb builder;
    uint8_t no_window = (launch_flags & TERMINAL_OPEN_FLAG_NO_WINDOW) ? 1u : 0u;

    if (!initialized || window.idx < 0 || !raw_path || !raw_path[0])
        return -1;

    ksb_init(&builder, title, sizeof(title));
    ksb_puts(&builder, "SAC Script");
    if (friendly_path && friendly_path[0])
    {
        ksb_puts(&builder, ": ");
        ksb_puts(&builder, friendly_path);
    }
    kwindow_set_title(window, title);

    ClearNoFlush();
    PrintInline("running ");
    Print(friendly_path && friendly_path[0] ? friendly_path : raw_path);

    if (dihos_script_load_file(&script, &shell, raw_path, friendly_path) != 0)
    {
        script_active = 0u;
        script_done_reported = 1u;
        sacx_active = 0u;
        sacx_done_reported = 1u;
        sacx_task_id = 0u;
        if (no_window)
            SetWindowVisible(0u);
        else
            Activate();
        return 0;
    }

    script_active = 1u;
    script_done_reported = 0u;
    sacx_active = 0u;
    sacx_done_reported = 1u;
    sacx_task_id = 0u;
    if (no_window)
        SetWindowVisible(0u);
    else
        Activate();
    return 0;
}

void Terminal::UpdateScript()
{
    char status[16];
    int was_waiting = 0;

    if (!script_active)
        return;

    was_waiting = dihos_script_waiting_input(&script);
    (void)dihos_script_step(&script, DIHOS_SCRIPT_STEP_BUDGET);
    if (!was_waiting && dihos_script_waiting_input(&script))
        RefreshVisibleLines();
    if (!dihos_script_finished(&script))
        return;

    script_active = 0u;
    if (script_done_reported)
        return;

    terminal_i32_to_dec(dihos_script_exit_status(&script), status, sizeof(status));
    if (dihos_script_exit_status(&script) == 0)
    {
        Print("script exited status ");
        Success(status);
    }
    else
    {
        Print("script exited status ");
        Error(status);
    }
    script_done_reported = 1u;
    RefreshVisibleLines();
}

void Terminal::UpdateSacx()
{
    sacx_task_status st;
    char status_text[16];

    if (!sacx_active || !sacx_task_id)
        return;

    if (sacx_runtime_task_status(sacx_task_id, &st) != 0)
    {
        sacx_active = 0u;
        sacx_done_reported = 1u;
        sacx_task_id = 0u;
        return;
    }

    if (st.state != SACX_TASK_EXITED && st.state != SACX_TASK_FAULTED)
        return;

    sacx_active = 0u;
    if (sacx_done_reported)
        return;

    terminal_i32_to_dec(st.exit_status, status_text, sizeof(status_text));
    if (st.state == SACX_TASK_FAULTED)
    {
        Print("sacx task faulted status ");
        Error(status_text);
    }
    else if (st.exit_status == 0)
    {
        Print("sacx task exited status ");
        Success(status_text);
    }
    else
    {
        Print("sacx task exited status ");
        Error(status_text);
    }

    if (st.message[0])
        Print(st.message);

    (void)sacx_runtime_task_release(sacx_task_id);
    sacx_done_reported = 1u;
    sacx_task_id = 0u;
    RefreshVisibleLines();
}

void Terminal::UpdateInput()
{
    kmouse_state mouse = {0};
    kgfx_obj *root = 0;
    int layout_changed = 0;
    int left_now = 0;
    int left_pressed = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int client_bottom = 0;
    int old_top = 0;
    int accepts_pointer = 0;

    if (!font_ptr)
        return;

    layout_changed = SyncLayoutFromWindow();
    if (layout_changed)
        RefreshVisibleLines();

    kmouse_get_state(&mouse);
    left_now = (mouse.buttons & 0x01u) ? 1 : 0;
    left_pressed = left_now && !(prev_mouse_buttons & 0x01u);

    root = kgfx_obj_ref(kwindow_root(window));
    if (!root || root->kind != KGFX_OBJ_RECT || !root->visible)
    {
        if (input_box.idx >= 0 && ktextbox_focused(input_box))
            ktextbox_set_focus(input_box, 0);
        prev_mouse_buttons = mouse.buttons;
        return;
    }

    left = root->u.rect.x + content_x;
    top = root->u.rect.y + content_y;
    right = root->u.rect.x + (int32_t)root->u.rect.w - padding_x;
    client_bottom = root->u.rect.y + (int32_t)root->u.rect.h - padding_y;
    accepts_pointer = kwindow_point_can_receive_input(window, mouse.x, mouse.y);

    if (left_pressed && accepts_pointer && input_box.idx >= 0 &&
        mouse.x >= left && mouse.y >= top &&
        mouse.x < right && mouse.y < client_bottom)
    {
        ktextbox_set_focus(input_box, 1);
    }

    if (input_box.idx >= 0 && ktextbox_focused(input_box) &&
        (!script_active || !dihos_script_waiting_input(&script)) && !sacx_active)
    {
        char recalled[DIHOS_SHELL_LINE_CAP];

        if (kinput_key_pressed(KEY_UP) &&
            dihos_shell_session_history_prev(&shell, ktextbox_text(input_box), recalled, sizeof(recalled)))
        {
            ktextbox_set_text(input_box, recalled);
        }
        else if (kinput_key_pressed(KEY_DOWN) &&
                 dihos_shell_session_history_next(&shell, ktextbox_text(input_box), recalled, sizeof(recalled)))
        {
            ktextbox_set_text(input_box, recalled);
        }
    }

    if (mouse.wheel == 0)
    {
        prev_mouse_buttons = mouse.buttons;
        return;
    }

    bottom = client_bottom - line_height_px;

    if (right <= left || bottom <= top)
    {
        prev_mouse_buttons = mouse.buttons;
        return;
    }

    if (!accepts_pointer || mouse.x < left || mouse.y < top || mouse.x >= right || mouse.y >= bottom)
    {
        prev_mouse_buttons = mouse.buttons;
        return;
    }

    old_top = view_top_line;
    view_top_line -= mouse.wheel * SCROLL_LINES_PER_WHEEL;
    if (view_top_line < 0)
        view_top_line = 0;
    if (view_top_line > MaxTopLine())
        view_top_line = MaxTopLine();

    if (view_top_line != old_top)
        RefreshVisibleLines();

    prev_mouse_buttons = mouse.buttons;
}
