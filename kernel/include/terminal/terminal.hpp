#pragma once

extern "C"
{
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kwindow.h"
}

class Terminal
{
public:
    Terminal();

    void Initialize(kfont *font);
    void Clear();
    void ClearNoFlush();
    void FlushLog();

    void Print(const char *text);
    void PrintInline(const char *text);
    void Warn(const char *text);
    void Error(const char *text);
    void Success(const char *text);
    void SubmitInput(const char *text);
    void UpdateInput();

    void ToggleQuiet();
    void SetQuiet();
    void SetLoud();

private:
    static const int LINE_TEXT_CAP = 512;
    static const int LINE_HISTORY_MAX = 1024;
    static const int MAX_VISIBLE_LINES = 160;
    static const int SCROLL_LINES_PER_WHEEL = 3;

    struct TerminalLine
    {
        uint16_t len;
        kcolor color;
        char text[LINE_TEXT_CAP];
    };

    void ResetState();
    void ScrollToBottom();
    void RefreshVisibleLines();
    int SyncLayoutFromWindow();
    int VisibleLineCapacity() const;
    int HistoryVisibleCapacity() const;
    void LayoutInputBox(int placeholder_y);
    int MaxTopLine() const;
    int IsAtBottom() const;
    int EnsureEditableLine(kcolor color);
    int EnsureVisibleHandle(int slot_idx);
    void DropOldestLine();
    void WriteStyledText(const char *text, kcolor color, int force_new_line);

private:
    int x;
    int y;
    int z;
    int width;
    int height;
    int scale;
    int active;

    int padding_x;
    int padding_y;
    int line_spacing;
    int line_height_px;
    int content_x;
    int content_y;
    int content_height;
    int view_top_line;
    int line_open;

    int line_count;
    int visible_handle_count;
    int last_root_w;
    int last_root_h;
    int prev_mouse_buttons;

    kfont *font_ptr;
    kwindow_style window_style;

    kwindow_handle window;
    ktextbox_handle input_box;

    TerminalLine lines[LINE_HISTORY_MAX];
    kgfx_obj_handle visible_handles[MAX_VISIBLE_LINES];
};
