#include "apps/file_explorer_api.h"

extern "C"
{
#include "filesystem/dihos_path.h"
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/string.h"
}

namespace
{
    static const char *kFolderIcon = "[D]";
    static const char *kFileIcon = "[F]";

    static kcolor rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        kcolor c = {r, g, b};
        return c;
    }

    static uint32_t text_height_px(const kfont *font, uint32_t scale)
    {
        if (!font)
            return 0u;
        return ktext_scale_mul_px(font->h, scale);
    }

    static void copy_text(char *dst, uint32_t cap, const char *src)
    {
        uint32_t i = 0;

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
        uint32_t len = 0;

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

    static void append_char(char *dst, uint32_t cap, char ch)
    {
        uint32_t len = 0;

        if (!dst || cap == 0u)
            return;

        while (dst[len] && len + 1u < cap)
            ++len;

        if (len + 1u >= cap)
            return;

        dst[len++] = ch;
        dst[len] = 0;
    }

    static void append_uint(char *dst, uint32_t cap, uint32_t value)
    {
        char scratch[16];
        uint32_t len = 0;

        if (value == 0u)
        {
            append_char(dst, cap, '0');
            return;
        }

        while (value > 0u && len < sizeof(scratch))
        {
            scratch[len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }

        while (len > 0u)
            append_char(dst, cap, scratch[--len]);
    }

    static int strings_equal(const char *a, const char *b)
    {
        if (!a)
            a = "";
        if (!b)
            b = "";
        return strcmp(a, b) == 0;
    }

    class FileExplorer
    {
    public:
        void Init(const kfont *font);
        void Update();
        void Activate();
        int Visible() const;

    private:
        enum
        {
            MAX_ENTRIES = 1024,
            MAX_VISIBLE_ROWS = 40,
            STATUS_CAP = 256,
            MODAL_TEXT_CAP = 256,
            MODAL_TITLE_CAP = 64,
            MODAL_LABEL_CAP = 24,
            DOUBLE_CLICK_FRAMES = 24,
            SCROLL_LINES_PER_WHEEL = 3
        };

        enum ModalMode
        {
            MODAL_NONE = 0,
            MODAL_CREATE_FOLDER,
            MODAL_CREATE_FILE,
            MODAL_RENAME,
            MODAL_DELETE_CONFIRM
        };

        struct Entry
        {
            char name[256];
            uint8_t is_dir;
            uint32_t size;
        };

        struct LabeledButton
        {
            kbutton_handle button;
            kgfx_obj_handle label;
        };

        struct RowSlot
        {
            FileExplorer *owner;
            kbutton_handle button;
            kgfx_obj_handle icon;
            kgfx_obj_handle name;
            int entry_index;
        };

        void CreateWindow(void);
        void CreateButtons(void);
        void CreateRows(void);
        void CreateModal(void);
        void Layout(void);
        void LayoutButton(LabeledButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t scale);
        void LayoutRow(RowSlot &row, int32_t y, uint32_t w, uint32_t h);
        void SyncActionStates(void);
        void SyncRows(void);
        void HandleMouseWheel(void);
        void ClampScroll(void);
        void EnsureSelectionVisible(void);
        int VisibleRowCapacity(void) const;
        int MaxScrollTop(void) const;
        int RootVisible(void) const;
        kgfx_obj *RootObject(void);
        void SetStatus(const char *text, kcolor color);
        void ShowDefaultStatus(void);
        void RefreshStatusVisual(void);
        void ResetDoubleClick(void);
        void SelectEntry(int idx);
        void ActivateSelection(void);
        void RefreshDirectory(void);
        int ReloadDirectory(const char *preferred_name);
        void SortEntries(void);
        int NavigateTo(const char *input);
        void OpenCreateFolderModal(void);
        void OpenCreateFileModal(void);
        void OpenRenameModal(void);
        void OpenDeleteModal(void);
        void OpenModal(ModalMode mode, const char *title, const char *body, const char *confirm_text, const char *initial_text);
        void CloseModal(void);
        void CommitModal(void);
        int ValidateSegmentName(const char *name);
        int BuildSelectedRawPath(char *out, uint32_t cap) const;
        int BuildSelectedFriendlyPath(char *out, uint32_t cap) const;
        int PathExists(const char *raw, uint8_t *is_dir_out) const;
        int DirectoryIsEmpty(const char *raw) const;
        void ApplyCreateFolder(void);
        void ApplyCreateFile(void);
        void ApplyRename(void);
        void ApplyDelete(void);
        void SetObjectVisible(kgfx_obj_handle handle, uint8_t visible);
        void SetButtonVisible(LabeledButton &button, uint8_t visible);
        void SetRowVisible(RowSlot &row, uint8_t visible);
        void SetTextboxVisible(ktextbox_handle handle, uint8_t visible);

        static void PathSubmitThunk(ktextbox_handle textbox, const char *text, void *user);
        static void ModalSubmitThunk(ktextbox_handle textbox, const char *text, void *user);
        static void RowClickThunk(kbutton_handle button, void *user);
        static void UpClickThunk(kbutton_handle button, void *user);
        static void RefreshClickThunk(kbutton_handle button, void *user);
        static void NewFolderClickThunk(kbutton_handle button, void *user);
        static void NewFileClickThunk(kbutton_handle button, void *user);
        static void RenameClickThunk(kbutton_handle button, void *user);
        static void DeleteClickThunk(kbutton_handle button, void *user);
        static void ModalConfirmClickThunk(kbutton_handle button, void *user);
        static void ModalCancelClickThunk(kbutton_handle button, void *user);

    private:
        uint8_t initialized_;
        uint32_t frame_counter_;
        const kfont *font_;
        kwindow_handle window_;
        ktextbox_handle path_box_;
        LabeledButton up_button_;
        LabeledButton refresh_button_;
        LabeledButton new_folder_button_;
        LabeledButton new_file_button_;
        LabeledButton rename_button_;
        LabeledButton delete_button_;
        kgfx_obj_handle list_viewport_;
        kgfx_obj_handle status_strip_;
        kgfx_obj_handle status_text_;
        RowSlot rows_[MAX_VISIBLE_ROWS];
        ktextbox_handle modal_input_;
        kgfx_obj_handle modal_backdrop_;
        kgfx_obj_handle modal_panel_;
        kgfx_obj_handle modal_title_;
        kgfx_obj_handle modal_body_;
        kgfx_obj_handle modal_error_;
        LabeledButton modal_confirm_button_;
        LabeledButton modal_cancel_button_;
        ModalMode modal_mode_;
        Entry entries_[MAX_ENTRIES];
        int entry_count_;
        int selected_index_;
        int scroll_top_;
        int visible_rows_;
        int last_click_index_;
        uint32_t last_click_frame_;
        uint8_t truncated_;
        char current_dir_[DIHOS_PATH_CAP];
        char current_raw_[DIHOS_PATH_CAP];
        char status_buffer_[STATUS_CAP];
        char modal_title_buffer_[MODAL_TITLE_CAP];
        char modal_body_buffer_[MODAL_TEXT_CAP];
        char modal_error_buffer_[MODAL_TEXT_CAP];
        char modal_confirm_label_[MODAL_LABEL_CAP];
        kcolor status_color_;
        kbutton_style action_style_;
        kbutton_style row_style_;
        kbutton_style row_selected_style_;
        kbutton_style modal_confirm_style_;
        kbutton_style modal_delete_style_;
    };

    static FileExplorer g_explorer = {};

    void FileExplorer::Init(const kfont *font)
    {
        if (initialized_ || !font)
            return;

        font_ = font;
        frame_counter_ = 0u;
        window_.idx = -1;
        path_box_.idx = -1;
        list_viewport_.idx = -1;
        status_strip_.idx = -1;
        status_text_.idx = -1;
        modal_input_.idx = -1;
        modal_backdrop_.idx = -1;
        modal_panel_.idx = -1;
        modal_title_.idx = -1;
        modal_body_.idx = -1;
        modal_error_.idx = -1;
        modal_mode_ = MODAL_NONE;
        selected_index_ = -1;
        scroll_top_ = 0;
        visible_rows_ = 0;
        last_click_index_ = -1;
        last_click_frame_ = 0u;
        entry_count_ = 0;
        truncated_ = 0u;
        copy_text(current_dir_, sizeof(current_dir_), "/");
        copy_text(current_raw_, sizeof(current_raw_), "0:/");
        copy_text(status_buffer_, sizeof(status_buffer_), "loading explorer...");
        modal_title_buffer_[0] = 0;
        modal_body_buffer_[0] = 0;
        modal_error_buffer_[0] = 0;
        modal_confirm_label_[0] = 0;
        status_color_ = rgb(222, 228, 238);

        action_style_ = kbutton_style_default();
        action_style_.fill = rgb(44, 56, 78);
        action_style_.hover_fill = rgb(58, 73, 100);
        action_style_.pressed_fill = rgb(82, 105, 144);
        action_style_.outline = rgb(120, 150, 210);
        action_style_.outline_width = 1u;

        row_style_ = kbutton_style_default();
        row_style_.fill = rgb(23, 26, 34);
        row_style_.hover_fill = rgb(35, 43, 58);
        row_style_.pressed_fill = rgb(47, 58, 79);
        row_style_.outline = rgb(55, 66, 89);
        row_style_.outline_width = 1u;

        row_selected_style_ = row_style_;
        row_selected_style_.fill = rgb(50, 80, 126);
        row_selected_style_.hover_fill = rgb(62, 95, 148);
        row_selected_style_.pressed_fill = rgb(74, 112, 170);
        row_selected_style_.outline = rgb(144, 194, 255);

        modal_confirm_style_ = action_style_;
        modal_confirm_style_.fill = rgb(52, 112, 80);
        modal_confirm_style_.hover_fill = rgb(66, 136, 98);
        modal_confirm_style_.pressed_fill = rgb(38, 89, 62);
        modal_confirm_style_.outline = rgb(140, 225, 175);

        modal_delete_style_ = action_style_;
        modal_delete_style_.fill = rgb(138, 53, 53);
        modal_delete_style_.hover_fill = rgb(166, 67, 67);
        modal_delete_style_.pressed_fill = rgb(112, 41, 41);
        modal_delete_style_.outline = rgb(255, 180, 180);

        for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
        {
            rows_[i].owner = this;
            rows_[i].button.idx = -1;
            rows_[i].icon.idx = -1;
            rows_[i].name.idx = -1;
            rows_[i].entry_index = -1;
        }

        CreateWindow();
        if (window_.idx < 0)
            return;

        CreateButtons();
        CreateRows();
        CreateModal();
        Layout();

        if (ReloadDirectory(0) != 0)
            SetStatus("unable to read /", rgb(255, 140, 140));
        else
            ShowDefaultStatus();

        ktextbox_set_text(path_box_, current_dir_);
        initialized_ = 1u;
    }

    void FileExplorer::CreateWindow(void)
    {
        kwindow_style style = kwindow_style_default();

        style.body_fill = rgb(15, 18, 24);
        style.body_outline = rgb(91, 112, 156);
        style.titlebar_fill = rgb(26, 33, 47);
        style.title_color = rgb(235, 240, 248);
        style.close_button_style.fill = rgb(128, 46, 46);
        style.close_button_style.hover_fill = rgb(154, 58, 58);
        style.close_button_style.pressed_fill = rgb(99, 35, 35);
        style.close_button_style.outline = rgb(255, 225, 225);
        style.fullscreen_button_style.fill = rgb(214, 168, 64);
        style.fullscreen_button_style.hover_fill = rgb(232, 186, 82);
        style.fullscreen_button_style.pressed_fill = rgb(182, 138, 50);
        style.fullscreen_button_style.outline = rgb(254, 246, 222);
        style.title_scale = 2u;

        window_ = kwindow_create(80, 76, 620, 430, 15, font_, "File Explorer", &style);
    }

    void FileExplorer::CreateButtons(void)
    {
        ktextbox_style path_style = ktextbox_style_default();
        kgfx_obj_handle root = kwindow_root(window_);

        path_style.fill = rgb(24, 29, 40);
        path_style.hover_fill = rgb(31, 38, 52);
        path_style.focus_fill = rgb(36, 46, 63);
        path_style.outline = rgb(102, 122, 158);
        path_style.focus_outline = rgb(144, 194, 255);
        path_style.text_color = rgb(236, 239, 245);
        path_style.padding_x = 6u;
        path_style.padding_y = 3u;

        path_box_ = ktextbox_add_rect(0, 0, 100, 24, 2, font_, &path_style, PathSubmitThunk, this);
        kgfx_obj_set_parent(ktextbox_root(path_box_), root);
        kgfx_obj_set_clip_to_parent(ktextbox_root(path_box_), 1);

        list_viewport_ = kgfx_obj_add_rect(0, 0, 100, 100, 1, rgb(20, 23, 30), 1);
        kgfx_obj_set_parent(list_viewport_, root);
        kgfx_obj_set_clip_to_parent(list_viewport_, 1);
        kgfx_obj_ref(list_viewport_)->outline = rgb(59, 70, 94);
        kgfx_obj_ref(list_viewport_)->outline_width = 1u;

        status_strip_ = kgfx_obj_add_rect(0, 0, 100, 24, 1, rgb(21, 26, 37), 1);
        kgfx_obj_set_parent(status_strip_, root);
        kgfx_obj_ref(status_strip_)->outline = rgb(80, 98, 132);
        kgfx_obj_ref(status_strip_)->outline_width = 1u;

        status_text_ = kgfx_obj_add_text(font_, status_buffer_, 8, 4, 1, status_color_, 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1);
        kgfx_obj_set_parent(status_text_, status_strip_);
        kgfx_obj_set_clip_to_parent(status_text_, 1);

        up_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, UpClickThunk, this);
        refresh_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, RefreshClickThunk, this);
        new_folder_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, NewFolderClickThunk, this);
        new_file_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, NewFileClickThunk, this);
        rename_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, RenameClickThunk, this);
        delete_button_.button = kbutton_add_rect(0, 0, 80, 28, 2, &action_style_, DeleteClickThunk, this);

        up_button_.label = kgfx_obj_add_text(font_, "Up", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        refresh_button_.label = kgfx_obj_add_text(font_, "Refresh", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        new_folder_button_.label = kgfx_obj_add_text(font_, "New Folder", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        new_file_button_.label = kgfx_obj_add_text(font_, "New File", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        rename_button_.label = kgfx_obj_add_text(font_, "Rename", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        delete_button_.label = kgfx_obj_add_text(font_, "Delete", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);

        LabeledButton *buttons[] = {
            &up_button_,
            &refresh_button_,
            &new_folder_button_,
            &new_file_button_,
            &rename_button_,
            &delete_button_};

        for (uint32_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            kgfx_obj_set_parent(kbutton_root(buttons[i]->button), root);
            kgfx_obj_set_clip_to_parent(kbutton_root(buttons[i]->button), 1);
            kgfx_obj_set_parent(buttons[i]->label, kbutton_root(buttons[i]->button));
            kgfx_obj_set_clip_to_parent(buttons[i]->label, 1);
        }
    }

    void FileExplorer::CreateRows(void)
    {
        for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
        {
            rows_[i].button = kbutton_add_rect(0, 0, 100, 24, 2, &row_style_, RowClickThunk, &rows_[i]);
            rows_[i].icon = kgfx_obj_add_text(font_, kFolderIcon, 10, 4, 1, rgb(248, 205, 90), 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1);
            rows_[i].name = kgfx_obj_add_text(font_, "", 36, 4, 1, rgb(236, 239, 245), 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 1);
            rows_[i].entry_index = -1;

            kgfx_obj_set_parent(kbutton_root(rows_[i].button), list_viewport_);
            kgfx_obj_set_clip_to_parent(kbutton_root(rows_[i].button), 1);
            kgfx_obj_set_parent(rows_[i].icon, kbutton_root(rows_[i].button));
            kgfx_obj_set_parent(rows_[i].name, kbutton_root(rows_[i].button));
            kgfx_obj_set_clip_to_parent(rows_[i].icon, 1);
            kgfx_obj_set_clip_to_parent(rows_[i].name, 1);

            SetRowVisible(rows_[i], 0u);
            kbutton_set_enabled(rows_[i].button, 0u);
        }
    }

    void FileExplorer::CreateModal(void)
    {
        ktextbox_style modal_text_style = ktextbox_style_default();
        kgfx_obj_handle root = kwindow_root(window_);

        modal_backdrop_ = kgfx_obj_add_rect(0, 0, 100, 100, 30, rgb(0, 0, 0), 0);
        kgfx_obj_ref(modal_backdrop_)->alpha = 140u;
        kgfx_obj_set_parent(modal_backdrop_, root);
        kgfx_obj_set_clip_to_parent(modal_backdrop_, 1);

        modal_panel_ = kgfx_obj_add_rect(0, 0, 100, 100, 31, rgb(24, 29, 40), 0);
        kgfx_obj_ref(modal_panel_)->outline = rgb(128, 154, 204);
        kgfx_obj_ref(modal_panel_)->outline_width = 2u;
        kgfx_obj_set_parent(modal_panel_, root);
        kgfx_obj_set_clip_to_parent(modal_panel_, 1);

        modal_title_ = kgfx_obj_add_text(font_, modal_title_buffer_, 14, 12, 1, rgb(240, 244, 250), 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 0);
        modal_body_ = kgfx_obj_add_text(font_, modal_body_buffer_, 14, 40, 1, rgb(213, 220, 232), 255, 1u, 0, 4, KTEXT_ALIGN_LEFT, 0);
        modal_error_ = kgfx_obj_add_text(font_, modal_error_buffer_, 14, 92, 1, rgb(255, 140, 140), 255, 1u, 0, 0, KTEXT_ALIGN_LEFT, 0);

        kgfx_obj_set_parent(modal_title_, modal_panel_);
        kgfx_obj_set_parent(modal_body_, modal_panel_);
        kgfx_obj_set_parent(modal_error_, modal_panel_);
        kgfx_obj_set_clip_to_parent(modal_title_, 1);
        kgfx_obj_set_clip_to_parent(modal_body_, 1);
        kgfx_obj_set_clip_to_parent(modal_error_, 1);

        modal_text_style.fill = rgb(18, 23, 31);
        modal_text_style.hover_fill = rgb(26, 32, 43);
        modal_text_style.focus_fill = rgb(34, 43, 58);
        modal_text_style.outline = rgb(112, 132, 171);
        modal_text_style.focus_outline = rgb(153, 204, 255);
        modal_text_style.text_color = rgb(240, 244, 248);
        modal_text_style.padding_x = 6u;
        modal_text_style.padding_y = 3u;

        modal_input_ = ktextbox_add_rect(0, 0, 160, 24, 32, font_, &modal_text_style, ModalSubmitThunk, this);
        kgfx_obj_set_parent(ktextbox_root(modal_input_), modal_panel_);
        kgfx_obj_set_clip_to_parent(ktextbox_root(modal_input_), 1);

        modal_confirm_button_.button = kbutton_add_rect(0, 0, 84, 28, 32, &modal_confirm_style_, ModalConfirmClickThunk, this);
        modal_cancel_button_.button = kbutton_add_rect(0, 0, 84, 28, 32, &action_style_, ModalCancelClickThunk, this);
        modal_confirm_button_.label = kgfx_obj_add_text(font_, modal_confirm_label_, 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
        modal_cancel_button_.label = kgfx_obj_add_text(font_, "Cancel", 0, 0, 1, white, 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);

        kgfx_obj_set_parent(kbutton_root(modal_confirm_button_.button), modal_panel_);
        kgfx_obj_set_parent(kbutton_root(modal_cancel_button_.button), modal_panel_);
        kgfx_obj_set_clip_to_parent(kbutton_root(modal_confirm_button_.button), 1);
        kgfx_obj_set_clip_to_parent(kbutton_root(modal_cancel_button_.button), 1);
        kgfx_obj_set_parent(modal_confirm_button_.label, kbutton_root(modal_confirm_button_.button));
        kgfx_obj_set_parent(modal_cancel_button_.label, kbutton_root(modal_cancel_button_.button));
        kgfx_obj_set_clip_to_parent(modal_confirm_button_.label, 1);
        kgfx_obj_set_clip_to_parent(modal_cancel_button_.label, 1);

        SetObjectVisible(modal_backdrop_, 0u);
        SetObjectVisible(modal_panel_, 0u);
        SetObjectVisible(modal_title_, 0u);
        SetObjectVisible(modal_body_, 0u);
        SetObjectVisible(modal_error_, 0u);
        SetTextboxVisible(modal_input_, 0u);
        SetButtonVisible(modal_confirm_button_, 0u);
        SetButtonVisible(modal_cancel_button_, 0u);
        kbutton_set_enabled(modal_confirm_button_.button, 0u);
        kbutton_set_enabled(modal_cancel_button_.button, 0u);
        ktextbox_set_enabled(modal_input_, 0u);
    }

    int FileExplorer::RootVisible(void) const
    {
        if (window_.idx < 0)
            return 0;
        return kwindow_visible(window_);
    }

    int FileExplorer::Visible() const
    {
        return RootVisible();
    }

    kgfx_obj *FileExplorer::RootObject(void)
    {
        if (window_.idx < 0)
            return 0;
        return kgfx_obj_ref(kwindow_root(window_));
    }

    int FileExplorer::VisibleRowCapacity(void) const
    {
        return visible_rows_;
    }

    int FileExplorer::MaxScrollTop(void) const
    {
        int visible = VisibleRowCapacity();

        if (visible <= 0 || entry_count_ <= visible)
            return 0;
        return entry_count_ - visible;
    }

    void FileExplorer::ClampScroll(void)
    {
        if (scroll_top_ < 0)
            scroll_top_ = 0;
        if (scroll_top_ > MaxScrollTop())
            scroll_top_ = MaxScrollTop();
    }

    void FileExplorer::EnsureSelectionVisible(void)
    {
        int visible = VisibleRowCapacity();

        if (selected_index_ < 0 || visible <= 0)
            return;

        if (selected_index_ < scroll_top_)
            scroll_top_ = selected_index_;
        else if (selected_index_ >= scroll_top_ + visible)
            scroll_top_ = selected_index_ - visible + 1;

        ClampScroll();
    }

    void FileExplorer::LayoutButton(LabeledButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t scale)
    {
        kgfx_obj *root = kgfx_obj_ref(kbutton_root(button.button));
        kgfx_obj *label = kgfx_obj_ref(button.label);
        uint32_t text_h = text_height_px(font_, scale);
        int32_t text_y = 0;

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        root->u.rect.x = x;
        root->u.rect.y = y;
        root->u.rect.w = w;
        root->u.rect.h = h;

        if (!label || label->kind != KGFX_OBJ_TEXT)
            return;

        label->u.text.scale = scale;
        label->u.text.x = (int32_t)w / 2;
        text_y = (int32_t)((h > text_h) ? (h - text_h) / 2u : 0u);
        label->u.text.y = text_y;
    }

    void FileExplorer::LayoutRow(RowSlot &row, int32_t y, uint32_t w, uint32_t h)
    {
        kgfx_obj *root = kgfx_obj_ref(kbutton_root(row.button));
        kgfx_obj *icon = kgfx_obj_ref(row.icon);
        kgfx_obj *name = kgfx_obj_ref(row.name);
        uint32_t text_h = text_height_px(font_, 1u);
        int32_t text_y = (int32_t)((h > text_h) ? (h - text_h) / 2u : 0u);

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        root->u.rect.x = 0;
        root->u.rect.y = y;
        root->u.rect.w = w;
        root->u.rect.h = h;

        if (icon && icon->kind == KGFX_OBJ_TEXT)
        {
            icon->u.text.x = 10;
            icon->u.text.y = text_y;
        }

        if (name && name->kind == KGFX_OBJ_TEXT)
        {
            name->u.text.x = 42;
            name->u.text.y = text_y;
        }
    }

    void FileExplorer::Layout(void)
    {
        kgfx_obj *root = RootObject();
        int32_t pad = 12;
        uint32_t text_h = text_height_px(font_, 1u);
        uint32_t path_h = text_h + 12u;
        uint32_t action_h = text_h + 14u;
        uint32_t status_h = text_h + 12u;
        uint32_t row_h = text_h + 10u;
        int32_t client_x = 0;
        int32_t path_y = 0;
        int32_t actions_y = 0;
        int32_t list_y = 0;
        int32_t status_y = 0;
        int32_t list_h = 0;
        uint32_t client_w = 0u;
        uint32_t button_gap = 8u;
        uint32_t button_count = 6u;
        uint32_t button_w = 1u;
        uint32_t used_w = 0u;
        uint32_t visible_rows = 0u;

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        if (root->u.rect.w < 2u * (uint32_t)pad)
            return;

        client_x = pad;
        client_w = root->u.rect.w - 2u * (uint32_t)pad;
        path_y = (int32_t)42 + pad;
        actions_y = path_y + (int32_t)path_h + 8;
        status_y = (int32_t)root->u.rect.h - pad - (int32_t)status_h;
        list_y = actions_y + (int32_t)action_h + 10;
        list_h = status_y - 8 - list_y;

        if (list_h < (int32_t)row_h)
            list_h = (int32_t)row_h;

        ktextbox_set_bounds(path_box_, client_x, path_y, client_w, path_h);

        used_w = client_w - button_gap * (button_count - 1u);
        button_w = (button_count > 0u) ? (used_w / button_count) : client_w;
        if (button_w == 0u)
            button_w = 1u;

        LayoutButton(up_button_, client_x, actions_y, button_w, action_h, 1u);
        LayoutButton(refresh_button_, client_x + (int32_t)(button_w + button_gap) * 1, actions_y, button_w, action_h, 1u);
        LayoutButton(new_folder_button_, client_x + (int32_t)(button_w + button_gap) * 2, actions_y, button_w, action_h, 1u);
        LayoutButton(new_file_button_, client_x + (int32_t)(button_w + button_gap) * 3, actions_y, button_w, action_h, 1u);
        LayoutButton(rename_button_, client_x + (int32_t)(button_w + button_gap) * 4, actions_y, button_w, action_h, 1u);
        LayoutButton(delete_button_, client_x + (int32_t)(button_w + button_gap) * 5, actions_y, button_w, action_h, 1u);

        if (kgfx_obj_ref(list_viewport_) && kgfx_obj_ref(list_viewport_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(list_viewport_)->u.rect.x = client_x;
            kgfx_obj_ref(list_viewport_)->u.rect.y = list_y;
            kgfx_obj_ref(list_viewport_)->u.rect.w = client_w;
            kgfx_obj_ref(list_viewport_)->u.rect.h = (uint32_t)list_h;
        }

        if (kgfx_obj_ref(status_strip_) && kgfx_obj_ref(status_strip_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(status_strip_)->u.rect.x = client_x;
            kgfx_obj_ref(status_strip_)->u.rect.y = status_y;
            kgfx_obj_ref(status_strip_)->u.rect.w = client_w;
            kgfx_obj_ref(status_strip_)->u.rect.h = status_h;
        }

        if (kgfx_obj_ref(status_text_) && kgfx_obj_ref(status_text_)->kind == KGFX_OBJ_TEXT)
        {
            kgfx_obj_ref(status_text_)->u.text.x = 8;
            kgfx_obj_ref(status_text_)->u.text.y = (int32_t)((status_h > text_h) ? (status_h - text_h) / 2u : 0u);
        }

        visible_rows = (uint32_t)(list_h / (int32_t)row_h);
        if ((list_h % (int32_t)row_h) != 0 || visible_rows == 0u)
            ++visible_rows;
        if (visible_rows > MAX_VISIBLE_ROWS)
            visible_rows = MAX_VISIBLE_ROWS;
        visible_rows_ = (int)visible_rows;

        for (uint32_t i = 0; i < MAX_VISIBLE_ROWS; ++i)
            LayoutRow(rows_[i], (int32_t)(i * row_h), client_w, row_h > 2u ? row_h - 2u : row_h);

        if (modal_mode_ != MODAL_NONE)
        {
            uint32_t modal_w = client_w > 460u ? 460u : client_w;
            uint32_t modal_h = (modal_mode_ == MODAL_DELETE_CONFIRM) ? 162u : 198u;
            int32_t modal_x = ((int32_t)root->u.rect.w - (int32_t)modal_w) / 2;
            int32_t modal_y = ((int32_t)root->u.rect.h - (int32_t)modal_h) / 2;

            if (kgfx_obj_ref(modal_backdrop_) && kgfx_obj_ref(modal_backdrop_)->kind == KGFX_OBJ_RECT)
            {
                kgfx_obj_ref(modal_backdrop_)->u.rect.x = 0;
                kgfx_obj_ref(modal_backdrop_)->u.rect.y = 0;
                kgfx_obj_ref(modal_backdrop_)->u.rect.w = root->u.rect.w;
                kgfx_obj_ref(modal_backdrop_)->u.rect.h = root->u.rect.h;
            }

            if (kgfx_obj_ref(modal_panel_) && kgfx_obj_ref(modal_panel_)->kind == KGFX_OBJ_RECT)
            {
                kgfx_obj_ref(modal_panel_)->u.rect.x = modal_x;
                kgfx_obj_ref(modal_panel_)->u.rect.y = modal_y;
                kgfx_obj_ref(modal_panel_)->u.rect.w = modal_w;
                kgfx_obj_ref(modal_panel_)->u.rect.h = modal_h;
            }

            if (kgfx_obj_ref(modal_title_) && kgfx_obj_ref(modal_title_)->kind == KGFX_OBJ_TEXT)
            {
                kgfx_obj_ref(modal_title_)->u.text.x = 14;
                kgfx_obj_ref(modal_title_)->u.text.y = 12;
            }

            if (kgfx_obj_ref(modal_body_) && kgfx_obj_ref(modal_body_)->kind == KGFX_OBJ_TEXT)
            {
                kgfx_obj_ref(modal_body_)->u.text.x = 14;
                kgfx_obj_ref(modal_body_)->u.text.y = 40;
            }

            if (modal_mode_ != MODAL_DELETE_CONFIRM)
                ktextbox_set_bounds(modal_input_, 14, 92, modal_w - 28u, text_h + 12u);

            if (kgfx_obj_ref(modal_error_) && kgfx_obj_ref(modal_error_)->kind == KGFX_OBJ_TEXT)
            {
                kgfx_obj_ref(modal_error_)->u.text.x = 14;
                kgfx_obj_ref(modal_error_)->u.text.y = (modal_mode_ == MODAL_DELETE_CONFIRM) ? 94 : 128;
            }

            LayoutButton(modal_confirm_button_, (int32_t)modal_w - 14 - 92 - 8 - 92, (int32_t)modal_h - 14 - (int32_t)action_h, 92u, action_h, 1u);
            LayoutButton(modal_cancel_button_, (int32_t)modal_w - 14 - 92, (int32_t)modal_h - 14 - (int32_t)action_h, 92u, action_h, 1u);
        }

        ClampScroll();
    }

    void FileExplorer::SetObjectVisible(kgfx_obj_handle handle, uint8_t visible)
    {
        kgfx_obj *obj = kgfx_obj_ref(handle);
        if (obj)
            obj->visible = visible ? 1u : 0u;
    }

    void FileExplorer::SetButtonVisible(LabeledButton &button, uint8_t visible)
    {
        SetObjectVisible(kbutton_root(button.button), visible);
    }

    void FileExplorer::SetTextboxVisible(ktextbox_handle handle, uint8_t visible)
    {
        SetObjectVisible(ktextbox_root(handle), visible);
    }

    void FileExplorer::SetRowVisible(RowSlot &row, uint8_t visible)
    {
        SetObjectVisible(kbutton_root(row.button), visible);
    }

    void FileExplorer::RefreshStatusVisual(void)
    {
        kgfx_obj *status = kgfx_obj_ref(status_text_);

        if (!status || status->kind != KGFX_OBJ_TEXT)
            return;

        status->fill = status_color_;
        status->u.text.text = status_buffer_;
    }

    void FileExplorer::SetStatus(const char *text, kcolor color)
    {
        copy_text(status_buffer_, sizeof(status_buffer_), text);
        status_color_ = color;
        RefreshStatusVisual();
    }

    int FileExplorer::BuildSelectedRawPath(char *out, uint32_t cap) const
    {
        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return -1;
        return dihos_path_join_raw(current_raw_, entries_[selected_index_].name, out, cap);
    }

    int FileExplorer::BuildSelectedFriendlyPath(char *out, uint32_t cap) const
    {
        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return -1;
        return dihos_path_join_friendly(current_dir_, entries_[selected_index_].name, out, cap);
    }

    void FileExplorer::ShowDefaultStatus(void)
    {
        char message[STATUS_CAP];

        message[0] = 0;

        if (truncated_)
        {
            append_text(message, sizeof(message), "showing first ");
            append_uint(message, sizeof(message), MAX_ENTRIES);
            append_text(message, sizeof(message), " items in ");
            append_text(message, sizeof(message), current_dir_);
            SetStatus(message, rgb(255, 214, 120));
            return;
        }

        if (selected_index_ >= 0 && selected_index_ < entry_count_)
        {
            char friendly[DIHOS_PATH_CAP];

            if (BuildSelectedFriendlyPath(friendly, sizeof(friendly)) == 0)
                append_text(message, sizeof(message), friendly);
            else
                append_text(message, sizeof(message), entries_[selected_index_].name);

            append_text(message, sizeof(message), entries_[selected_index_].is_dir ? " [dir]" : " [file]");
            if (!entries_[selected_index_].is_dir)
            {
                append_text(message, sizeof(message), " ");
                append_uint(message, sizeof(message), entries_[selected_index_].size);
                append_text(message, sizeof(message), " bytes");
            }

            SetStatus(message, rgb(222, 228, 238));
            return;
        }

        append_text(message, sizeof(message), current_dir_);
        append_text(message, sizeof(message), " (");
        append_uint(message, sizeof(message), (uint32_t)entry_count_);
        append_text(message, sizeof(message), entry_count_ == 1 ? " item)" : " items)");
        SetStatus(message, rgb(182, 198, 220));
    }

    void FileExplorer::ResetDoubleClick(void)
    {
        last_click_index_ = -1;
        last_click_frame_ = 0u;
    }

    void FileExplorer::SelectEntry(int idx)
    {
        if (idx < 0 || idx >= entry_count_)
        {
            selected_index_ = -1;
            ShowDefaultStatus();
            return;
        }

        selected_index_ = idx;
        EnsureSelectionVisible();
        ResetDoubleClick();
        ShowDefaultStatus();
    }

    int FileExplorer::PathExists(const char *raw, uint8_t *is_dir_out) const
    {
        KDir dir;
        KFile file;

        if (is_dir_out)
            *is_dir_out = 0u;

        if (!raw || !raw[0])
            return 0;

        if (kdir_open(&dir, raw) == 0)
        {
            kdir_close(&dir);
            if (is_dir_out)
                *is_dir_out = 1u;
            return 1;
        }

        if (kfile_open(&file, raw, KFILE_READ) == 0)
        {
            kfile_close(&file);
            if (is_dir_out)
                *is_dir_out = 0u;
            return 1;
        }

        return 0;
    }

    int FileExplorer::DirectoryIsEmpty(const char *raw) const
    {
        KDir dir;
        kdirent ent;
        int rc = 0;

        if (kdir_open(&dir, raw) != 0)
            return 0;

        rc = kdir_next(&dir, &ent);
        kdir_close(&dir);
        return rc == 0;
    }

    int FileExplorer::ValidateSegmentName(const char *name)
    {
        if (!name || !name[0])
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "name cannot be empty");
            return -1;
        }

        if (strings_equal(name, ".") || strings_equal(name, ".."))
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "'.' and '..' are not allowed");
            return -1;
        }

        for (uint32_t i = 0; name[i]; ++i)
        {
            if (name[i] == '/' || name[i] == '\\' || name[i] == ':')
            {
                copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "use a single name without /, \\, or :");
                return -1;
            }
        }

        modal_error_buffer_[0] = 0;
        return 0;
    }

    void FileExplorer::SortEntries(void)
    {
        for (int i = 1; i < entry_count_; ++i)
        {
            Entry key = entries_[i];
            int j = i - 1;

            while (j >= 0)
            {
                int should_move = 0;

                if (entries_[j].is_dir != key.is_dir)
                    should_move = entries_[j].is_dir < key.is_dir;
                else if (strcmp(entries_[j].name, key.name) > 0)
                    should_move = 1;

                if (!should_move)
                    break;

                entries_[j + 1] = entries_[j];
                --j;
            }

            entries_[j + 1] = key;
        }
    }

    int FileExplorer::ReloadDirectory(const char *preferred_name)
    {
        KDir dir;
        kdirent ent;
        int rc = 0;
        char selected_name[256];
        uint8_t want_selected_name = 0u;

        if (preferred_name && preferred_name[0])
        {
            copy_text(selected_name, sizeof(selected_name), preferred_name);
            want_selected_name = 1u;
        }
        else if (selected_index_ >= 0 && selected_index_ < entry_count_)
        {
            copy_text(selected_name, sizeof(selected_name), entries_[selected_index_].name);
            want_selected_name = 1u;
        }
        else
        {
            selected_name[0] = 0;
        }

        entry_count_ = 0;
        selected_index_ = -1;
        scroll_top_ = 0;
        truncated_ = 0u;

        if (kdir_open(&dir, current_raw_) != 0)
            return -1;

        for (;;)
        {
            rc = kdir_next(&dir, &ent);
            if (rc <= 0)
                break;

            if (entry_count_ >= MAX_ENTRIES)
            {
                truncated_ = 1u;
                break;
            }

            copy_text(entries_[entry_count_].name, sizeof(entries_[entry_count_].name), ent.name);
            entries_[entry_count_].is_dir = ent.is_dir;
            entries_[entry_count_].size = ent.size;
            ++entry_count_;
        }

        kdir_close(&dir);

        if (rc < 0)
            return -1;

        SortEntries();

        if (want_selected_name)
        {
            for (int i = 0; i < entry_count_; ++i)
            {
                if (strings_equal(entries_[i].name, selected_name))
                {
                    selected_index_ = i;
                    break;
                }
            }
        }

        EnsureSelectionVisible();
        ResetDoubleClick();
        return 0;
    }

    int FileExplorer::NavigateTo(const char *input)
    {
        char input_copy[DIHOS_PATH_CAP];
        char friendly[DIHOS_PATH_CAP];
        char raw[DIHOS_PATH_CAP];
        uint8_t is_dir = 0u;

        copy_text(input_copy, sizeof(input_copy), input);

        if (dihos_path_resolve_raw(current_dir_, input_copy, friendly, sizeof(friendly), raw, sizeof(raw)) != 0)
        {
            SetStatus("invalid path", rgb(255, 140, 140));
            ktextbox_set_text(path_box_, current_dir_);
            return -1;
        }

        if (!PathExists(raw, &is_dir))
        {
            SetStatus("path not found", rgb(255, 140, 140));
            ktextbox_set_text(path_box_, current_dir_);
            return -1;
        }

        if (!is_dir)
        {
            SetStatus("that path is not a directory", rgb(255, 170, 120));
            ktextbox_set_text(path_box_, current_dir_);
            return -1;
        }

        copy_text(current_dir_, sizeof(current_dir_), friendly);
        copy_text(current_raw_, sizeof(current_raw_), raw);
        ktextbox_set_text(path_box_, current_dir_);

        if (ReloadDirectory(0) != 0)
        {
            SetStatus("unable to open directory", rgb(255, 140, 140));
            return -1;
        }

        ShowDefaultStatus();
        return 0;
    }

    void FileExplorer::RefreshDirectory(void)
    {
        if (ReloadDirectory(0) != 0)
        {
            SetStatus("unable to refresh directory", rgb(255, 140, 140));
            return;
        }

        ShowDefaultStatus();
    }

    void FileExplorer::SyncActionStates(void)
    {
        uint8_t modal_active = modal_mode_ != MODAL_NONE;
        uint8_t has_selection = (selected_index_ >= 0 && selected_index_ < entry_count_) ? 1u : 0u;
        uint8_t is_root = strings_equal(current_dir_, "/") ? 1u : 0u;

        ktextbox_set_enabled(path_box_, modal_active ? 0u : 1u);
        kbutton_set_enabled(up_button_.button, (!modal_active && !is_root) ? 1u : 0u);
        kbutton_set_enabled(refresh_button_.button, modal_active ? 0u : 1u);
        kbutton_set_enabled(new_folder_button_.button, modal_active ? 0u : 1u);
        kbutton_set_enabled(new_file_button_.button, modal_active ? 0u : 1u);
        kbutton_set_enabled(rename_button_.button, (!modal_active && has_selection) ? 1u : 0u);
        kbutton_set_enabled(delete_button_.button, (!modal_active && has_selection) ? 1u : 0u);

        for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
        {
            if (rows_[i].entry_index >= 0 && i < visible_rows_)
                kbutton_set_enabled(rows_[i].button, modal_active ? 0u : 1u);
            else
                kbutton_set_enabled(rows_[i].button, 0u);
        }

        if (modal_active)
        {
            SetObjectVisible(modal_backdrop_, 1u);
            SetObjectVisible(modal_panel_, 1u);
            SetObjectVisible(modal_title_, 1u);
            SetObjectVisible(modal_body_, 1u);
            SetObjectVisible(modal_error_, modal_error_buffer_[0] ? 1u : 0u);
            SetButtonVisible(modal_confirm_button_, 1u);
            SetButtonVisible(modal_cancel_button_, 1u);
            kbutton_set_enabled(modal_confirm_button_.button, 1u);
            kbutton_set_enabled(modal_cancel_button_.button, 1u);

            if (modal_mode_ == MODAL_DELETE_CONFIRM)
            {
                SetTextboxVisible(modal_input_, 0u);
                ktextbox_set_enabled(modal_input_, 0u);
            }
            else
            {
                SetTextboxVisible(modal_input_, 1u);
                ktextbox_set_enabled(modal_input_, 1u);
            }
        }
        else
        {
            SetObjectVisible(modal_backdrop_, 0u);
            SetObjectVisible(modal_panel_, 0u);
            SetObjectVisible(modal_title_, 0u);
            SetObjectVisible(modal_body_, 0u);
            SetObjectVisible(modal_error_, 0u);
            SetTextboxVisible(modal_input_, 0u);
            SetButtonVisible(modal_confirm_button_, 0u);
            SetButtonVisible(modal_cancel_button_, 0u);
            ktextbox_set_enabled(modal_input_, 0u);
            kbutton_set_enabled(modal_confirm_button_.button, 0u);
            kbutton_set_enabled(modal_cancel_button_.button, 0u);
        }
    }

    void FileExplorer::SyncRows(void)
    {
        for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
        {
            int entry_index = scroll_top_ + i;
            kgfx_obj *icon = kgfx_obj_ref(rows_[i].icon);
            kgfx_obj *name = kgfx_obj_ref(rows_[i].name);

            if (i >= visible_rows_ || entry_index >= entry_count_)
            {
                rows_[i].entry_index = -1;
                SetRowVisible(rows_[i], 0u);
                continue;
            }

            rows_[i].entry_index = entry_index;
            SetRowVisible(rows_[i], 1u);

            if (entries_[entry_index].is_dir)
            {
                if (icon && icon->kind == KGFX_OBJ_TEXT)
                {
                    icon->u.text.text = kFolderIcon;
                    icon->fill = rgb(248, 205, 90);
                }
            }
            else
            {
                if (icon && icon->kind == KGFX_OBJ_TEXT)
                {
                    icon->u.text.text = kFileIcon;
                    icon->fill = rgb(149, 211, 255);
                }
            }

            if (name && name->kind == KGFX_OBJ_TEXT)
                name->u.text.text = entries_[entry_index].name;

            kbutton_set_style(rows_[i].button, (entry_index == selected_index_) ? &row_selected_style_ : &row_style_);
        }
    }

    void FileExplorer::HandleMouseWheel(void)
    {
        kgfx_obj *viewport = kgfx_obj_ref(list_viewport_);
        kgfx_obj *root = RootObject();
        kmouse_state mouse;
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;

        if (!viewport || viewport->kind != KGFX_OBJ_RECT || !root || root->kind != KGFX_OBJ_RECT)
            return;

        if (modal_mode_ != MODAL_NONE)
            return;

        kmouse_get_state(&mouse);
        if (mouse.wheel == 0)
            return;

        left = root->u.rect.x + viewport->u.rect.x;
        top = root->u.rect.y + viewport->u.rect.y;
        right = left + (int32_t)viewport->u.rect.w;
        bottom = top + (int32_t)viewport->u.rect.h;

        if (mouse.x < left || mouse.y < top || mouse.x >= right || mouse.y >= bottom)
            return;

        scroll_top_ -= mouse.wheel * SCROLL_LINES_PER_WHEEL;
        ClampScroll();
    }

    void FileExplorer::ActivateSelection(void)
    {
        char friendly[DIHOS_PATH_CAP];

        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return;

        if (entries_[selected_index_].is_dir)
        {
            if (BuildSelectedFriendlyPath(friendly, sizeof(friendly)) == 0)
                (void)NavigateTo(friendly);
            return;
        }

        SetStatus("no app registered for files yet", rgb(255, 214, 120));
    }

    void FileExplorer::OpenModal(ModalMode mode, const char *title, const char *body, const char *confirm_text, const char *initial_text)
    {
        modal_mode_ = mode;
        copy_text(modal_title_buffer_, sizeof(modal_title_buffer_), title);
        copy_text(modal_body_buffer_, sizeof(modal_body_buffer_), body);
        copy_text(modal_confirm_label_, sizeof(modal_confirm_label_), confirm_text);
        modal_error_buffer_[0] = 0;
        RefreshStatusVisual();

        if (kgfx_obj_ref(modal_title_) && kgfx_obj_ref(modal_title_)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_title_)->u.text.text = modal_title_buffer_;
        if (kgfx_obj_ref(modal_body_) && kgfx_obj_ref(modal_body_)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_body_)->u.text.text = modal_body_buffer_;
        if (kgfx_obj_ref(modal_error_) && kgfx_obj_ref(modal_error_)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_error_)->u.text.text = modal_error_buffer_;
        if (kgfx_obj_ref(modal_confirm_button_.label) && kgfx_obj_ref(modal_confirm_button_.label)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_confirm_button_.label)->u.text.text = modal_confirm_label_;

        if (mode == MODAL_DELETE_CONFIRM)
            kbutton_set_style(modal_confirm_button_.button, &modal_delete_style_);
        else
            kbutton_set_style(modal_confirm_button_.button, &modal_confirm_style_);

        if (initial_text)
            ktextbox_set_text(modal_input_, initial_text);
        else
            ktextbox_set_text(modal_input_, "");

        Layout();
        SyncActionStates();

        if (mode != MODAL_DELETE_CONFIRM)
            ktextbox_set_focus(modal_input_, 1u);
    }

    void FileExplorer::CloseModal(void)
    {
        modal_mode_ = MODAL_NONE;
        modal_error_buffer_[0] = 0;
        ktextbox_set_text(modal_input_, "");
        SyncActionStates();
        ktextbox_set_focus(path_box_, 1u);
    }

    void FileExplorer::OpenCreateFolderModal(void)
    {
        OpenModal(MODAL_CREATE_FOLDER,
                  "Create Folder",
                  "Enter a single folder name.",
                  "Create",
                  "");
    }

    void FileExplorer::OpenCreateFileModal(void)
    {
        OpenModal(MODAL_CREATE_FILE,
                  "Create File",
                  "Enter a file name. Extensions are allowed.",
                  "Create",
                  "");
    }

    void FileExplorer::OpenRenameModal(void)
    {
        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return;

        OpenModal(MODAL_RENAME,
                  "Rename Entry",
                  "Enter the new name for the selected item.",
                  "Rename",
                  entries_[selected_index_].name);
    }

    void FileExplorer::OpenDeleteModal(void)
    {
        char body[MODAL_TEXT_CAP];

        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return;

        body[0] = 0;
        append_text(body, sizeof(body), "Delete ");
        append_text(body, sizeof(body), entries_[selected_index_].name);
        append_text(body, sizeof(body), "?\nThis cannot be undone.");

        OpenModal(MODAL_DELETE_CONFIRM, "Delete Entry", body, "Delete", 0);
    }

    void FileExplorer::ApplyCreateFolder(void)
    {
        char raw[DIHOS_PATH_CAP];
        char name[256];
        uint8_t exists_dir = 0u;

        copy_text(name, sizeof(name), ktextbox_text(modal_input_));

        if (ValidateSegmentName(name) != 0)
            return;

        if (dihos_path_join_raw(current_raw_, name, raw, sizeof(raw)) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "folder path is too long");
            return;
        }

        if (PathExists(raw, &exists_dir))
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "that name already exists");
            return;
        }

        if (kfile_mkdir(raw) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "unable to create folder");
            return;
        }

        if (ReloadDirectory(name) != 0)
        {
            SetStatus("folder created but refresh failed", rgb(255, 214, 120));
            CloseModal();
            return;
        }

        CloseModal();
        SetStatus("folder created", rgb(148, 232, 180));
    }

    void FileExplorer::ApplyCreateFile(void)
    {
        char raw[DIHOS_PATH_CAP];
        char name[256];
        uint8_t exists_dir = 0u;
        KFile file;

        copy_text(name, sizeof(name), ktextbox_text(modal_input_));

        if (ValidateSegmentName(name) != 0)
            return;

        if (dihos_path_join_raw(current_raw_, name, raw, sizeof(raw)) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "file path is too long");
            return;
        }

        if (PathExists(raw, &exists_dir))
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "that name already exists");
            return;
        }

        if (kfile_open(&file, raw, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "unable to create file");
            return;
        }

        kfile_close(&file);

        if (ReloadDirectory(name) != 0)
        {
            SetStatus("file created but refresh failed", rgb(255, 214, 120));
            CloseModal();
            return;
        }

        CloseModal();
        SetStatus("file created", rgb(148, 232, 180));
    }

    void FileExplorer::ApplyRename(void)
    {
        char old_raw[DIHOS_PATH_CAP];
        char new_raw[DIHOS_PATH_CAP];
        char new_name[256];
        uint8_t exists_dir = 0u;

        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return;

        copy_text(new_name, sizeof(new_name), ktextbox_text(modal_input_));

        if (ValidateSegmentName(new_name) != 0)
            return;

        if (strings_equal(new_name, entries_[selected_index_].name))
        {
            CloseModal();
            ShowDefaultStatus();
            return;
        }

        if (BuildSelectedRawPath(old_raw, sizeof(old_raw)) != 0 ||
            dihos_path_join_raw(current_raw_, new_name, new_raw, sizeof(new_raw)) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "rename path is too long");
            return;
        }

        if (PathExists(new_raw, &exists_dir))
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "that name already exists");
            return;
        }

        if (kfile_rename(old_raw, new_raw) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "unable to rename entry");
            return;
        }

        if (ReloadDirectory(new_name) != 0)
        {
            SetStatus("rename worked but refresh failed", rgb(255, 214, 120));
            CloseModal();
            return;
        }

        CloseModal();
        SetStatus("entry renamed", rgb(148, 232, 180));
    }

    void FileExplorer::ApplyDelete(void)
    {
        char raw[DIHOS_PATH_CAP];

        if (selected_index_ < 0 || selected_index_ >= entry_count_)
            return;

        if (BuildSelectedRawPath(raw, sizeof(raw)) != 0)
        {
            SetStatus("unable to resolve selected path", rgb(255, 140, 140));
            CloseModal();
            return;
        }

        if (entries_[selected_index_].is_dir && !DirectoryIsEmpty(raw))
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "non-empty folders cannot be deleted in v1");
            return;
        }

        if (kfile_unlink(raw) != 0)
        {
            copy_text(modal_error_buffer_, sizeof(modal_error_buffer_), "unable to delete entry");
            return;
        }

        if (ReloadDirectory(0) != 0)
        {
            SetStatus("delete worked but refresh failed", rgb(255, 214, 120));
            CloseModal();
            return;
        }

        CloseModal();
        SetStatus("entry deleted", rgb(148, 232, 180));
    }

    void FileExplorer::CommitModal(void)
    {
        if (kgfx_obj_ref(modal_error_) && kgfx_obj_ref(modal_error_)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_error_)->u.text.text = modal_error_buffer_;

        switch (modal_mode_)
        {
        case MODAL_CREATE_FOLDER:
            ApplyCreateFolder();
            break;
        case MODAL_CREATE_FILE:
            ApplyCreateFile();
            break;
        case MODAL_RENAME:
            ApplyRename();
            break;
        case MODAL_DELETE_CONFIRM:
            ApplyDelete();
            break;
        default:
            break;
        }

        if (modal_mode_ != MODAL_NONE && kgfx_obj_ref(modal_error_) && kgfx_obj_ref(modal_error_)->kind == KGFX_OBJ_TEXT)
            kgfx_obj_ref(modal_error_)->u.text.text = modal_error_buffer_;
    }

    void FileExplorer::Update()
    {
        if (!initialized_)
            return;

        if (!RootVisible())
        {
            if (path_box_.idx >= 0 && ktextbox_focused(path_box_))
                ktextbox_set_focus(path_box_, 0u);
            if (modal_input_.idx >= 0 && ktextbox_focused(modal_input_))
                ktextbox_set_focus(modal_input_, 0u);
            return;
        }

        ++frame_counter_;

        if (modal_mode_ != MODAL_NONE && kinput_key_pressed(KEY_ESCAPE))
            CloseModal();

        Layout();
        HandleMouseWheel();
        SyncActionStates();
        SyncRows();
        RefreshStatusVisual();
    }

    void FileExplorer::Activate()
    {
        if (!initialized_ || window_.idx < 0)
            return;

        kwindow_set_visible(window_, 1u);
        (void)kwindow_raise(window_);
        Layout();
        SyncActionStates();
        SyncRows();

        if (modal_mode_ != MODAL_NONE && modal_mode_ != MODAL_DELETE_CONFIRM && modal_input_.idx >= 0)
            ktextbox_set_focus(modal_input_, 1u);
        else if (path_box_.idx >= 0)
            ktextbox_set_focus(path_box_, 1u);
    }

    void FileExplorer::PathSubmitThunk(ktextbox_handle textbox, const char *text, void *user)
    {
        char input[DIHOS_PATH_CAP];
        FileExplorer *self = (FileExplorer *)user;
        (void)textbox;

        if (!self)
            return;

        copy_text(input, sizeof(input), text);
        (void)self->NavigateTo(input);
    }

    void FileExplorer::ModalSubmitThunk(ktextbox_handle textbox, const char *text, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)textbox;
        (void)text;

        if (!self || self->modal_mode_ == MODAL_DELETE_CONFIRM)
            return;

        self->CommitModal();
    }

    void FileExplorer::RowClickThunk(kbutton_handle button, void *user)
    {
        RowSlot *row = (RowSlot *)user;
        FileExplorer *self = row ? row->owner : 0;
        uint32_t delta = 0u;
        (void)button;

        if (!self || !row || row->entry_index < 0 || row->entry_index >= self->entry_count_)
            return;

        if (self->selected_index_ == row->entry_index &&
            self->last_click_index_ == row->entry_index)
        {
            delta = self->frame_counter_ - self->last_click_frame_;
            if (delta <= DOUBLE_CLICK_FRAMES)
            {
                self->ResetDoubleClick();
                self->ActivateSelection();
                return;
            }
        }

        self->selected_index_ = row->entry_index;
        self->EnsureSelectionVisible();
        self->ShowDefaultStatus();
        self->last_click_index_ = row->entry_index;
        self->last_click_frame_ = self->frame_counter_;
    }

    void FileExplorer::UpClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            (void)self->NavigateTo("..");
    }

    void FileExplorer::RefreshClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->RefreshDirectory();
    }

    void FileExplorer::NewFolderClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->OpenCreateFolderModal();
    }

    void FileExplorer::NewFileClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->OpenCreateFileModal();
    }

    void FileExplorer::RenameClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->OpenRenameModal();
    }

    void FileExplorer::DeleteClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->OpenDeleteModal();
    }

    void FileExplorer::ModalConfirmClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->CommitModal();
    }

    void FileExplorer::ModalCancelClickThunk(kbutton_handle button, void *user)
    {
        FileExplorer *self = (FileExplorer *)user;
        (void)button;
        if (self)
            self->CloseModal();
    }
}

extern "C" void file_explorer_init(const kfont *font)
{
    g_explorer.Init(font);
}

extern "C" void file_explorer_update(void)
{
    g_explorer.Update();
}

extern "C" void file_explorer_activate(void)
{
    g_explorer.Activate();
}

extern "C" int file_explorer_visible(void)
{
    return g_explorer.Visible();
}
