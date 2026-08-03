#pragma once

#include <stdint.h>

extern "C"
{
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kwindow.h"
#include "shell/dihos_shell.h"
}

class Terminal
{
public:
    Terminal();

    void Initialize(kfont *font, const char *title, int slot_index);
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
    int CopyConsoleText(char *out, uint32_t cap) const;

    void ToggleQuiet();
    void SetQuiet();
    void SetLoud();
    void SetTitle(const char *title);
    void Activate();
    void SetWindowVisible(uint32_t visible);
    int Visible() const;
    int Initialized() const;
    int ProgramActive() const;
    int ScriptActive() const;
    int StartProgram(const char *raw_path, const char *friendly_path, uint32_t launch_flags = 0u);
    int StartProgramWithArg(const char *raw_path, const char *friendly_path,
                            const char *arg_raw_path, const char *arg_friendly_path,
                            uint32_t launch_flags = 0u);
    int StartScript(const char *raw_path, const char *friendly_path, uint32_t launch_flags = 0u);
    void UpdateScript();
    void UpdateSacx();
    int CancelProgramIfClosed();
    void VisualBegin(const char *title);
    void VisualBeginEx(const char *title, uint32_t flags, uint32_t rows);
    void VisualEnd();
    void VisualClear();
    void VisualPut(int32_t grid_x, int32_t grid_y, char ch, kcolor fg, kcolor bg);
    void VisualText(int32_t grid_x, int32_t grid_y, const char *text, kcolor fg, kcolor bg);
    void VisualProgress(uint32_t id, int32_t grid_x, int32_t grid_y, int32_t grid_w,
                        uint32_t percent, uint32_t style);
    void VisualSpinner(uint32_t id, int32_t grid_x, int32_t grid_y, uint32_t frame);
    void VisualPresent();
    int VisualCols() const;
    int VisualRows() const;

private:
    static const int LINE_TEXT_CAP = 512;
    static const int LINE_HISTORY_MAX = 32768;
    static const int MAX_VISIBLE_LINES = 160;
    static const int SCROLL_LINES_PER_WHEEL = 3;
    static const int VISUAL_MAX_COLS = 128;
    static const int VISUAL_MAX_ROWS = 64;
    static const int VISUAL_TITLE_CAP = 96;

    struct TerminalLine
    {
        uint16_t len;
        kcolor color;
        char text[LINE_TEXT_CAP];
    };

    struct VisualCell
    {
        char ch;
        kcolor fg;
        kcolor bg;
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
    void VisualResetState();
    void VisualHideNormalObjects();
    void VisualRestoreNormalObjects();
    int VisualEnsureSurface();
    int VisualEnsureInlineSurface();
    int VisualEnsureFullscreenSurface();
    void VisualRenderToImage();
    void VisualPresentInline();
    void VisualFillRect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, kcolor color);
    uint32_t VisualPackArgb(kcolor color) const;

private:
    int initialized;
    int slot_index;
    int x;
    int y;
    int z;
    int width;
    int height;
    uint32_t scale;
    int active;

    int padding_x;
    int padding_y;
    int line_spacing;
    int line_height_px;
    int content_x;
    int content_y;
    int content_width;
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
    uint8_t visual_active;
    uint8_t visual_dirty;
    uint8_t visual_fullscreen;
    char visual_title[VISUAL_TITLE_CAP];
    int visual_cols;
    int visual_rows;
    int visual_requested_rows;
    int visual_start_line;
    int visual_reserved_rows;
    int visual_cell_w;
    int visual_cell_h;
    uint32_t visual_argb_w;
    uint32_t visual_argb_h;
    uint64_t visual_argb_pages;
    uint32_t *visual_argb;
    kgfx_obj_handle visual_image;
    VisualCell visual_cells[VISUAL_MAX_COLS * VISUAL_MAX_ROWS];
    dihos_shell_session shell;
    dihos_script_runner script;
    uint8_t script_active;
    uint8_t script_done_reported;
    uint8_t sacx_active;
    uint8_t sacx_done_reported;
    uint8_t program_window_expected_visible;
    uint32_t sacx_task_id;
};
