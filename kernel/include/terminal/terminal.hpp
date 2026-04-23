#pragma once

extern "C"
{
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
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
    void UpdateInput();

    void ToggleQuiet();
    void SetQuiet();
    void SetLoud();

private:
    void ResetState();
    void AddLine(const char *text, kcolor color);

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
    int scroll_y;
    int text_base_y;

    int line_count;

    kfont *font_ptr;

    kwindow_handle window;
    kgfx_obj *w;

    static const int TEXT_CAP = 262144;

    kgfx_obj_handle text_handle;
    kgfx_obj *text_ptr;
    char text_storage[TEXT_CAP];
    int text_len;
};
