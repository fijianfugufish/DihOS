#include "apps/text_editor_api.h"
#include "apps/file_explorer_api.h"

extern "C"
{
#include "filesystem/dihos_path.h"
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/ktext.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/string.h"
}

namespace
{
    static const uint8_t kPrintableUsages[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
        KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
        KEY_SPACE, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH,
        KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_DOT, KEY_SLASH,
        KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
        KEY_KP_DOT, KEY_KP_PLUS, KEY_KP_MINUS, KEY_KP_ASTERISK, KEY_KP_SLASH};

    static kcolor rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        kcolor c = {r, g, b};
        return c;
    }

    static void copy_text(char *dst, uint32_t cap, const char *src)
    {
        uint32_t i = 0u;

        if (!dst || cap == 0u)
            return;

        if (!src)
            src = "";

        while (src[i] && i + 1u < cap)
        {
            dst[i] = src[i];
            ++i;
        }

        dst[i] = 0;
    }

    static void append_text(char *dst, uint32_t cap, const char *src)
    {
        uint32_t len = 0u;

        if (!dst || cap == 0u)
            return;

        while (dst[len] && len + 1u < cap)
            ++len;

        if (!src)
            return;

        while (*src && len + 1u < cap)
            dst[len++] = *src++;

        dst[len] = 0;
    }

    static int strings_equal(const char *a, const char *b)
    {
        if (!a)
            a = "";
        if (!b)
            b = "";
        return strcmp(a, b) == 0;
    }

    static uint32_t text_height_px(const kfont *font, uint32_t scale)
    {
        if (!font)
            return 0u;
        return ktext_scale_mul_px(font->h, scale);
    }

    static char usage_to_char(uint8_t usage, uint8_t shift, uint8_t caps_lock)
    {
        if (usage >= KEY_A && usage <= KEY_Z)
        {
            char ch = (char)('a' + (usage - KEY_A));
            if ((shift ? 1u : 0u) ^ (caps_lock ? 1u : 0u))
                ch = (char)(ch - ('a' - 'A'));
            return ch;
        }

        switch (usage)
        {
        case KEY_1: return shift ? '!' : '1';
        case KEY_2: return shift ? '@' : '2';
        case KEY_3: return shift ? '#' : '3';
        case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5';
        case KEY_6: return shift ? '^' : '6';
        case KEY_7: return shift ? '&' : '7';
        case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9';
        case KEY_0: return shift ? ')' : '0';
        case KEY_SPACE: return ' ';
        case KEY_MINUS: return shift ? '_' : '-';
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return shift ? '{' : '[';
        case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_BACKSLASH: return shift ? '|' : '\\';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_KP_0: return '0';
        case KEY_KP_1: return '1';
        case KEY_KP_2: return '2';
        case KEY_KP_3: return '3';
        case KEY_KP_4: return '4';
        case KEY_KP_5: return '5';
        case KEY_KP_6: return '6';
        case KEY_KP_7: return '7';
        case KEY_KP_8: return '8';
        case KEY_KP_9: return '9';
        case KEY_KP_DOT: return '.';
        case KEY_KP_PLUS: return '+';
        case KEY_KP_MINUS: return '-';
        case KEY_KP_ASTERISK: return '*';
        case KEY_KP_SLASH: return '/';
        default:
            return 0;
        }
    }

    static int ascii_is_space(char ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }

    static int ascii_is_digit(char ch)
    {
        return ch >= '0' && ch <= '9';
    }

    static int ascii_is_name_start(char ch)
    {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
    }

    static int ascii_is_name_char(char ch)
    {
        return ascii_is_name_start(ch) || ascii_is_digit(ch);
    }

    static char ascii_lower(char ch)
    {
        if (ch >= 'A' && ch <= 'Z')
            return (char)(ch - 'A' + 'a');
        return ch;
    }

    static int token_equals_ci(const char *token, uint32_t token_len, const char *keyword)
    {
        uint32_t i = 0u;

        if (!token || !keyword)
            return 0;

        while (keyword[i])
        {
            if (i >= token_len || ascii_lower(token[i]) != ascii_lower(keyword[i]))
                return 0;
            ++i;
        }

        return i == token_len ? 1 : 0;
    }

    static int line_starts_with_keyword_ci(const char *line, const char *keyword)
    {
        uint32_t i = 0u;

        if (!line || !keyword)
            return 0;

        while (keyword[i])
        {
            if (!line[i] || ascii_lower(line[i]) != ascii_lower(keyword[i]))
                return 0;
            ++i;
        }

        return line[i] == 0 || ascii_is_space(line[i]);
    }

    static int path_has_ext_ci(const char *path, const char *ext)
    {
        uint32_t path_len = 0u;
        uint32_t ext_len = 0u;

        if (!path || !ext)
            return 0;

        while (path[path_len])
            ++path_len;
        while (ext[ext_len])
            ++ext_len;

        if (path_len < ext_len || ext_len == 0u)
            return 0;

        for (uint32_t i = 0u; i < ext_len; ++i)
        {
            if (ascii_lower(path[path_len - ext_len + i]) != ascii_lower(ext[i]))
                return 0;
        }

        return 1;
    }

    static void sac_newline_behavior(const char *trimmed_line,
                                     uint8_t *out_indent_next,
                                     uint8_t *out_insert_end)
    {
        uint8_t indent_next = 0u;
        uint8_t insert_end = 0u;

        if (trimmed_line && trimmed_line[0] && trimmed_line[0] != '#')
        {
            if (line_starts_with_keyword_ci(trimmed_line, "if") ||
                line_starts_with_keyword_ci(trimmed_line, "while") ||
                line_starts_with_keyword_ci(trimmed_line, "for") ||
                line_starts_with_keyword_ci(trimmed_line, "fn"))
            {
                indent_next = 1u;
                insert_end = 1u;
            }
            else if (line_starts_with_keyword_ci(trimmed_line, "else if") ||
                     line_starts_with_keyword_ci(trimmed_line, "else"))
            {
                indent_next = 1u;
            }
        }

        if (out_indent_next)
            *out_indent_next = indent_next;
        if (out_insert_end)
            *out_insert_end = insert_end;
    }

    static int sac_keyword_token(const char *token, uint32_t token_len)
    {
        static const char *kKeywords[] = {
            "if", "else", "while", "for", "fn", "end",
            "continue", "break", "return", "let", "unset",
            "goto", "call", "exit", "input",
            "add", "sub", "mul", "div", "pow", "mod",
            "rand", "randrange", "seedrand",
            "and", "or", "xor", "not", "shl", "shr", "rol", "ror",
            "cmp", "eq", "neq", "lt", "lte", "gt", "gte", "abs"};

        for (uint32_t i = 0u; i < sizeof(kKeywords) / sizeof(kKeywords[0]); ++i)
        {
            if (token_equals_ci(token, token_len, kKeywords[i]))
                return 1;
        }

        return 0;
    }

    static int token_has_known_namespace(const char *token, uint32_t token_len)
    {
        static const char *kNamespaces[] = {"sys", "fs", "hw"};
        uint32_t colon = token_len;

        if (!token || token_len == 0u)
            return 0;

        for (uint32_t i = 0u; i < token_len; ++i)
        {
            if (token[i] == ':')
            {
                colon = i;
                break;
            }
        }

        if (colon == 0u || colon >= token_len)
            return 0;

        for (uint32_t i = 0u; i < sizeof(kNamespaces) / sizeof(kNamespaces[0]); ++i)
        {
            if (token_equals_ci(token, colon, kNamespaces[i]))
                return 1;
        }

        return 0;
    }

    static void line_append_char(char *dst, uint32_t cap, uint32_t *len, char ch)
    {
        if (!dst || !len || cap == 0u)
            return;

        if (*len + 1u >= cap)
            return;

        dst[*len] = ch;
        ++(*len);
        dst[*len] = 0;
    }

    static void line_append_span(char *dst, uint32_t cap, uint32_t *len, const char *src, uint32_t src_len)
    {
        if (!dst || !len || !src || cap == 0u)
            return;

        for (uint32_t i = 0u; i < src_len && *len + 1u < cap; ++i)
        {
            dst[*len] = src[i];
            ++(*len);
        }
        dst[*len] = 0;
    }

    static void line_append_cstr(char *dst, uint32_t cap, uint32_t *len, const char *src)
    {
        if (!src)
            return;

        while (*src && *len + 1u < cap)
        {
            dst[*len] = *src;
            ++(*len);
            ++src;
        }
        dst[*len] = 0;
    }

    static void line_append_color_set(char *dst, uint32_t cap, uint32_t *len, const char *hex_rgb)
    {
        line_append_char(dst, cap, len, KTEXT_INLINE_COLOR_CTRL);
        line_append_char(dst, cap, len, '<');
        line_append_cstr(dst, cap, len, hex_rgb);
        line_append_char(dst, cap, len, '>');
    }

    static void line_append_color_reset(char *dst, uint32_t cap, uint32_t *len)
    {
        line_append_char(dst, cap, len, KTEXT_INLINE_COLOR_CTRL);
        line_append_char(dst, cap, len, '.');
    }

    static int sac_line_is_leading_colon_label(const char *src, uint32_t index)
    {
        uint32_t i = 0u;

        if (!src || src[index] != ':')
            return 0;

        for (i = 0u; i < index; ++i)
        {
            if (src[i] != ' ' && src[i] != '\t')
                return 0;
        }

        return ascii_is_name_start(src[index + 1u]) ? 1 : 0;
    }

    static void sac_render_highlighted_line(const char *src, char *dst, uint32_t cap)
    {
        // Color palette chosen for high contrast against the editor's dark viewport.
        static const char *kKeyword = "7BC8FF";
        static const char *kUserVar = "FFA658";
        static const char *kRuntimeVar = "FFEB7A";
        static const char *kComment = "7FA18E";
        static const char *kLabel = "C78DFF";
        static const char *kCommand = "8CF5D0";
        static const char *kOperator = "FF9DB5";
        static const char *kFnArg = "9FE8A6";
        static const char *kNumber = "F9C780";

        uint32_t out_len = 0u;
        uint32_t i = 0u;
        uint8_t fn_arg_context = 0u;
        uint8_t call_arg_context = 0u;

        if (!dst || cap == 0u)
            return;

        dst[0] = 0;
        if (!src)
            return;

        while (src[i] && out_len + 1u < cap)
        {
            uint32_t j = 0u;

            if (src[i] == '#')
            {
                line_append_color_set(dst, cap, &out_len, kComment);
                while (src[i] && out_len + 1u < cap)
                {
                    line_append_char(dst, cap, &out_len, src[i]);
                    ++i;
                }
                line_append_color_reset(dst, cap, &out_len);
                break;
            }

            if (src[i] == '$')
            {
                j = i + 1u;
                if (src[j] == '{')
                {
                    ++j;
                    while (src[j] && src[j] != '}' && out_len + 1u < cap)
                        ++j;
                    if (src[j] == '}')
                    {
                        ++j;
                        line_append_color_set(dst, cap, &out_len, kUserVar);
                        line_append_span(dst, cap, &out_len, src + i, j - i);
                        line_append_color_reset(dst, cap, &out_len);
                        i = j;
                        continue;
                    }
                }
                else if (ascii_is_name_start(src[j]))
                {
                    ++j;
                    while (ascii_is_name_char(src[j]))
                        ++j;
                    line_append_color_set(dst, cap, &out_len, kUserVar);
                    line_append_span(dst, cap, &out_len, src + i, j - i);
                    line_append_color_reset(dst, cap, &out_len);
                    i = j;
                    continue;
                }
            }

            if (src[i] == '%')
            {
                j = i + 1u;
                if (ascii_is_digit(src[j]))
                {
                    while (ascii_is_digit(src[j]))
                        ++j;
                    line_append_color_set(dst, cap, &out_len, kRuntimeVar);
                    line_append_span(dst, cap, &out_len, src + i, j - i);
                    line_append_color_reset(dst, cap, &out_len);
                    i = j;
                    continue;
                }
                if (ascii_is_name_start(src[j]))
                {
                    ++j;
                    while (src[j] && src[j] != '%')
                        ++j;
                    if (src[j] == '%')
                    {
                        ++j;
                        line_append_color_set(dst, cap, &out_len, kRuntimeVar);
                        line_append_span(dst, cap, &out_len, src + i, j - i);
                        line_append_color_reset(dst, cap, &out_len);
                        i = j;
                        continue;
                    }
                }
            }

            if (sac_line_is_leading_colon_label(src, i))
            {
                j = i + 1u;
                while (ascii_is_name_char(src[j]))
                    ++j;
                line_append_color_set(dst, cap, &out_len, kLabel);
                line_append_span(dst, cap, &out_len, src + i, j - i);
                line_append_color_reset(dst, cap, &out_len);
                i = j;
                continue;
            }

            if (ascii_is_name_start(src[i]))
            {
                j = i + 1u;
                while (ascii_is_name_char(src[j]) || src[j] == ':')
                    ++j;

                if (sac_keyword_token(src + i, j - i))
                {
                    uint8_t is_fn = token_equals_ci(src + i, j - i, "fn") ? 1u : 0u;
                    uint8_t is_call = token_equals_ci(src + i, j - i, "call") ? 1u : 0u;

                    line_append_color_set(dst, cap, &out_len, kKeyword);
                    line_append_span(dst, cap, &out_len, src + i, j - i);
                    line_append_color_reset(dst, cap, &out_len);

                    if (is_fn)
                    {
                        fn_arg_context = 1u;
                        call_arg_context = 0u;
                    }
                    else if (is_call)
                    {
                        call_arg_context = 1u;
                        fn_arg_context = 0u;
                    }
                    else
                    {
                        fn_arg_context = 0u;
                        call_arg_context = 0u;
                    }

                    i = j;
                    continue;
                }

                if (token_has_known_namespace(src + i, j - i))
                {
                    line_append_color_set(dst, cap, &out_len, kCommand);
                    line_append_span(dst, cap, &out_len, src + i, j - i);
                    line_append_color_reset(dst, cap, &out_len);
                    i = j;
                    continue;
                }

                if (fn_arg_context || call_arg_context)
                {
                    line_append_color_set(dst, cap, &out_len, kFnArg);
                    line_append_span(dst, cap, &out_len, src + i, j - i);
                    line_append_color_reset(dst, cap, &out_len);
                    i = j;
                    continue;
                }

                line_append_span(dst, cap, &out_len, src + i, j - i);
                i = j;
                continue;
            }

            if (ascii_is_digit(src[i]))
            {
                j = i + 1u;
                while (ascii_is_digit(src[j]))
                    ++j;

                if (src[j] == '.' && ascii_is_digit(src[j + 1u]))
                {
                    ++j;
                    while (ascii_is_digit(src[j]))
                        ++j;
                }

                line_append_color_set(dst, cap, &out_len, kNumber);
                line_append_span(dst, cap, &out_len, src + i, j - i);
                line_append_color_reset(dst, cap, &out_len);
                i = j;
                continue;
            }

            if ((src[i] == '=' && src[i + 1u] == '=') ||
                (src[i] == '!' && src[i + 1u] == '=') ||
                (src[i] == '<' && src[i + 1u] == '=') ||
                (src[i] == '>' && src[i + 1u] == '=') ||
                (src[i] == '-' && src[i + 1u] == '>') ||
                (src[i] == '&' && src[i + 1u] == '&') ||
                (src[i] == '|' && src[i + 1u] == '|'))
            {
                line_append_color_set(dst, cap, &out_len, kOperator);
                line_append_char(dst, cap, &out_len, src[i]);
                line_append_char(dst, cap, &out_len, src[i + 1u]);
                line_append_color_reset(dst, cap, &out_len);
                i += 2u;
                continue;
            }

            if (src[i] == '=' || src[i] == '<' || src[i] == '>' || src[i] == '!' ||
                src[i] == '+' || src[i] == '-' || src[i] == '*' || src[i] == '/' ||
                src[i] == '%' || src[i] == '^' || src[i] == '&' || src[i] == '~')
            {
                line_append_color_set(dst, cap, &out_len, kOperator);
                line_append_char(dst, cap, &out_len, src[i]);
                line_append_color_reset(dst, cap, &out_len);
                ++i;
                continue;
            }

            line_append_char(dst, cap, &out_len, src[i]);
            ++i;
        }

        dst[out_len] = 0;
    }

    class TextEditor
    {
    public:
        void Init(const kfont *font);
        void Update();
        void Activate();
        int Visible() const;
        int Initialized() const;
        void NewDocument();
        int OpenPath(const char *raw_path, const char *friendly_path);

    private:
        enum
        {
            TEXT_CAP = 65536,
            MAX_LINES = TEXT_CAP + 1,
            MAX_VISIBLE_LINES = 96,
            LINE_RENDER_CAP = 1024,
            STATUS_CAP = 256,
            PATH_CAP = DIHOS_PATH_CAP + 16,
            CARET_BLINK_FRAMES = 30u,
            KEY_REPEAT_DELAY = 30u,
            KEY_REPEAT_INTERVAL = 3u,
            SCROLL_LINES_PER_WHEEL = 3
        };

        enum PendingDialogAction
        {
            PENDING_DIALOG_NONE = 0,
            PENDING_DIALOG_OPEN,
            PENDING_DIALOG_SAVE_AS
        };

        struct LabeledButton
        {
            kbutton_handle button;
            kgfx_obj_handle label;
        };

        void CreateWindow();
        void CreateChrome();
        void CreateButtons();
        void CreateLineObjects();
        void Layout();
        void LayoutButton(LabeledButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h);
        void RefreshPathVisual();
        void RefreshStatusVisual();
        void SetStatus(const char *text, kcolor color);
        void ResetRepeat();
        void ResetCaretBlink();
        void SnapshotFromCurrent(char *dst_buf, uint32_t *dst_len, uint32_t *dst_cursor,
                                 uint8_t *dst_has_selection, uint32_t *dst_sel_anchor, uint32_t *dst_sel_cursor) const;
        void RestoreFromSnapshot(const char *src_buf, uint32_t src_len, uint32_t src_cursor,
                                 uint8_t src_has_selection, uint32_t src_sel_anchor, uint32_t src_sel_cursor);
        void SaveUndoSnapshot();
        void SaveRedoSnapshot();
        void ClearRedoSnapshot();
        void Undo();
        void Redo();
        uint8_t ShiftDown() const;
        uint8_t CtrlDown() const;
        uint8_t CaretVisible() const;
        uint8_t HasSelection() const;
        uint32_t SelectionStart() const;
        uint32_t SelectionEnd() const;
        void ClearSelection();
        void StartSelectionFromCursor();
        void UpdateSelectionToCursor();
        void DeleteSelection();
        void CopySelectionToClipboard();
        void CutSelectionToClipboard();
        void ReplaceSelectionWithText(const char *text);
        void WrapSelection(const char *left, const char *right);
        void PrefixSelection(const char *prefix);
        uint8_t KeyRepeatTrigger(uint8_t usage);
        uint32_t MeasureCharPx(char ch) const;
        uint32_t TextAreaAvailablePx() const;
        uint32_t WrappedSegmentEnd(uint32_t start, uint32_t end, uint32_t available_px) const;
        uint32_t WrappedLineCount(uint32_t line_idx) const;
        uint32_t TotalVisualLines() const;
        uint32_t CursorVisualLine() const;
        uint32_t CursorVisualX() const;
        int VisualSegmentForLine(uint32_t visual_idx, uint32_t *line_idx,
                                 uint32_t *segment_start, uint32_t *segment_end) const;
        void UpdatePreferredFromCursor();
        kgfx_obj *RootObject();
        int RootVisible() const;
        void ClearDocument();
        void RebuildLineStarts();
        uint32_t LineStart(uint32_t line_idx) const;
        uint32_t LineEnd(uint32_t line_idx) const;
        uint32_t CursorLine() const;
        uint32_t CursorColumn() const;
        void SetCursorFromLineColumn(uint32_t line_idx, uint32_t column);
        void MoveCursorVertical(int delta);
        void MoveCursorToLineBoundary(uint8_t end_of_line);
        uint32_t VisibleLineCapacity() const;
        uint32_t MaxTopLine() const;
        void ClampScroll();
        void EnsureCursorVisible();
        void SyncVisibleLines();
        void SetCursorFromVisualLineX(uint32_t visual_idx, int32_t x);
        void RefreshWrapButton();
        void ToggleWrap();
        int SacEditingEnabled() const;
        void MarkTextChanged();
        void InsertChar(char ch);
        void InsertText(const char *text);
        void InsertNewlineSmart();
        void Backspace();
        void DeleteForward();
        void PlaceCursorFromMouse(int32_t mouse_x, int32_t mouse_y);
        void HandleMouse();
        void HandleKeys();
        void SplitCurrentPath(char *dir_out, uint32_t dir_cap, char *name_out, uint32_t name_cap) const;
        void BeginNewDocument();
        void BeginOpenDialog();
        void BeginSaveAsDialog();
        void Save();
        int LoadFromPath(const char *raw_path, const char *friendly_path);
        int SaveToPath(const char *raw_path, const char *friendly_path);

        static void NewClickThunk(kbutton_handle button, void *user);
        static void OpenClickThunk(kbutton_handle button, void *user);
        static void SaveClickThunk(kbutton_handle button, void *user);
        static void SaveAsClickThunk(kbutton_handle button, void *user);
        static void WrapClickThunk(kbutton_handle button, void *user);
        static void ExplorerDialogThunk(int accepted, const char *raw_path, const char *friendly_path, void *user);

    private:
        uint8_t initialized_;
        uint8_t focused_;
        uint8_t dirty_;
        uint8_t wrap_enabled_;
        uint8_t caps_lock_;
        uint8_t prev_mouse_buttons_;
        uint8_t repeat_usage_;
        uint32_t repeat_frames_;
        uint32_t caret_blink_tick_;
        uint32_t approx_char_px_;
        PendingDialogAction pending_dialog_action_;
        const kfont *font_;
        kwindow_handle window_;
        LabeledButton new_button_;
        LabeledButton open_button_;
        LabeledButton save_button_;
        LabeledButton save_as_button_;
        LabeledButton wrap_button_;
        kgfx_obj_handle path_strip_;
        kgfx_obj_handle path_text_;
        kgfx_obj_handle viewport_;
        kgfx_obj_handle status_strip_;
        kgfx_obj_handle status_text_;
        kgfx_obj_handle line_objs_[MAX_VISIBLE_LINES];
        kgfx_obj_handle selection_objs_[MAX_VISIBLE_LINES];
        char line_buffers_[MAX_VISIBLE_LINES][LINE_RENDER_CAP];
        char buffer_[TEXT_CAP];
        char scratch_[TEXT_CAP];
        char clipboard_[TEXT_CAP];
        uint32_t clipboard_len_;
        char undo_buffer_[TEXT_CAP];
        uint32_t undo_len_;
        uint32_t undo_cursor_;
        uint8_t undo_has_selection_;
        uint32_t undo_sel_anchor_;
        uint32_t undo_sel_cursor_;
        uint8_t has_undo_;
        char redo_buffer_[TEXT_CAP];
        uint32_t redo_len_;
        uint32_t redo_cursor_;
        uint8_t redo_has_selection_;
        uint32_t redo_sel_anchor_;
        uint32_t redo_sel_cursor_;
        uint8_t has_redo_;
        uint8_t selecting_mouse_;
        uint8_t has_selection_;
        uint32_t selection_anchor_;
        uint32_t selection_cursor_;
        uint32_t line_starts_[MAX_LINES];
        uint32_t len_;
        uint32_t line_count_;
        uint32_t cursor_;
        uint32_t preferred_column_;
        uint32_t preferred_visual_x_px_;
        uint32_t scroll_top_line_;
        uint32_t view_column_;
        uint32_t visible_line_count_;
        uint32_t line_height_px_;
        int last_root_w_;
        int last_root_h_;
        uint8_t layout_dirty_;
        uint8_t view_dirty_;
        char current_raw_[DIHOS_PATH_CAP];
        char current_friendly_[DIHOS_PATH_CAP];
        char path_buffer_[PATH_CAP];
        char status_buffer_[STATUS_CAP];
        kcolor status_color_;
        kbutton_style action_style_;
    };

    class TextEditorApp
    {
    public:
        void Init(const kfont *font);
        void Update();
        void Activate();
        int Visible() const;
        int OpenPath(const char *raw_path, const char *friendly_path);

    private:
        enum
        {
            MAX_INSTANCES = 6
        };

        TextEditor *AcquireEditor(void);

    private:
        uint8_t initialized_;
        const kfont *font_;
        TextEditor editors_[MAX_INSTANCES];
    };

    static TextEditorApp g_editor_app = {};

    void TextEditor::Init(const kfont *font)
    {
        if (initialized_ || !font)
            return;

        initialized_ = 0u;
        focused_ = 0u;
        dirty_ = 0u;
        wrap_enabled_ = 0u;
        caps_lock_ = 0u;
        prev_mouse_buttons_ = 0u;
        repeat_usage_ = 0u;
        repeat_frames_ = 0u;
        caret_blink_tick_ = 0u;
        approx_char_px_ = 8u;
        pending_dialog_action_ = PENDING_DIALOG_NONE;
        font_ = font;
        window_.idx = -1;
        new_button_.button.idx = -1;
        new_button_.label.idx = -1;
        open_button_.button.idx = -1;
        open_button_.label.idx = -1;
        save_button_.button.idx = -1;
        save_button_.label.idx = -1;
        save_as_button_.button.idx = -1;
        save_as_button_.label.idx = -1;
        wrap_button_.button.idx = -1;
        wrap_button_.label.idx = -1;
        path_strip_.idx = -1;
        path_text_.idx = -1;
        viewport_.idx = -1;
        status_strip_.idx = -1;
        status_text_.idx = -1;
        len_ = 0u;
        line_count_ = 1u;
        cursor_ = 0u;
        preferred_column_ = 0u;
        preferred_visual_x_px_ = 0u;
        scroll_top_line_ = 0u;
        view_column_ = 0u;
        visible_line_count_ = 1u;
        line_height_px_ = 1u;
        last_root_w_ = -1;
        last_root_h_ = -1;
        layout_dirty_ = 1u;
        view_dirty_ = 1u;
        clipboard_[0] = 0;
        clipboard_len_ = 0u;
        undo_buffer_[0] = 0;
        undo_len_ = 0u;
        undo_cursor_ = 0u;
        undo_has_selection_ = 0u;
        undo_sel_anchor_ = 0u;
        undo_sel_cursor_ = 0u;
        has_undo_ = 0u;
        redo_buffer_[0] = 0;
        redo_len_ = 0u;
        redo_cursor_ = 0u;
        redo_has_selection_ = 0u;
        redo_sel_anchor_ = 0u;
        redo_sel_cursor_ = 0u;
        has_redo_ = 0u;
        selecting_mouse_ = 0u;
        has_selection_ = 0u;
        selection_anchor_ = 0u;
        selection_cursor_ = 0u;
        current_raw_[0] = 0;
        current_friendly_[0] = 0;
        path_buffer_[0] = 0;
        copy_text(status_buffer_, sizeof(status_buffer_), "Blank document");
        status_color_ = rgb(188, 202, 224);
        buffer_[0] = 0;
        scratch_[0] = 0;
        line_starts_[0] = 0u;

        for (uint32_t i = 0; i < MAX_VISIBLE_LINES; ++i)
        {
            line_objs_[i].idx = -1;
            selection_objs_[i].idx = -1;
            line_buffers_[i][0] = 0;
        }

        action_style_ = kbutton_style_default();
        action_style_.fill = rgb(42, 55, 79);
        action_style_.hover_fill = rgb(58, 74, 106);
        action_style_.pressed_fill = rgb(76, 97, 136);
        action_style_.outline = rgb(134, 166, 224);
        action_style_.outline_width = 1u;

        CreateWindow();
        if (window_.idx < 0)
            return;

        CreateChrome();
        CreateButtons();
        CreateLineObjects();
        ClearDocument();
        RefreshPathVisual();
        RefreshStatusVisual();
        Layout();
        SyncVisibleLines();
        kwindow_set_visible(window_, 0u);
        initialized_ = 1u;
    }

    void TextEditor::CreateWindow()
    {
        kwindow_style style = kwindow_style_default();

        style.body_fill = rgb(14, 18, 24);
        style.body_outline = rgb(98, 122, 166);
        style.titlebar_fill = rgb(26, 35, 48);
        style.title_color = rgb(239, 243, 248);
        style.close_button_style.fill = rgb(128, 46, 46);
        style.close_button_style.hover_fill = rgb(154, 58, 58);
        style.close_button_style.pressed_fill = rgb(99, 35, 35);
        style.close_button_style.outline = rgb(255, 225, 225);
        style.fullscreen_button_style.fill = rgb(214, 168, 64);
        style.fullscreen_button_style.hover_fill = rgb(232, 186, 82);
        style.fullscreen_button_style.pressed_fill = rgb(182, 138, 50);
        style.fullscreen_button_style.outline = rgb(254, 246, 222);
        style.title_scale = 2u;

        window_ = kwindow_create(132, 96, 720, 500, 18, font_, "Text Editor", &style);
    }

    void TextEditor::CreateChrome()
    {
        kgfx_obj_handle root = kwindow_root(window_);

        path_strip_ = kgfx_obj_add_rect(0, 0, 10, 10, 1, rgb(18, 24, 34), 1);
        viewport_ = kgfx_obj_add_rect(0, 0, 10, 10, 1, rgb(18, 21, 28), 1);
        status_strip_ = kgfx_obj_add_rect(0, 0, 10, 10, 1, rgb(18, 24, 34), 1);
        path_text_ = kgfx_obj_add_text(font_, "", 8, 4, 1, rgb(220, 228, 240), 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1);
        status_text_ = kgfx_obj_add_text(font_, status_buffer_, 8, 4, 1, status_color_, 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1);

        kgfx_obj_set_parent(path_strip_, root);
        kgfx_obj_set_parent(viewport_, root);
        kgfx_obj_set_parent(status_strip_, root);
        kgfx_obj_set_parent(path_text_, path_strip_);
        kgfx_obj_set_parent(status_text_, status_strip_);
        kgfx_obj_set_clip_to_parent(path_strip_, 1u);
        kgfx_obj_set_clip_to_parent(viewport_, 1u);
        kgfx_obj_set_clip_to_parent(status_strip_, 1u);
        kgfx_obj_set_clip_to_parent(path_text_, 1u);
        kgfx_obj_set_clip_to_parent(status_text_, 1u);

        if (kgfx_obj_ref(path_strip_) && kgfx_obj_ref(path_strip_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(path_strip_)->outline = rgb(68, 86, 118);
            kgfx_obj_ref(path_strip_)->outline_width = 1u;
        }

        if (kgfx_obj_ref(viewport_) && kgfx_obj_ref(viewport_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(viewport_)->outline = rgb(66, 82, 110);
            kgfx_obj_ref(viewport_)->outline_width = 1u;
        }

        if (kgfx_obj_ref(status_strip_) && kgfx_obj_ref(status_strip_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(status_strip_)->outline = rgb(68, 86, 118);
            kgfx_obj_ref(status_strip_)->outline_width = 1u;
        }
    }

    void TextEditor::CreateButtons()
    {
        kgfx_obj_handle root = kwindow_root(window_);

        new_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, NewClickThunk, this);
        open_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, OpenClickThunk, this);
        save_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, SaveClickThunk, this);
        save_as_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, SaveAsClickThunk, this);
        wrap_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, WrapClickThunk, this);

        new_button_.label = kgfx_obj_add_text(font_, "New", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        open_button_.label = kgfx_obj_add_text(font_, "Open", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        save_button_.label = kgfx_obj_add_text(font_, "Save", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        save_as_button_.label = kgfx_obj_add_text(font_, "Save As", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        wrap_button_.label = kgfx_obj_add_text(font_, "Wrap Off", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);

        LabeledButton *buttons[] = {&new_button_, &open_button_, &save_button_, &save_as_button_, &wrap_button_};
        for (uint32_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            kgfx_obj_set_parent(kbutton_root(buttons[i]->button), root);
            kgfx_obj_set_clip_to_parent(kbutton_root(buttons[i]->button), 1u);
            kgfx_obj_set_parent(buttons[i]->label, kbutton_root(buttons[i]->button));
            kgfx_obj_set_clip_to_parent(buttons[i]->label, 1u);
        }
    }

    void TextEditor::CreateLineObjects()
    {
        for (uint32_t i = 0; i < MAX_VISIBLE_LINES; ++i)
        {
            line_objs_[i].idx = -1;
            selection_objs_[i].idx = -1;
            line_buffers_[i][0] = 0;
        }
    }

    kgfx_obj *TextEditor::RootObject()
    {
        return kgfx_obj_ref(kwindow_root(window_));
    }

    int TextEditor::RootVisible() const
    {
        if (window_.idx < 0)
            return 0;
        return kwindow_visible(window_);
    }

    int TextEditor::Visible() const
    {
        return RootVisible();
    }

    int TextEditor::Initialized() const
    {
        return initialized_ ? 1 : 0;
    }

    void TextEditor::NewDocument()
    {
        pending_dialog_action_ = PENDING_DIALOG_NONE;
        focused_ = 0u;
        ResetRepeat();
        ResetCaretBlink();
        ClearDocument();
    }

    void TextEditor::LayoutButton(LabeledButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h)
    {
        kgfx_obj *root = kgfx_obj_ref(kbutton_root(button.button));
        kgfx_obj *label = kgfx_obj_ref(button.label);
        uint32_t text_h = text_height_px(font_, 1u);

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        root->u.rect.x = x;
        root->u.rect.y = y;
        root->u.rect.w = w;
        root->u.rect.h = h;

        if (!label || label->kind != KGFX_OBJ_TEXT)
            return;

        label->u.text.scale = 1u;
        label->u.text.x = (int32_t)w / 2;
        label->u.text.y = (int32_t)((h > text_h) ? (h - text_h) / 2u : 0u);
    }

    void TextEditor::Layout()
    {
        kgfx_obj *root = RootObject();
        int32_t pad = 12;
        uint32_t text_h = text_height_px(font_, 1u);
        uint32_t action_h = text_h + 14u;
        uint32_t strip_h = text_h + 12u;
        int32_t buttons_y = 0;
        int32_t path_y = 0;
        int32_t viewport_y = 0;
        int32_t status_y = 0;
        int32_t viewport_h = 0;
        uint32_t client_w = 0u;
        uint32_t button_gap = 8u;
        uint32_t button_w = 1u;
        uint32_t inner_h = 0u;
        uint32_t inner_w = 0u;

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        last_root_w_ = (int)root->u.rect.w;
        last_root_h_ = (int)root->u.rect.h;

        if (root->u.rect.w < 2u * (uint32_t)pad)
            return;

        client_w = root->u.rect.w - 2u * (uint32_t)pad;
        buttons_y = (int32_t)42 + pad;
        path_y = buttons_y + (int32_t)action_h + 10;
        status_y = (int32_t)root->u.rect.h - pad - (int32_t)strip_h;
        viewport_y = path_y + (int32_t)strip_h + 10;
        viewport_h = status_y - 8 - viewport_y;
        if (viewport_h < 24)
            viewport_h = 24;

        button_w = (client_w > button_gap * 4u) ? (client_w - button_gap * 4u) / 5u : client_w;
        if (button_w == 0u)
            button_w = 1u;

        LayoutButton(new_button_, pad, buttons_y, button_w, action_h);
        LayoutButton(open_button_, pad + (int32_t)(button_w + button_gap), buttons_y, button_w, action_h);
        LayoutButton(save_button_, pad + (int32_t)(button_w + button_gap) * 2, buttons_y, button_w, action_h);
        LayoutButton(save_as_button_, pad + (int32_t)(button_w + button_gap) * 3, buttons_y, button_w, action_h);
        LayoutButton(wrap_button_, pad + (int32_t)(button_w + button_gap) * 4, buttons_y, button_w, action_h);

        if (kgfx_obj_ref(path_strip_) && kgfx_obj_ref(path_strip_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(path_strip_)->u.rect.x = pad;
            kgfx_obj_ref(path_strip_)->u.rect.y = path_y;
            kgfx_obj_ref(path_strip_)->u.rect.w = client_w;
            kgfx_obj_ref(path_strip_)->u.rect.h = strip_h;
        }

        if (kgfx_obj_ref(viewport_) && kgfx_obj_ref(viewport_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(viewport_)->u.rect.x = pad;
            kgfx_obj_ref(viewport_)->u.rect.y = viewport_y;
            kgfx_obj_ref(viewport_)->u.rect.w = client_w;
            kgfx_obj_ref(viewport_)->u.rect.h = (uint32_t)viewport_h;
        }

        if (kgfx_obj_ref(status_strip_) && kgfx_obj_ref(status_strip_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(status_strip_)->u.rect.x = pad;
            kgfx_obj_ref(status_strip_)->u.rect.y = status_y;
            kgfx_obj_ref(status_strip_)->u.rect.w = client_w;
            kgfx_obj_ref(status_strip_)->u.rect.h = strip_h;
        }

        if (kgfx_obj_ref(path_text_) && kgfx_obj_ref(path_text_)->kind == KGFX_OBJ_TEXT)
        {
            kgfx_obj_ref(path_text_)->u.text.x = 8;
            kgfx_obj_ref(path_text_)->u.text.y = (int32_t)((strip_h > text_h) ? (strip_h - text_h) / 2u : 0u);
        }

        if (kgfx_obj_ref(status_text_) && kgfx_obj_ref(status_text_)->kind == KGFX_OBJ_TEXT)
        {
            kgfx_obj_ref(status_text_)->u.text.x = 8;
            kgfx_obj_ref(status_text_)->u.text.y = (int32_t)((strip_h > text_h) ? (strip_h - text_h) / 2u : 0u);
        }

        line_height_px_ = ktext_line_height(font_, 1u, 0);
        if (line_height_px_ == 0u)
            line_height_px_ = text_h ? text_h : 1u;

        approx_char_px_ = MeasureCharPx('M');
        if (approx_char_px_ == 0u)
            approx_char_px_ = 8u;

        inner_h = (uint32_t)viewport_h > 8u ? (uint32_t)viewport_h - 8u : (uint32_t)viewport_h;
        inner_w = client_w > 12u ? client_w - 12u : client_w;
        visible_line_count_ = (line_height_px_ > 0u) ? (inner_h / line_height_px_) : 1u;
        if ((inner_h % line_height_px_) != 0u || visible_line_count_ == 0u)
            ++visible_line_count_;
        if (visible_line_count_ > MAX_VISIBLE_LINES)
            visible_line_count_ = MAX_VISIBLE_LINES;
        if (visible_line_count_ == 0u)
            visible_line_count_ = 1u;

        if (inner_w > 0u && approx_char_px_ > 0u)
        {
            uint32_t visible_columns = inner_w / approx_char_px_;
            if ((inner_w % approx_char_px_) != 0u || visible_columns == 0u)
                ++visible_columns;
            if (visible_columns == 0u)
                visible_columns = 1u;
            if (wrap_enabled_)
                view_column_ = 0u;
            else if (view_column_ > visible_columns)
                view_column_ = visible_columns;
        }

        ClampScroll();
        layout_dirty_ = 0u;
        view_dirty_ = 1u;
    }

    void TextEditor::RefreshPathVisual()
    {
        kgfx_obj *path = kgfx_obj_ref(path_text_);

        path_buffer_[0] = 0;
        if (dirty_)
            append_text(path_buffer_, sizeof(path_buffer_), "* ");
        append_text(path_buffer_, sizeof(path_buffer_),
                    current_friendly_[0] ? current_friendly_ : "Untitled");

        if (!path || path->kind != KGFX_OBJ_TEXT)
            return;

        path->u.text.text = path_buffer_;
    }

    void TextEditor::RefreshStatusVisual()
    {
        kgfx_obj *status = kgfx_obj_ref(status_text_);

        if (!status || status->kind != KGFX_OBJ_TEXT)
            return;

        status->fill = status_color_;
        status->u.text.text = status_buffer_;
    }

    void TextEditor::SetStatus(const char *text, kcolor color)
    {
        if (strings_equal(status_buffer_, text) &&
            status_color_.r == color.r &&
            status_color_.g == color.g &&
            status_color_.b == color.b)
            return;

        copy_text(status_buffer_, sizeof(status_buffer_), text);
        status_color_ = color;
        RefreshStatusVisual();
    }

    void TextEditor::ResetRepeat()
    {
        repeat_usage_ = 0u;
        repeat_frames_ = 0u;
    }

    void TextEditor::ResetCaretBlink()
    {
        caret_blink_tick_ = 0u;
    }

    void TextEditor::SnapshotFromCurrent(char *dst_buf, uint32_t *dst_len, uint32_t *dst_cursor,
                                         uint8_t *dst_has_selection, uint32_t *dst_sel_anchor, uint32_t *dst_sel_cursor) const
    {
        if (!dst_buf || !dst_len || !dst_cursor || !dst_has_selection || !dst_sel_anchor || !dst_sel_cursor)
            return;

        for (uint32_t i = 0u; i <= len_ && i < TEXT_CAP; ++i)
            dst_buf[i] = buffer_[i];

        *dst_len = len_;
        *dst_cursor = cursor_;
        *dst_has_selection = has_selection_;
        *dst_sel_anchor = selection_anchor_;
        *dst_sel_cursor = selection_cursor_;
    }

    void TextEditor::RestoreFromSnapshot(const char *src_buf, uint32_t src_len, uint32_t src_cursor,
                                         uint8_t src_has_selection, uint32_t src_sel_anchor, uint32_t src_sel_cursor)
    {
        if (!src_buf || src_len >= TEXT_CAP)
            return;

        for (uint32_t i = 0u; i <= src_len; ++i)
            buffer_[i] = src_buf[i];

        len_ = src_len;
        cursor_ = src_cursor <= len_ ? src_cursor : len_;
        has_selection_ = src_has_selection ? 1u : 0u;
        selection_anchor_ = src_sel_anchor <= len_ ? src_sel_anchor : len_;
        selection_cursor_ = src_sel_cursor <= len_ ? src_sel_cursor : len_;
        if (!HasSelection())
            ClearSelection();

        dirty_ = 1u;
        RefreshPathVisual();
        RebuildLineStarts();
        UpdatePreferredFromCursor();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    void TextEditor::SaveUndoSnapshot()
    {
        SnapshotFromCurrent(undo_buffer_, &undo_len_, &undo_cursor_,
                            &undo_has_selection_, &undo_sel_anchor_, &undo_sel_cursor_);
        has_undo_ = 1u;
    }

    void TextEditor::SaveRedoSnapshot()
    {
        SnapshotFromCurrent(redo_buffer_, &redo_len_, &redo_cursor_,
                            &redo_has_selection_, &redo_sel_anchor_, &redo_sel_cursor_);
        has_redo_ = 1u;
    }

    void TextEditor::ClearRedoSnapshot()
    {
        has_redo_ = 0u;
    }

    void TextEditor::Undo()
    {
        if (!has_undo_)
            return;

        SaveRedoSnapshot();
        RestoreFromSnapshot(undo_buffer_, undo_len_, undo_cursor_,
                            undo_has_selection_, undo_sel_anchor_, undo_sel_cursor_);
        has_undo_ = 0u;
        SetStatus("undo", rgb(188, 202, 224));
    }

    void TextEditor::Redo()
    {
        if (!has_redo_)
            return;

        SaveUndoSnapshot();
        RestoreFromSnapshot(redo_buffer_, redo_len_, redo_cursor_,
                            redo_has_selection_, redo_sel_anchor_, redo_sel_cursor_);
        has_redo_ = 0u;
        SetStatus("redo", rgb(188, 202, 224));
    }

    uint8_t TextEditor::ShiftDown() const
    {
        return (kinput_key_down(KEY_LSHIFT) || kinput_key_down(KEY_RSHIFT)) ? 1u : 0u;
    }

    uint8_t TextEditor::CtrlDown() const
    {
        return (kinput_key_down(KEY_LCTRL) || kinput_key_down(KEY_RCTRL)) ? 1u : 0u;
    }

    uint8_t TextEditor::CaretVisible() const
    {
        if (!focused_)
            return 0u;
        return ((caret_blink_tick_ / CARET_BLINK_FRAMES) & 1u) == 0u ? 1u : 0u;
    }

    uint8_t TextEditor::HasSelection() const
    {
        return (has_selection_ && selection_anchor_ != selection_cursor_) ? 1u : 0u;
    }

    uint32_t TextEditor::SelectionStart() const
    {
        if (!HasSelection())
            return cursor_;
        return selection_anchor_ < selection_cursor_ ? selection_anchor_ : selection_cursor_;
    }

    uint32_t TextEditor::SelectionEnd() const
    {
        if (!HasSelection())
            return cursor_;
        return selection_anchor_ > selection_cursor_ ? selection_anchor_ : selection_cursor_;
    }

    void TextEditor::ClearSelection()
    {
        has_selection_ = 0u;
        selection_anchor_ = cursor_;
        selection_cursor_ = cursor_;
    }

    void TextEditor::StartSelectionFromCursor()
    {
        has_selection_ = 1u;
        selection_anchor_ = cursor_;
        selection_cursor_ = cursor_;
    }

    void TextEditor::UpdateSelectionToCursor()
    {
        if (!has_selection_)
            StartSelectionFromCursor();
        selection_cursor_ = cursor_;
    }

    void TextEditor::DeleteSelection()
    {
        uint32_t start = 0u;
        uint32_t end = 0u;

        if (!HasSelection())
            return;

        start = SelectionStart();
        end = SelectionEnd();
        memmove(buffer_ + start, buffer_ + end, len_ - end + 1u);
        len_ -= (end - start);
        cursor_ = start;
        ClearSelection();
        MarkTextChanged();
        ResetCaretBlink();
    }

    void TextEditor::CopySelectionToClipboard()
    {
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t count = 0u;

        if (!HasSelection())
            return;

        start = SelectionStart();
        end = SelectionEnd();
        count = end - start;
        if (count >= TEXT_CAP)
            count = TEXT_CAP - 1u;

        for (uint32_t i = 0u; i < count; ++i)
            clipboard_[i] = buffer_[start + i];
        clipboard_[count] = 0;
        clipboard_len_ = count;
    }

    void TextEditor::CutSelectionToClipboard()
    {
        if (!HasSelection())
            return;

        CopySelectionToClipboard();
        DeleteSelection();
    }

    void TextEditor::ReplaceSelectionWithText(const char *text)
    {
        if (HasSelection())
            DeleteSelection();
        InsertText(text);
    }

    void TextEditor::WrapSelection(const char *left, const char *right)
    {
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t left_len = 0u;
        uint32_t right_len = 0u;

        if (!HasSelection())
            return;

        if (!left)
            left = "";
        if (!right)
            right = "";

        start = SelectionStart();
        end = SelectionEnd();
        while (left[left_len])
            ++left_len;
        while (right[right_len])
            ++right_len;

        ClearSelection();
        cursor_ = end;
        InsertText(right);
        cursor_ = start;
        InsertText(left);

        has_selection_ = 1u;
        selection_anchor_ = start;
        selection_cursor_ = end + left_len + right_len;
        cursor_ = selection_cursor_;
        UpdatePreferredFromCursor();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    void TextEditor::PrefixSelection(const char *prefix)
    {
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t prefix_len = 0u;

        if (!HasSelection() || !prefix)
            return;

        start = SelectionStart();
        end = SelectionEnd();
        while (prefix[prefix_len])
            ++prefix_len;

        ClearSelection();
        cursor_ = start;
        InsertText(prefix);
        has_selection_ = 1u;
        selection_anchor_ = start;
        selection_cursor_ = end + prefix_len;
        cursor_ = selection_cursor_;
        UpdatePreferredFromCursor();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    uint8_t TextEditor::KeyRepeatTrigger(uint8_t usage)
    {
        if (kinput_key_pressed(usage))
        {
            repeat_usage_ = usage;
            repeat_frames_ = 0u;
            return 1u;
        }

        if (repeat_usage_ != usage)
            return 0u;

        if (!kinput_key_down(usage))
        {
            ResetRepeat();
            return 0u;
        }

        ++repeat_frames_;
        if (repeat_frames_ < KEY_REPEAT_DELAY)
            return 0u;

        return ((repeat_frames_ - KEY_REPEAT_DELAY) % KEY_REPEAT_INTERVAL) == 0u ? 1u : 0u;
    }

    uint32_t TextEditor::MeasureCharPx(char ch) const
    {
        char text[2];

        if (!font_)
            return 0u;

        text[0] = ch;
        text[1] = 0;
        return ktext_measure_line_px(font_, text, 1u, 1);
    }

    uint32_t TextEditor::TextAreaAvailablePx() const
    {
        kgfx_obj *viewport = kgfx_obj_ref(viewport_);
        uint32_t pad_x = 6u;

        if (!viewport || viewport->kind != KGFX_OBJ_RECT)
            return 0u;
        if (viewport->u.rect.w <= pad_x * 2u)
            return viewport->u.rect.w;
        return viewport->u.rect.w - pad_x * 2u;
    }

    uint32_t TextEditor::WrappedSegmentEnd(uint32_t start, uint32_t end, uint32_t available_px) const
    {
        uint32_t pos = start;
        uint32_t used_px = 0u;

        if (!wrap_enabled_ || start >= end || available_px == 0u)
            return end;

        while (pos < end)
        {
            uint32_t ch_px = MeasureCharPx(buffer_[pos]) + 1u;
            if (pos > start && used_px + ch_px > available_px)
                break;
            used_px += ch_px;
            ++pos;
        }

        return pos > start ? pos : start + 1u;
    }

    uint32_t TextEditor::WrappedLineCount(uint32_t line_idx) const
    {
        uint32_t start = LineStart(line_idx);
        uint32_t end = LineEnd(line_idx);
        uint32_t available_px = TextAreaAvailablePx();
        uint32_t count = 0u;
        uint32_t pos = start;

        if (!wrap_enabled_ || start >= end)
            return 1u;

        while (pos < end && count < MAX_LINES)
        {
            uint32_t next = WrappedSegmentEnd(pos, end, available_px);
            if (next <= pos)
                next = pos + 1u;
            pos = next;
            ++count;
        }

        return count ? count : 1u;
    }

    uint32_t TextEditor::TotalVisualLines() const
    {
        uint32_t total = 0u;

        if (!wrap_enabled_)
            return line_count_ ? line_count_ : 1u;

        for (uint32_t i = 0u; i < line_count_ && total < MAX_LINES; ++i)
        {
            uint32_t count = WrappedLineCount(i);
            if (count > MAX_LINES - total)
                return MAX_LINES;
            total += count;
        }

        return total ? total : 1u;
    }

    int TextEditor::VisualSegmentForLine(uint32_t visual_idx, uint32_t *line_idx,
                                         uint32_t *segment_start, uint32_t *segment_end) const
    {
        uint32_t remaining = visual_idx;
        uint32_t fallback_line = line_count_ ? line_count_ - 1u : 0u;

        if (!wrap_enabled_)
        {
            if (remaining >= line_count_)
                remaining = fallback_line;
            if (line_idx)
                *line_idx = remaining;
            if (segment_start)
                *segment_start = LineStart(remaining);
            if (segment_end)
                *segment_end = LineEnd(remaining);
            return line_count_ > 0u ? 1 : 0;
        }

        for (uint32_t i = 0u; i < line_count_; ++i)
        {
            uint32_t start = LineStart(i);
            uint32_t end = LineEnd(i);
            uint32_t count = WrappedLineCount(i);

            if (remaining >= count)
            {
                remaining -= count;
                continue;
            }

            if (start >= end)
            {
                if (line_idx)
                    *line_idx = i;
                if (segment_start)
                    *segment_start = start;
                if (segment_end)
                    *segment_end = end;
                return 1;
            }

            for (uint32_t seg = 0u; seg < remaining; ++seg)
            {
                uint32_t next = WrappedSegmentEnd(start, end, TextAreaAvailablePx());
                if (next <= start)
                    next = start + 1u;
                start = next;
            }

            if (line_idx)
                *line_idx = i;
            if (segment_start)
                *segment_start = start;
            if (segment_end)
                *segment_end = WrappedSegmentEnd(start, end, TextAreaAvailablePx());
            return 1;
        }

        if (line_idx)
            *line_idx = fallback_line;
        if (segment_start)
            *segment_start = LineStart(fallback_line);
        if (segment_end)
            *segment_end = LineEnd(fallback_line);
        return line_count_ > 0u ? 1 : 0;
    }

    uint32_t TextEditor::CursorVisualLine() const
    {
        uint32_t cursor_line = CursorLine();
        uint32_t visual = 0u;
        uint32_t start = 0u;
        uint32_t end = 0u;

        if (!wrap_enabled_)
            return cursor_line;

        for (uint32_t i = 0u; i < cursor_line && i < line_count_; ++i)
            visual += WrappedLineCount(i);

        start = LineStart(cursor_line);
        end = LineEnd(cursor_line);
        if (start >= end)
            return visual;

        while (start < end)
        {
            uint32_t next = WrappedSegmentEnd(start, end, TextAreaAvailablePx());
            if (next <= start)
                next = start + 1u;
            if (cursor_ < next || next >= end)
                return visual;
            start = next;
            ++visual;
        }

        return visual;
    }

    uint32_t TextEditor::CursorVisualX() const
    {
        uint32_t visual = CursorVisualLine();
        uint32_t line_idx = 0u;
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t x = 0u;
        uint32_t pos = 0u;

        if (!VisualSegmentForLine(visual, &line_idx, &start, &end))
            return 0u;

        (void)line_idx;
        pos = start;
        while (pos < cursor_ && pos < end)
        {
            x += MeasureCharPx(buffer_[pos]) + 1u;
            ++pos;
        }

        return x;
    }

    void TextEditor::UpdatePreferredFromCursor()
    {
        preferred_column_ = CursorColumn();
        preferred_visual_x_px_ = CursorVisualX();
    }

    void TextEditor::ClearDocument()
    {
        len_ = 0u;
        line_count_ = 1u;
        cursor_ = 0u;
        preferred_column_ = 0u;
        preferred_visual_x_px_ = 0u;
        scroll_top_line_ = 0u;
        view_column_ = 0u;
        dirty_ = 0u;
        has_undo_ = 0u;
        has_redo_ = 0u;
        selecting_mouse_ = 0u;
        ClearSelection();
        buffer_[0] = 0;
        line_starts_[0] = 0u;
        current_raw_[0] = 0;
        current_friendly_[0] = 0;
        RefreshPathVisual();
        SetStatus("Blank document", rgb(188, 202, 224));
        view_dirty_ = 1u;
    }

    void TextEditor::RebuildLineStarts()
    {
        line_count_ = 1u;
        line_starts_[0] = 0u;

        for (uint32_t i = 0u; i < len_ && line_count_ < MAX_LINES; ++i)
        {
            if (buffer_[i] == '\n')
                line_starts_[line_count_++] = i + 1u;
        }
    }

    uint32_t TextEditor::LineStart(uint32_t line_idx) const
    {
        if (line_idx >= line_count_)
            return len_;
        return line_starts_[line_idx];
    }

    uint32_t TextEditor::LineEnd(uint32_t line_idx) const
    {
        uint32_t end = len_;

        if (line_idx + 1u < line_count_)
            end = line_starts_[line_idx + 1u];
        if (end > 0u && end <= len_ && buffer_[end - 1u] == '\n')
            --end;
        return end;
    }

    uint32_t TextEditor::CursorLine() const
    {
        uint32_t low = 0u;
        uint32_t high = (line_count_ > 0u) ? (line_count_ - 1u) : 0u;

        while (low < high)
        {
            uint32_t mid = (low + high + 1u) / 2u;
            if (line_starts_[mid] <= cursor_)
                low = mid;
            else
                high = mid - 1u;
        }

        return low;
    }

    uint32_t TextEditor::CursorColumn() const
    {
        uint32_t line_idx = CursorLine();
        uint32_t line_start = LineStart(line_idx);
        return cursor_ >= line_start ? (cursor_ - line_start) : 0u;
    }

    void TextEditor::SetCursorFromLineColumn(uint32_t line_idx, uint32_t column)
    {
        uint32_t start = LineStart(line_idx);
        uint32_t end = LineEnd(line_idx);
        uint32_t line_len = end >= start ? (end - start) : 0u;

        if (column > line_len)
            column = line_len;

        cursor_ = start + column;
        preferred_column_ = column;
        preferred_visual_x_px_ = CursorVisualX();
        ResetCaretBlink();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    void TextEditor::MoveCursorVertical(int delta)
    {
        int32_t target = 0;
        uint32_t current_line = CursorLine();

        if (line_count_ == 0u)
            return;

        if (wrap_enabled_)
        {
            uint32_t total = TotalVisualLines();
            uint32_t current_visual = CursorVisualLine();
            uint32_t keep_x = preferred_visual_x_px_;

            target = (int32_t)current_visual + delta;
            if (target < 0)
                target = 0;
            if ((uint32_t)target >= total)
                target = (int32_t)total - 1;

            SetCursorFromVisualLineX((uint32_t)target, (int32_t)keep_x);
            preferred_visual_x_px_ = keep_x;
            return;
        }

        target = (int32_t)current_line + delta;
        if (target < 0)
            target = 0;
        if ((uint32_t)target >= line_count_)
            target = (int32_t)line_count_ - 1;

        SetCursorFromLineColumn((uint32_t)target, preferred_column_);
    }

    void TextEditor::MoveCursorToLineBoundary(uint8_t end_of_line)
    {
        uint32_t line_idx = CursorLine();
        cursor_ = end_of_line ? LineEnd(line_idx) : LineStart(line_idx);
        UpdatePreferredFromCursor();
        ResetCaretBlink();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    uint32_t TextEditor::VisibleLineCapacity() const
    {
        return visible_line_count_ ? visible_line_count_ : 1u;
    }

    uint32_t TextEditor::MaxTopLine() const
    {
        uint32_t visible = VisibleLineCapacity();
        uint32_t total = TotalVisualLines();

        if (total <= visible)
            return 0u;
        return total - visible;
    }

    void TextEditor::ClampScroll()
    {
        if (scroll_top_line_ > MaxTopLine())
            scroll_top_line_ = MaxTopLine();
    }

    void TextEditor::EnsureCursorVisible()
    {
        uint32_t line_idx = CursorLine();
        uint32_t column = CursorColumn();
        uint32_t visual_idx = CursorVisualLine();
        uint32_t visible_lines = VisibleLineCapacity();
        uint32_t visible_columns = 1u;
        kgfx_obj *viewport = kgfx_obj_ref(viewport_);

        if (wrap_enabled_)
        {
            if (visual_idx < scroll_top_line_)
                scroll_top_line_ = visual_idx;
            else if (visual_idx >= scroll_top_line_ + visible_lines)
                scroll_top_line_ = visual_idx - visible_lines + 1u;
            view_column_ = 0u;
            ClampScroll();
            return;
        }

        if (line_idx < scroll_top_line_)
            scroll_top_line_ = line_idx;
        else if (line_idx >= scroll_top_line_ + visible_lines)
            scroll_top_line_ = line_idx - visible_lines + 1u;

        if (viewport && viewport->kind == KGFX_OBJ_RECT && approx_char_px_ > 0u)
        {
            uint32_t inner_w = viewport->u.rect.w > 12u ? viewport->u.rect.w - 12u : viewport->u.rect.w;
            visible_columns = inner_w / approx_char_px_;
            if ((inner_w % approx_char_px_) != 0u || visible_columns == 0u)
                ++visible_columns;
            if (visible_columns == 0u)
                visible_columns = 1u;
        }

        if (column < view_column_)
            view_column_ = column;
        else if (column >= view_column_ + visible_columns)
            view_column_ = column - visible_columns + 1u;

        ClampScroll();
    }

    void TextEditor::SetCursorFromVisualLineX(uint32_t visual_idx, int32_t x)
    {
        uint32_t line_idx = 0u;
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t src = 0u;
        int32_t advance_x = 0;

        if (!VisualSegmentForLine(visual_idx, &line_idx, &start, &end))
            return;

        (void)line_idx;
        src = start;

        if (x > 0)
        {
            while (src < end)
            {
                uint32_t ch_w = MeasureCharPx(buffer_[src]) + 1u;
                int32_t boundary = advance_x + (int32_t)(ch_w / 2u);

                if (x < boundary)
                    break;

                advance_x += (int32_t)ch_w;
                ++src;
            }
        }

        cursor_ = src;
        UpdatePreferredFromCursor();
        ResetCaretBlink();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    void TextEditor::RefreshWrapButton()
    {
        kgfx_obj *label = kgfx_obj_ref(wrap_button_.label);

        if (label && label->kind == KGFX_OBJ_TEXT)
            label->u.text.text = wrap_enabled_ ? "Wrap On" : "Wrap Off";
    }

    void TextEditor::ToggleWrap()
    {
        wrap_enabled_ = wrap_enabled_ ? 0u : 1u;
        if (wrap_enabled_)
            view_column_ = 0u;

        UpdatePreferredFromCursor();
        EnsureCursorVisible();
        RefreshWrapButton();
        SetStatus(wrap_enabled_ ? "text wrap on" : "text wrap off", rgb(188, 202, 224));
        view_dirty_ = 1u;
    }

    int TextEditor::SacEditingEnabled() const
    {
        if (path_has_ext_ci(current_friendly_, ".sac"))
            return 1;
        if (path_has_ext_ci(current_raw_, ".sac"))
            return 1;
        return 0;
    }

    void TextEditor::InsertNewlineSmart()
    {
        static const char *kIndentUnit = "    ";
        char indent[128];
        char trimmed[LINE_RENDER_CAP];
        uint32_t line_idx = 0u;
        uint32_t line_start = 0u;
        uint32_t line_end = 0u;
        uint32_t i = 0u;
        uint32_t indent_len = 0u;
        uint32_t trim_start = 0u;
        uint32_t trim_end = 0u;
        uint32_t trim_len = 0u;
        uint8_t indent_next = 0u;
        uint8_t insert_end = 0u;
        uint32_t body_cursor = 0u;

        if (!SacEditingEnabled())
        {
            InsertChar('\n');
            return;
        }

        line_idx = CursorLine();
        line_start = LineStart(line_idx);
        line_end = LineEnd(line_idx);
        if (cursor_ < line_end)
            line_end = cursor_;

        i = line_start;
        while (i < line_end && (buffer_[i] == ' ' || buffer_[i] == '\t'))
        {
            if (indent_len + 1u < sizeof(indent))
                indent[indent_len++] = buffer_[i];
            ++i;
        }
        indent[indent_len] = 0;

        trim_start = line_start;
        while (trim_start < line_end && (buffer_[trim_start] == ' ' || buffer_[trim_start] == '\t'))
            ++trim_start;

        trim_end = line_end;
        while (trim_end > trim_start && (buffer_[trim_end - 1u] == ' ' || buffer_[trim_end - 1u] == '\t'))
            --trim_end;

        trim_len = trim_end > trim_start ? (trim_end - trim_start) : 0u;
        if (trim_len >= sizeof(trimmed))
            trim_len = sizeof(trimmed) - 1u;

        for (i = 0u; i < trim_len; ++i)
            trimmed[i] = buffer_[trim_start + i];
        trimmed[trim_len] = 0;

        sac_newline_behavior(trimmed, &indent_next, &insert_end);

        InsertChar('\n');
        InsertText(indent);
        if (indent_next)
            InsertText(kIndentUnit);

        body_cursor = cursor_;

        if (insert_end)
        {
            InsertChar('\n');
            InsertText(indent);
            InsertText("end");
            cursor_ = body_cursor;
            UpdatePreferredFromCursor();
            ResetCaretBlink();
            EnsureCursorVisible();
            view_dirty_ = 1u;
        }
    }

    void TextEditor::SyncVisibleLines()
    {
        kgfx_obj *viewport = kgfx_obj_ref(viewport_);
        uint32_t cursor_line = CursorLine();
        uint32_t cursor_column = CursorColumn();
        uint32_t cursor_visual = CursorVisualLine();
        uint32_t total_visual = TotalVisualLines();
        uint8_t caret_visible = CaretVisible();
        uint8_t sac_mode = SacEditingEnabled() ? 1u : 0u;
        uint8_t has_selection = HasSelection();
        uint32_t selection_start = SelectionStart();
        uint32_t selection_end = SelectionEnd();
        uint32_t pad_x = 6u;
        uint32_t pad_y = 4u;
        uint32_t available_px = 0u;

        if (!font_ || !viewport || viewport->kind != KGFX_OBJ_RECT)
            return;

        if (viewport->u.rect.w > pad_x * 2u)
            available_px = viewport->u.rect.w - pad_x * 2u;

        for (uint32_t i = 0u; i < MAX_VISIBLE_LINES; ++i)
        {
            kgfx_obj *line_obj = kgfx_obj_ref(line_objs_[i]);
            kgfx_obj *sel_obj = kgfx_obj_ref(selection_objs_[i]);
            if (line_obj)
                line_obj->visible = 0u;
            if (sel_obj)
                sel_obj->visible = 0u;
        }

        for (uint32_t i = 0u; i < visible_line_count_ && i < MAX_VISIBLE_LINES; ++i)
        {
            uint32_t visual_idx = scroll_top_line_ + i;
            uint32_t line_idx = 0u;
            uint32_t start = 0u;
            uint32_t end = 0u;
            uint32_t src = 0u;
            uint32_t render_start = 0u;
            uint32_t out_len = 0u;
            uint32_t used_px = 0u;
            uint32_t column = 0u;
            uint8_t caret_drawn = 0u;
            char plain_line[LINE_RENDER_CAP];
            kgfx_obj *obj = 0;
            kgfx_obj *sel_obj = 0;

            if (visual_idx >= total_visual)
                break;

            if (line_objs_[i].idx < 0)
            {
                line_objs_[i] = kgfx_obj_add_text(font_, line_buffers_[i], 6, 4, 1,
                                                  rgb(228, 233, 241), 255, 1u, 1, 0,
                                                  KTEXT_ALIGN_LEFT, 0);
                if (line_objs_[i].idx >= 0)
                {
                    kgfx_obj_set_parent(line_objs_[i], viewport_);
                    kgfx_obj_set_clip_to_parent(line_objs_[i], 1u);
                    if (kgfx_obj_ref(line_objs_[i]) && kgfx_obj_ref(line_objs_[i])->kind == KGFX_OBJ_TEXT)
                        kgfx_obj_ref(line_objs_[i])->outline_width = 0u;
                }
            }

            if (selection_objs_[i].idx < 0)
            {
                selection_objs_[i] = kgfx_obj_add_rect(0, 0, 1, line_height_px_ ? line_height_px_ : 1u,
                                                       0, rgb(68, 96, 145), 0u);
                if (selection_objs_[i].idx >= 0)
                {
                    kgfx_obj_set_parent(selection_objs_[i], viewport_);
                    kgfx_obj_set_clip_to_parent(selection_objs_[i], 1u);
                    if (kgfx_obj_ref(selection_objs_[i]) && kgfx_obj_ref(selection_objs_[i])->kind == KGFX_OBJ_RECT)
                    {
                        kgfx_obj_ref(selection_objs_[i])->outline_width = 0u;
                        kgfx_obj_ref(selection_objs_[i])->alpha = 185u;
                    }
                }
            }

            obj = kgfx_obj_ref(line_objs_[i]);
            sel_obj = kgfx_obj_ref(selection_objs_[i]);
            if (!obj || obj->kind != KGFX_OBJ_TEXT)
                break;

            if (!VisualSegmentForLine(visual_idx, &line_idx, &start, &end))
                break;

            src = start;
            column = start >= LineStart(line_idx) ? (start - LineStart(line_idx)) : 0u;

            while (!wrap_enabled_ && src < end && column < view_column_)
            {
                ++src;
                ++column;
            }
            render_start = src;

            while (out_len + 1u < LINE_RENDER_CAP)
            {
                if (focused_ && caret_visible && !caret_drawn &&
                    visual_idx == cursor_visual &&
                    line_idx == cursor_line && column == cursor_column)
                {
                    uint32_t caret_px = MeasureCharPx('|') + 1u;
                    if (available_px == 0u || out_len == 0u || used_px + caret_px <= available_px)
                    {
                        plain_line[out_len++] = '|';
                        used_px += caret_px;
                        caret_drawn = 1u;
                    }
                }

                if (src >= end)
                    break;

                {
                    uint32_t ch_px = MeasureCharPx(buffer_[src]) + 1u;
                    if (available_px > 0u && out_len > 0u && used_px + ch_px > available_px)
                        break;

                    plain_line[out_len++] = buffer_[src++];
                    used_px += ch_px;
                    ++column;
                }
            }

            if (focused_ && caret_visible && !caret_drawn &&
                visual_idx == cursor_visual &&
                line_idx == cursor_line && column == cursor_column &&
                out_len + 1u < LINE_RENDER_CAP)
            {
                plain_line[out_len++] = '|';
            }

            plain_line[out_len] = 0;

            if (sel_obj && sel_obj->kind == KGFX_OBJ_RECT && has_selection && src > render_start)
            {
                uint32_t hi_start = selection_start > render_start ? selection_start : render_start;
                uint32_t hi_end = selection_end < src ? selection_end : src;

                if (hi_start < hi_end)
                {
                    uint32_t x_start = 0u;
                    uint32_t x_width = 0u;

                    for (uint32_t p = render_start; p < hi_start; ++p)
                        x_start += MeasureCharPx(buffer_[p]) + 1u;
                    for (uint32_t p = hi_start; p < hi_end; ++p)
                        x_width += MeasureCharPx(buffer_[p]) + 1u;

                    if (x_width == 0u)
                        x_width = 1u;

                    sel_obj->u.rect.x = (int32_t)pad_x + (int32_t)x_start;
                    sel_obj->u.rect.y = (int32_t)pad_y + (int32_t)(i * line_height_px_);
                    sel_obj->u.rect.w = x_width;
                    sel_obj->u.rect.h = line_height_px_ ? line_height_px_ : 1u;
                    sel_obj->visible = 1u;
                }
            }

            if (sac_mode)
                sac_render_highlighted_line(plain_line, line_buffers_[i], LINE_RENDER_CAP);
            else
                copy_text(line_buffers_[i], LINE_RENDER_CAP, plain_line);

            obj->u.text.font = font_;
            obj->u.text.text = line_buffers_[i];
            obj->u.text.x = (int32_t)pad_x;
            obj->u.text.y = (int32_t)pad_y + (int32_t)(i * line_height_px_);
            obj->u.text.scale = 1u;
            obj->u.text.char_spacing = 1;
            obj->u.text.line_spacing = 0;
            obj->u.text.align = KTEXT_ALIGN_LEFT;
            obj->fill = (visual_idx == cursor_visual) ? rgb(240, 244, 250) : rgb(224, 229, 238);
            obj->alpha = 255u;
            obj->visible = 1u;
        }

        view_dirty_ = 0u;
    }

    void TextEditor::MarkTextChanged()
    {
        dirty_ = 1u;
        RebuildLineStarts();
        UpdatePreferredFromCursor();
        EnsureCursorVisible();
        RefreshPathVisual();
        view_dirty_ = 1u;
    }

    void TextEditor::InsertChar(char ch)
    {
        if (HasSelection())
            DeleteSelection();

        if (len_ + 1u >= TEXT_CAP)
        {
            SetStatus("document is full", rgb(255, 170, 120));
            return;
        }

        memmove(buffer_ + cursor_ + 1u, buffer_ + cursor_, len_ - cursor_ + 1u);
        buffer_[cursor_] = ch;
        ++len_;
        ++cursor_;
        MarkTextChanged();
        ResetCaretBlink();
    }

    void TextEditor::InsertText(const char *text)
    {
        if (!text)
            return;

        while (*text)
            InsertChar(*text++);
    }

    void TextEditor::Backspace()
    {
        uint32_t remove_count = 1u;

        if (HasSelection())
        {
            DeleteSelection();
            return;
        }

        if (cursor_ == 0u || len_ == 0u)
            return;

        if (cursor_ >= 4u)
        {
            uint32_t line_start = LineStart(CursorLine());
            uint32_t column = cursor_ - line_start;
            if (column >= 4u &&
                buffer_[cursor_ - 1u] == ' ' &&
                buffer_[cursor_ - 2u] == ' ' &&
                buffer_[cursor_ - 3u] == ' ' &&
                buffer_[cursor_ - 4u] == ' ')
            {
                remove_count = 4u;
            }
        }

        memmove(buffer_ + cursor_ - remove_count, buffer_ + cursor_, len_ - cursor_ + 1u);
        cursor_ -= remove_count;
        len_ -= remove_count;
        MarkTextChanged();
        ResetCaretBlink();
    }

    void TextEditor::DeleteForward()
    {
        if (HasSelection())
        {
            DeleteSelection();
            return;
        }

        if (cursor_ >= len_)
            return;

        memmove(buffer_ + cursor_, buffer_ + cursor_ + 1u, len_ - cursor_);
        --len_;
        MarkTextChanged();
        ResetCaretBlink();
    }

    void TextEditor::PlaceCursorFromMouse(int32_t mouse_x, int32_t mouse_y)
    {
        kgfx_obj *root = RootObject();
        kgfx_obj *viewport = kgfx_obj_ref(viewport_);
        int32_t world_left = 0;
        int32_t world_top = 0;
        int32_t local_x = 0;
        int32_t local_y = 0;
        uint32_t line_offset = 0u;
        uint32_t line_idx = 0u;
        uint32_t start = 0u;
        uint32_t end = 0u;
        uint32_t src = 0u;
        uint32_t column = 0u;
        int32_t advance_x = 0;

        if (!root || root->kind != KGFX_OBJ_RECT || !viewport || viewport->kind != KGFX_OBJ_RECT)
            return;

        world_left = root->u.rect.x + viewport->u.rect.x + 6;
        world_top = root->u.rect.y + viewport->u.rect.y + 4;
        local_x = mouse_x - world_left;
        local_y = mouse_y - world_top;

        if (local_y < 0)
            local_y = 0;

        line_offset = line_height_px_ > 0u ? (uint32_t)local_y / line_height_px_ : 0u;

        if (wrap_enabled_)
        {
            SetCursorFromVisualLineX(scroll_top_line_ + line_offset, local_x);
            return;
        }

        line_idx = scroll_top_line_ + line_offset;
        if (line_idx >= line_count_)
            line_idx = line_count_ ? (line_count_ - 1u) : 0u;

        start = LineStart(line_idx);
        end = LineEnd(line_idx);
        src = start;
        column = 0u;

        while (src < end && column < view_column_)
        {
            ++src;
            ++column;
        }

        if (local_x <= 0)
        {
            cursor_ = src;
            UpdatePreferredFromCursor();
            ResetCaretBlink();
            EnsureCursorVisible();
            view_dirty_ = 1u;
            return;
        }

        while (src < end)
        {
            uint32_t ch_w = MeasureCharPx(buffer_[src]) + 1u;
            int32_t boundary = advance_x + (int32_t)(ch_w / 2u);

            if (local_x < boundary)
                break;

            advance_x += (int32_t)ch_w;
            ++src;
            ++column;
        }

        cursor_ = src;
        UpdatePreferredFromCursor();
        ResetCaretBlink();
        EnsureCursorVisible();
        view_dirty_ = 1u;
    }

    void TextEditor::HandleMouse()
    {
        kgfx_obj *root = RootObject();
        kgfx_obj *viewport = kgfx_obj_ref(viewport_);
        kmouse_state mouse = {0};
        uint8_t left_now = 0u;
        uint8_t left_pressed = 0u;
        uint8_t left_released = 0u;
        int32_t root_left = 0;
        int32_t root_top = 0;
        int32_t root_right = 0;
        int32_t root_bottom = 0;
        int32_t view_left = 0;
        int32_t view_top = 0;
        int32_t view_right = 0;
        int32_t view_bottom = 0;
        uint32_t old_scroll = 0u;
        int in_root = 0;
        int in_view = 0;
        int accepts_pointer = 0;

        if (!root || root->kind != KGFX_OBJ_RECT || !viewport || viewport->kind != KGFX_OBJ_RECT)
            return;

        kmouse_get_state(&mouse);
        left_now = (mouse.buttons & 0x01u) ? 1u : 0u;
        left_pressed = left_now && !(prev_mouse_buttons_ & 0x01u);
        left_released = (!left_now) && (prev_mouse_buttons_ & 0x01u);

        root_left = root->u.rect.x;
        root_top = root->u.rect.y;
        root_right = root_left + (int32_t)root->u.rect.w;
        root_bottom = root_top + (int32_t)root->u.rect.h;

        view_left = root_left + viewport->u.rect.x;
        view_top = root_top + viewport->u.rect.y;
        view_right = view_left + (int32_t)viewport->u.rect.w;
        view_bottom = view_top + (int32_t)viewport->u.rect.h;
        accepts_pointer = kwindow_point_can_receive_input(window_, mouse.x, mouse.y);

        in_root = accepts_pointer &&
                  mouse.x >= root_left && mouse.y >= root_top &&
                  mouse.x < root_right && mouse.y < root_bottom;
        in_view = accepts_pointer &&
                  mouse.x >= view_left && mouse.y >= view_top &&
                  mouse.x < view_right && mouse.y < view_bottom;

        if (in_view)
            (void)kmouse_set_cursor(KMOUSE_CURSOR_BEAM);

        if (mouse.wheel != 0 && in_view)
        {
            old_scroll = scroll_top_line_;
            if (mouse.wheel > 0)
            {
                uint32_t delta = (uint32_t)(mouse.wheel * SCROLL_LINES_PER_WHEEL);
                scroll_top_line_ = (delta > scroll_top_line_) ? 0u : (scroll_top_line_ - delta);
            }
            else
            {
                scroll_top_line_ += (uint32_t)((-mouse.wheel) * SCROLL_LINES_PER_WHEEL);
                ClampScroll();
            }

            if (scroll_top_line_ != old_scroll)
                view_dirty_ = 1u;
        }

        if (left_pressed)
        {
            if (in_view)
            {
                uint32_t prior_cursor = cursor_;
                uint8_t shift = ShiftDown();
                focused_ = 1u;
                ktextbox_clear_focus();
                ResetRepeat();
                PlaceCursorFromMouse(mouse.x, mouse.y);
                if (shift)
                {
                    if (!has_selection_)
                    {
                        has_selection_ = 1u;
                        selection_anchor_ = prior_cursor;
                    }
                    selection_cursor_ = cursor_;
                }
                else
                {
                    has_selection_ = 1u;
                    selection_anchor_ = cursor_;
                    selection_cursor_ = cursor_;
                }
                selecting_mouse_ = 1u;
                view_dirty_ = 1u;
            }
            else if (in_root)
            {
                focused_ = 0u;
                selecting_mouse_ = 0u;
            }
            else
            {
                focused_ = 0u;
                selecting_mouse_ = 0u;
            }
        }

        if (left_now && selecting_mouse_ && in_view)
        {
            PlaceCursorFromMouse(mouse.x, mouse.y);
            has_selection_ = 1u;
            selection_cursor_ = cursor_;
            view_dirty_ = 1u;
        }

        if (left_released)
        {
            selecting_mouse_ = 0u;
            if (!HasSelection())
                ClearSelection();
            view_dirty_ = 1u;
        }

        prev_mouse_buttons_ = mouse.buttons;
    }

    void TextEditor::HandleKeys()
    {
        if (!focused_)
            return;

        if (kinput_key_pressed(KEY_CAPSLOCK))
            caps_lock_ = caps_lock_ ? 0u : 1u;

        if (CtrlDown())
        {
            if (kinput_key_pressed(KEY_Z))
            {
                Undo();
                return;
            }

            if (kinput_key_pressed(KEY_Y))
            {
                Redo();
                return;
            }

            if (kinput_key_pressed(KEY_C))
            {
                if (HasSelection())
                {
                    CopySelectionToClipboard();
                    SetStatus("copied selection", rgb(188, 202, 224));
                }
                return;
            }

            if (kinput_key_pressed(KEY_X))
            {
                if (HasSelection())
                {
                    SaveUndoSnapshot();
                    ClearRedoSnapshot();
                    CutSelectionToClipboard();
                    SetStatus("cut selection", rgb(188, 202, 224));
                }
                return;
            }

            if (kinput_key_pressed(KEY_V))
            {
                if (clipboard_len_ > 0u)
                {
                    SaveUndoSnapshot();
                    ClearRedoSnapshot();
                    ReplaceSelectionWithText(clipboard_);
                    SetStatus("pasted", rgb(188, 202, 224));
                }
                return;
            }

            if (kinput_key_pressed(KEY_A))
            {
                cursor_ = len_;
                has_selection_ = 1u;
                selection_anchor_ = 0u;
                selection_cursor_ = len_;
                UpdatePreferredFromCursor();
                EnsureCursorVisible();
                view_dirty_ = 1u;
                return;
            }

            if (kinput_key_pressed(KEY_N))
            {
                BeginNewDocument();
                return;
            }

            if (kinput_key_pressed(KEY_O))
            {
                BeginOpenDialog();
                return;
            }

            if (kinput_key_pressed(KEY_S))
            {
                if (ShiftDown())
                    BeginSaveAsDialog();
                else
                    Save();
                return;
            }
        }

        if (kinput_key_pressed(KEY_ESCAPE))
        {
            focused_ = 0u;
            ClearSelection();
            view_dirty_ = 1u;
            return;
        }

        if (KeyRepeatTrigger(KEY_BACKSPACE))
        {
            if (kinput_key_pressed(KEY_BACKSPACE))
            {
                SaveUndoSnapshot();
                ClearRedoSnapshot();
            }
            Backspace();
        }

        if (KeyRepeatTrigger(KEY_DELETE))
        {
            if (kinput_key_pressed(KEY_DELETE))
            {
                SaveUndoSnapshot();
                ClearRedoSnapshot();
            }
            DeleteForward();
        }

        if (KeyRepeatTrigger(KEY_LEFT))
        {
            if (!ShiftDown() && HasSelection())
            {
                cursor_ = SelectionStart();
                ClearSelection();
            }
            else if (cursor_ > 0u)
            {
                if (ShiftDown())
                {
                    if (!has_selection_)
                        StartSelectionFromCursor();
                    --cursor_;
                    UpdateSelectionToCursor();
                }
                else
                {
                    --cursor_;
                    ClearSelection();
                }
            }
            UpdatePreferredFromCursor();
            ResetCaretBlink();
            EnsureCursorVisible();
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_RIGHT))
        {
            if (!ShiftDown() && HasSelection())
            {
                cursor_ = SelectionEnd();
                ClearSelection();
            }
            else if (cursor_ < len_)
            {
                if (ShiftDown())
                {
                    if (!has_selection_)
                        StartSelectionFromCursor();
                    ++cursor_;
                    UpdateSelectionToCursor();
                }
                else
                {
                    ++cursor_;
                    ClearSelection();
                }
            }
            UpdatePreferredFromCursor();
            ResetCaretBlink();
            EnsureCursorVisible();
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_UP))
        {
            if (ShiftDown())
            {
                if (!has_selection_)
                    StartSelectionFromCursor();
            }
            else
            {
                ClearSelection();
            }
            MoveCursorVertical(-1);
            if (ShiftDown())
                UpdateSelectionToCursor();
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_DOWN))
        {
            if (ShiftDown())
            {
                if (!has_selection_)
                    StartSelectionFromCursor();
            }
            else
            {
                ClearSelection();
            }
            MoveCursorVertical(1);
            if (ShiftDown())
                UpdateSelectionToCursor();
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_HOME))
        {
            if (ShiftDown())
            {
                if (!has_selection_)
                    StartSelectionFromCursor();
            }
            else
            {
                ClearSelection();
            }
            MoveCursorToLineBoundary(0u);
            if (ShiftDown())
                UpdateSelectionToCursor();
            view_dirty_ = 1u;
        }
        if (KeyRepeatTrigger(KEY_END))
        {
            if (ShiftDown())
            {
                if (!has_selection_)
                    StartSelectionFromCursor();
            }
            else
            {
                ClearSelection();
            }
            MoveCursorToLineBoundary(1u);
            if (ShiftDown())
                UpdateSelectionToCursor();
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_PAGEUP))
        {
            uint32_t delta = VisibleLineCapacity();
            ClearSelection();
            if (delta > scroll_top_line_)
                scroll_top_line_ = 0u;
            else
                scroll_top_line_ -= delta;
            MoveCursorVertical(-(int)delta);
            view_dirty_ = 1u;
        }

        if (KeyRepeatTrigger(KEY_PAGEDOWN))
        {
            ClearSelection();
            scroll_top_line_ += VisibleLineCapacity();
            ClampScroll();
            MoveCursorVertical((int)VisibleLineCapacity());
            view_dirty_ = 1u;
        }

        if (kinput_key_pressed(KEY_ENTER) || kinput_key_pressed(KEY_KP_ENTER))
        {
            SaveUndoSnapshot();
            ClearRedoSnapshot();
            InsertNewlineSmart();
        }

        if (kinput_key_pressed(KEY_TAB))
        {
            SaveUndoSnapshot();
            ClearRedoSnapshot();
            InsertText("    ");
        }

        if (!CtrlDown())
        {
            uint8_t shift = ShiftDown();

            for (uint32_t i = 0u; i < sizeof(kPrintableUsages) / sizeof(kPrintableUsages[0]); ++i)
            {
                uint8_t usage = kPrintableUsages[i];
                if (!KeyRepeatTrigger(usage))
                    continue;

                {
                    char ch = usage_to_char(usage, shift, caps_lock_);
                    if (ch)
                    {
                        SaveUndoSnapshot();
                        ClearRedoSnapshot();

                        if (SacEditingEnabled() && HasSelection())
                        {
                            if (ch == '$')
                            {
                                PrefixSelection("$");
                                continue;
                            }
                            if (ch == '%')
                            {
                                WrapSelection("%", "%");
                                continue;
                            }
                            if (ch == '"')
                            {
                                WrapSelection("\"", "\"");
                                continue;
                            }
                            if (ch == '\'')
                            {
                                WrapSelection("'", "'");
                                continue;
                            }
                            if (ch == '(')
                            {
                                WrapSelection("(", ")");
                                continue;
                            }
                            if (ch == '[')
                            {
                                WrapSelection("[", "]");
                                continue;
                            }
                            if (ch == '{')
                            {
                                WrapSelection("{", "}");
                                continue;
                            }
                        }

                        InsertChar(ch);
                    }
                }
            }
        }
    }

    void TextEditor::SplitCurrentPath(char *dir_out, uint32_t dir_cap, char *name_out, uint32_t name_cap) const
    {
        const char *path = current_friendly_;
        uint32_t slash = 0u;
        uint32_t i = 0u;

        if (dir_out && dir_cap > 0u)
            copy_text(dir_out, dir_cap, "/");
        if (name_out && name_cap > 0u)
            name_out[0] = 0;

        if (!path[0])
            return;

        while (path[i])
        {
            if (path[i] == '/')
                slash = i;
            ++i;
        }

        if (slash == 0u)
        {
            if (name_out && name_cap > 0u)
                copy_text(name_out, name_cap, path + 1u);
            return;
        }

        if (dir_out && dir_cap > 0u)
        {
            uint32_t j = 0u;
            while (j < slash && j + 1u < dir_cap)
            {
                dir_out[j] = path[j];
                ++j;
            }
            dir_out[j] = 0;
        }

        if (name_out && name_cap > 0u)
            copy_text(name_out, name_cap, path + slash + 1u);
    }

    void TextEditor::BeginNewDocument()
    {
        NewDocument();
        Activate();
    }

    void TextEditor::BeginOpenDialog()
    {
        char dir[DIHOS_PATH_CAP];
        char name[256];

        SplitCurrentPath(dir, sizeof(dir), name, sizeof(name));
        focused_ = 0u;
        pending_dialog_action_ = PENDING_DIALOG_NONE;

        if (file_explorer_begin_dialog(FILE_EXPLORER_DIALOG_OPEN_FILE,
                                       dir,
                                       name[0] ? name : 0,
                                       ExplorerDialogThunk,
                                       this) != 0)
        {
            SetStatus("unable to open file dialog", rgb(255, 140, 140));
            return;
        }

        pending_dialog_action_ = PENDING_DIALOG_OPEN;
        SetStatus("choose a file to open", rgb(255, 214, 120));
    }

    void TextEditor::BeginSaveAsDialog()
    {
        char dir[DIHOS_PATH_CAP];
        char name[256];

        SplitCurrentPath(dir, sizeof(dir), name, sizeof(name));
        if (!name[0])
            copy_text(name, sizeof(name), "untitled.txt");

        focused_ = 0u;
        pending_dialog_action_ = PENDING_DIALOG_NONE;

        if (file_explorer_begin_dialog(FILE_EXPLORER_DIALOG_SAVE_FILE,
                                       dir,
                                       name,
                                       ExplorerDialogThunk,
                                       this) != 0)
        {
            SetStatus("unable to open save dialog", rgb(255, 140, 140));
            return;
        }

        pending_dialog_action_ = PENDING_DIALOG_SAVE_AS;
        SetStatus("choose where to save this file", rgb(255, 214, 120));
    }

    void TextEditor::Save()
    {
        if (current_raw_[0])
            (void)SaveToPath(current_raw_, current_friendly_);
        else
            BeginSaveAsDialog();
    }

    int TextEditor::LoadFromPath(const char *raw_path, const char *friendly_path)
    {
        KFile file;
        uint64_t size = 0u;
        uint32_t read = 0u;
        uint32_t out_len = 0u;

        if (!raw_path || !raw_path[0] || !friendly_path || !friendly_path[0])
            return -1;

        if (kfile_open(&file, raw_path, KFILE_READ) != 0)
        {
            SetStatus("unable to open file", rgb(255, 140, 140));
            return -1;
        }

        size = kfile_size(&file);
        if (size >= TEXT_CAP)
        {
            kfile_close(&file);
            SetStatus("file is too large for the editor", rgb(255, 170, 120));
            return -1;
        }

        if (size > 0u && kfile_read(&file, scratch_, (uint32_t)size, &read) != 0)
        {
            kfile_close(&file);
            SetStatus("unable to read file", rgb(255, 140, 140));
            return -1;
        }

        kfile_close(&file);

        if (read != (uint32_t)size)
        {
            SetStatus("file read was incomplete", rgb(255, 170, 120));
            return -1;
        }

        for (uint32_t i = 0u; i < read && out_len + 1u < TEXT_CAP; ++i)
        {
            if (scratch_[i] == '\r')
            {
                buffer_[out_len++] = '\n';
                if (i + 1u < read && scratch_[i + 1u] == '\n')
                    ++i;
            }
            else
            {
                buffer_[out_len++] = scratch_[i];
            }
        }

        buffer_[out_len] = 0;
        len_ = out_len;
        cursor_ = 0u;
        preferred_column_ = 0u;
        preferred_visual_x_px_ = 0u;
        scroll_top_line_ = 0u;
        view_column_ = 0u;
        dirty_ = 0u;
        has_undo_ = 0u;
        has_redo_ = 0u;
        selecting_mouse_ = 0u;
        copy_text(current_raw_, sizeof(current_raw_), raw_path);
        copy_text(current_friendly_, sizeof(current_friendly_), friendly_path);
        RebuildLineStarts();
        ClearSelection();
        RefreshPathVisual();
        SetStatus("file opened", rgb(148, 232, 180));
        view_dirty_ = 1u;
        return 0;
    }

    int TextEditor::SaveToPath(const char *raw_path, const char *friendly_path)
    {
        KFile file;
        uint32_t written = 0u;

        if (!raw_path || !raw_path[0] || !friendly_path || !friendly_path[0])
            return -1;

        if (kfile_open(&file, raw_path, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
        {
            SetStatus("unable to save file", rgb(255, 140, 140));
            return -1;
        }

        if (len_ > 0u && kfile_write(&file, buffer_, len_, &written) != 0)
        {
            kfile_close(&file);
            SetStatus("unable to write file", rgb(255, 140, 140));
            return -1;
        }

        kfile_close(&file);

        if (written != len_)
        {
            SetStatus("file write was incomplete", rgb(255, 170, 120));
            return -1;
        }

        dirty_ = 0u;
        copy_text(current_raw_, sizeof(current_raw_), raw_path);
        copy_text(current_friendly_, sizeof(current_friendly_), friendly_path);
        RefreshPathVisual();
        SetStatus("file saved", rgb(148, 232, 180));
        view_dirty_ = 1u;
        return 0;
    }

    void TextEditor::NewClickThunk(kbutton_handle button, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        (void)button;
        if (self)
            self->BeginNewDocument();
    }

    void TextEditor::OpenClickThunk(kbutton_handle button, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        (void)button;
        if (self)
            self->BeginOpenDialog();
    }

    void TextEditor::SaveClickThunk(kbutton_handle button, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        (void)button;
        if (self)
            self->Save();
    }

    void TextEditor::SaveAsClickThunk(kbutton_handle button, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        (void)button;
        if (self)
            self->BeginSaveAsDialog();
    }

    void TextEditor::WrapClickThunk(kbutton_handle button, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        (void)button;
        if (self)
            self->ToggleWrap();
    }

    void TextEditor::ExplorerDialogThunk(int accepted, const char *raw_path, const char *friendly_path, void *user)
    {
        TextEditor *self = (TextEditor *)user;
        PendingDialogAction action = PENDING_DIALOG_NONE;

        if (!self)
            return;

        action = self->pending_dialog_action_;
        self->pending_dialog_action_ = PENDING_DIALOG_NONE;

        if (!accepted)
        {
            if (action == PENDING_DIALOG_OPEN)
                self->SetStatus("open cancelled", rgb(255, 214, 120));
            else if (action == PENDING_DIALOG_SAVE_AS)
                self->SetStatus("save cancelled", rgb(255, 214, 120));
            self->Activate();
            return;
        }

        if (action == PENDING_DIALOG_OPEN)
            (void)self->LoadFromPath(raw_path, friendly_path);
        else if (action == PENDING_DIALOG_SAVE_AS)
            (void)self->SaveToPath(raw_path, friendly_path);

        self->Activate();
    }

    void TextEditor::Activate()
    {
        if (window_.idx < 0)
            return;

        kwindow_set_visible(window_, 1u);
        (void)kwindow_raise(window_);
        ktextbox_clear_focus();
        focused_ = 1u;
        ResetRepeat();
        ResetCaretBlink();

        if (layout_dirty_)
            Layout();

        EnsureCursorVisible();
        SyncVisibleLines();
    }

    int TextEditor::OpenPath(const char *raw_path, const char *friendly_path)
    {
        int rc = LoadFromPath(raw_path, friendly_path);
        Activate();
        return rc;
    }

    void TextEditor::Update()
    {
        kgfx_obj *root = 0;

        if (!initialized_)
            return;

        if (!RootVisible())
        {
            focused_ = 0u;
            return;
        }

        root = RootObject();
        if (root && root->kind == KGFX_OBJ_RECT &&
            ((int)root->u.rect.w != last_root_w_ || (int)root->u.rect.h != last_root_h_))
        {
            layout_dirty_ = 1u;
        }

        ++caret_blink_tick_;
        if (focused_ && (caret_blink_tick_ % CARET_BLINK_FRAMES) == 0u)
            view_dirty_ = 1u;

        if (layout_dirty_)
            Layout();

        HandleMouse();
        HandleKeys();

        if (view_dirty_)
            SyncVisibleLines();
    }

    void TextEditorApp::Init(const kfont *font)
    {
        if (initialized_ || !font)
            return;

        initialized_ = 1u;
        font_ = font;
    }

    TextEditor *TextEditorApp::AcquireEditor(void)
    {
        if (!initialized_ || !font_)
            return 0;

        for (uint32_t i = 0u; i < MAX_INSTANCES; ++i)
        {
            if (editors_[i].Initialized() && !editors_[i].Visible())
                return &editors_[i];
        }

        for (uint32_t i = 0u; i < MAX_INSTANCES; ++i)
        {
            if (!editors_[i].Initialized())
            {
                editors_[i].Init(font_);
                return &editors_[i];
            }
        }

        return 0;
    }

    void TextEditorApp::Update()
    {
        if (!initialized_)
            return;

        for (uint32_t i = 0u; i < MAX_INSTANCES; ++i)
        {
            if (editors_[i].Initialized())
                editors_[i].Update();
        }
    }

    void TextEditorApp::Activate()
    {
        TextEditor *editor = AcquireEditor();

        if (!editor)
            return;

        editor->NewDocument();
        editor->Activate();
    }

    int TextEditorApp::Visible() const
    {
        if (!initialized_)
            return 0;

        for (uint32_t i = 0u; i < MAX_INSTANCES; ++i)
        {
            if (editors_[i].Initialized() && editors_[i].Visible())
                return 1;
        }

        return 0;
    }

    int TextEditorApp::OpenPath(const char *raw_path, const char *friendly_path)
    {
        TextEditor *editor = AcquireEditor();

        if (!editor)
            return -1;

        editor->NewDocument();
        return editor->OpenPath(raw_path, friendly_path);
    }
}

extern "C" void text_editor_init(const kfont *font)
{
    g_editor_app.Init(font);
}

extern "C" void text_editor_update(void)
{
    g_editor_app.Update();
}

extern "C" void text_editor_activate(void)
{
    g_editor_app.Activate();
}

extern "C" int text_editor_visible(void)
{
    return g_editor_app.Visible();
}

extern "C" int text_editor_open_path(const char *raw_path, const char *friendly_path)
{
    return g_editor_app.OpenPath(raw_path, friendly_path);
}
