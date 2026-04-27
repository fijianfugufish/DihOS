#include "terminal/terminal.hpp"
#include "shell/dihos_shell.h"

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

    window.idx = -1;
    input_box.idx = -1;

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

    prompt = dihos_shell_prompt();
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
    prompt = dihos_shell_prompt();

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

void Terminal::Initialize(kfont *font)
{
    ResetState();
    dihos_shell_init();
    font_ptr = font;
    window_style = kwindow_style_default();
    window = kwindow_create(x, y, width, height, z, font_ptr, "Terminal", &window_style);

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
    if (kfile_open(&f, "0:/OS/System/Logs/terminal.txt",
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
    const char *prompt = dihos_shell_prompt();
    char echoed[DIHOS_SHELL_PROMPT_CAP + DIHOS_SHELL_LINE_CAP];
    char submitted[DIHOS_SHELL_LINE_CAP];

    echoed[0] = 0;
    submitted[0] = 0;
    if (text)
        strncpy(submitted, text, sizeof(submitted) - 1u);
    submitted[sizeof(submitted) - 1u] = 0;

    if (input_box.idx >= 0)
        ktextbox_clear(input_box);

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

    (void)dihos_shell_execute_line(submitted);
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

int Terminal::Visible() const
{
    if (window.idx < 0)
        return 0;
    return kwindow_visible(window);
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

    if (left_pressed && input_box.idx >= 0 &&
        mouse.x >= left && mouse.y >= top &&
        mouse.x < right && mouse.y < client_bottom)
    {
        ktextbox_set_focus(input_box, 1);
    }

    if (input_box.idx >= 0 && ktextbox_focused(input_box))
    {
        char recalled[DIHOS_SHELL_LINE_CAP];

        if (kinput_key_pressed(KEY_UP) &&
            dihos_shell_history_prev(ktextbox_text(input_box), recalled, sizeof(recalled)))
        {
            ktextbox_set_text(input_box, recalled);
        }
        else if (kinput_key_pressed(KEY_DOWN) &&
                 dihos_shell_history_next(ktextbox_text(input_box), recalled, sizeof(recalled)))
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

    if (mouse.x < left || mouse.y < top || mouse.x >= right || mouse.y >= bottom)
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
