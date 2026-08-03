#include "apps/task_manager_api.h"
#include "apps/file_explorer_api.h"
#include "apps/text_editor_api.h"
#include "apps/sacx_runtime.h"
#include "terminal/terminal_api.h"

extern "C"
{
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/ktext.h"
#include "kwrappers/kui.h"
#include "kwrappers/kwindow.h"
#include "memory/pmem.h"
#include "system/cpu_info.h"
#include "system/device_inventory.h"
#include "system/kwork.h"
#include "system/smp.h"
#include "system/task_accounting.h"
}

namespace
{
    enum
    {
        VIEW_APPS = 0,
        VIEW_PERF = 1,
        VIEW_DEVICES = 2,
        MAX_APP_ROWS = 20,
        MAX_DEVICE_ROWS = 18,
        PERF_LINE_COUNT = 12,
        PERF_TAB_COUNT = 5,
        PERF_HISTORY_COUNT = 48,
        PERF_GRAPH_BARS = 48,
        PERF_GRID_LINES = 10,
        PERF_COMPOSITION_SEGMENTS = 3,
        PERF_DETAIL_COUNT = 16,
        TEXT_CAP = 128
    };

    enum row_kind
    {
        ROW_NONE = 0,
        ROW_SACX,
        ROW_KERNEL,
        ROW_PROTECTED
    };

    enum action_kind
    {
        ACTION_FOCUS = 1,
        ACTION_TERMINATE = 2,
        ACTION_RELEASE = 3,
        ACTION_CONFIRM_TERMINATE = 4,
        ACTION_CANCEL_MODAL = 5,
        ACTION_PERF_SELECT = 6
    };

    enum perf_tab
    {
        PERF_CPU = 0,
        PERF_MEMORY = 1,
        PERF_STORAGE = 2,
        PERF_NETWORK = 3,
        PERF_ACTIVITY = 4
    };

    struct Label
    {
        kgfx_obj_handle obj;
        char text[TEXT_CAP];
    };

    struct Button
    {
        kbutton_handle button;
        kgfx_obj_handle label;
        char text[32];
        uint32_t action;
        uint32_t row;
    };

    struct AppRow
    {
        kgfx_obj_handle root;
        Label name;
        Label kind;
        Label state;
        Label memory;
        Label activity;
        Button focus;
        Button action;
        row_kind row_type;
        uint32_t task_id;
        uint32_t kernel_id;
        uint32_t visible;
        uint32_t protected_entry;
    };

    struct PerfLine
    {
        Label name;
        Label value;
        kgfx_obj_handle bar;
    };

    struct DeviceRow
    {
        kgfx_obj_handle root;
        Label group;
        Label name;
        Label status;
        Label detail;
    };

    class TaskManager
    {
    public:
        void Init(const kfont *font);
        void Update();
        void Activate();
        int Visible() const;

    private:
        void CreateWindow();
        void CreateViews();
        void CreateRows();
        void CreateModal();
        void Layout();
        void Refresh();
        void RefreshApps();
        void RefreshPerformance();
        void RefreshDevices();
        void RefreshPerformanceLayoutText(uint64_t total_bytes, uint64_t used_bytes, uint64_t free_bytes,
                                          const pmem_stats &stats, const task_accounting_snapshot &accounting,
                                          uint64_t total_ticks);
        void PushPerfHistory(uint32_t mem_value, uint32_t cpu_pct, uint32_t activity_pct);
        void DrawPerfGraph(uint32_t *history, uint32_t color_kind, uint32_t ceiling);
        void SetText(Label &label, const char *text);
        void SetTextEllipsized(Label &label, const char *text, uint32_t max_px);
        void SetTextWrapped(Label &label, const char *text, uint32_t max_px, uint32_t max_lines);
        void SetButtonText(Button &button, const char *text);
        void SetObjectVisible(kgfx_obj_handle obj, uint32_t visible);
        void SetRowVisible(AppRow &row, uint32_t visible);
        void SetDeviceRowVisible(DeviceRow &row, uint32_t visible);
        Label AddLabel(kgfx_obj_handle parent, const char *text, int32_t x, int32_t y, kcolor color, uint32_t scale);
        Button AddButton(kgfx_obj_handle parent, const char *text, uint32_t action, uint32_t row);
        void PositionButton(Button &button, int32_t x, int32_t y, uint32_t w, uint32_t h);
        void ShowKernelWarning(uint32_t row_index);
        void ShowProtectedWarning(const char *name);
        void HideModal();
        void PerformKernelTerminate(uint32_t kernel_id);
        void FocusKernel(uint32_t kernel_id);
        const char *SacxStateName(uint32_t state) const;
        void FormatBytes(uint64_t bytes, char *out, uint32_t cap) const;
        void FormatPercent(uint64_t value, uint64_t total, char *out, uint32_t cap) const;
        void FormatUInt(uint32_t value, char *out, uint32_t cap) const;
        void FormatHex64(uint64_t value, char *out, uint32_t cap) const;
        const char *CpuImplementerName(uint32_t implementer) const;
        void FormatMidrSummary(uint64_t midr, char *out, uint32_t cap) const;
        void FormatMpidrAffinity(uint64_t mpidr, char *out, uint32_t cap) const;
        void FormatCoreSummary(const cpu_core_info &core, char *out, uint32_t cap) const;
        void FormatSmpCoreSummary(const smp_core_status &core, char *out, uint32_t cap) const;
        const char *SmpCoreStateName(uint32_t state) const;
        void CopyText(char *dst, uint32_t cap, const char *src) const;
        void AppendText(char *dst, uint32_t cap, const char *src) const;
        void AppendUInt(char *dst, uint32_t cap, uint32_t value) const;
        const char *Basename(const char *path) const;

        static void NavChanged(uint32_t widget, int32_t value, void *user);
        static void ButtonClicked(kbutton_handle button, void *user);
        void HandleButton(Button *button);

    private:
        uint8_t initialized_;
        const kfont *font_;
        kwindow_handle window_;
        kgfx_obj_handle root_;
        kui_radio_handle nav_;
        kgfx_obj_handle nav_root_;
        kui_view_handle main_view_;
        kgfx_obj_handle main_root_;
        kgfx_obj_handle apps_panel_;
        kgfx_obj_handle perf_panel_;
        kgfx_obj_handle devices_panel_;
        Label title_;
        Label summary_;
        AppRow app_rows_[MAX_APP_ROWS];
        PerfLine perf_lines_[PERF_LINE_COUNT];
        Button perf_tabs_[PERF_TAB_COUNT];
        Label perf_heading_;
        Label perf_subheading_;
        Label perf_top_value_;
        Label perf_top_detail_;
        Label perf_graph_label_;
        Label perf_graph_left_;
        Label perf_graph_right_;
        Label perf_comp_label_;
        Label perf_detail_labels_[PERF_DETAIL_COUNT];
        Label perf_detail_values_[PERF_DETAIL_COUNT];
        kgfx_obj_handle perf_graph_bg_;
        kgfx_obj_handle perf_grid_lines_[PERF_GRID_LINES];
        kgfx_obj_handle perf_graph_bars_[PERF_GRAPH_BARS];
        kgfx_obj_handle perf_comp_bg_;
        kgfx_obj_handle perf_comp_segments_[PERF_COMPOSITION_SEGMENTS];
        uint32_t perf_mem_history_[PERF_HISTORY_COUNT];
        uint32_t perf_cpu_history_[PERF_HISTORY_COUNT];
        uint32_t perf_activity_history_[PERF_HISTORY_COUNT];
        uint32_t perf_history_pos_;
        uint32_t active_perf_tab_;
        DeviceRow device_rows_[MAX_DEVICE_ROWS];
        uint32_t active_view_;
        uint32_t last_w_;
        uint32_t last_h_;
        uint32_t app_name_w_;
        uint32_t app_kind_w_;
        uint32_t app_state_w_;
        uint32_t app_mem_w_;
        uint32_t app_activity_w_;
        uint32_t device_group_w_;
        uint32_t device_name_w_;
        uint32_t device_status_w_;
        uint32_t device_detail_w_;
        uint32_t perf_detail_x_;
        uint32_t perf_detail_w_;
        uint32_t perf_detail_value_w_;
        uint32_t perf_top_value_w_;
        uint32_t refresh_tick_;
        uint32_t pending_kernel_row_;
        kwindow_handle modal_window_;
        Label modal_title_;
        Label modal_body_;
        Label modal_body_2_;
        Button modal_confirm_;
        Button modal_cancel_;
        kbutton_style button_style_;
        kbutton_style selected_style_;
        kbutton_style danger_style_;
    };

    static TaskManager g_task_manager;

    static kcolor rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        kcolor c = {r, g, b};
        return c;
    }

    void TaskManager::CopyText(char *dst, uint32_t cap, const char *src) const
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

    void TaskManager::AppendText(char *dst, uint32_t cap, const char *src) const
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

    void TaskManager::AppendUInt(char *dst, uint32_t cap, uint32_t value) const
    {
        char tmp[16];
        uint32_t len = 0u;
        if (value == 0u)
        {
            AppendText(dst, cap, "0");
            return;
        }
        while (value && len < sizeof(tmp))
        {
            tmp[len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (len)
        {
            char s[2] = {tmp[--len], 0};
            AppendText(dst, cap, s);
        }
    }

    void TaskManager::FormatUInt(uint32_t value, char *out, uint32_t cap) const
    {
        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendUInt(out, cap, value);
    }

    void TaskManager::FormatHex64(uint64_t value, char *out, uint32_t cap) const
    {
        const char *hex = "0123456789ABCDEF";
        if (!out || cap == 0u)
            return;
        if (cap < 3u)
        {
            out[0] = 0;
            return;
        }
        out[0] = '0';
        out[1] = 'x';
        uint32_t pos = 2u;
        uint32_t started = 0u;
        for (int32_t i = 15; i >= 0 && pos + 1u < cap; --i)
        {
            uint8_t nibble = (uint8_t)((value >> ((uint32_t)i * 4u)) & 0xFull);
            if (nibble || started || i == 0)
            {
                started = 1u;
                out[pos++] = hex[nibble];
            }
        }
        out[pos] = 0;
    }

    const char *TaskManager::CpuImplementerName(uint32_t implementer) const
    {
        switch (implementer)
        {
        case 0x41u:
            return "Arm";
        case 0x42u:
            return "Broadcom";
        case 0x43u:
            return "Cavium";
        case 0x46u:
            return "Fujitsu";
        case 0x48u:
            return "HiSilicon";
        case 0x4Eu:
            return "NVIDIA";
        case 0x50u:
            return "Ampere";
        case 0x51u:
            return "Qualcomm";
        case 0x61u:
            return "Apple";
        default:
            return "Unknown";
        }
    }

    void TaskManager::FormatMidrSummary(uint64_t midr, char *out, uint32_t cap) const
    {
        uint32_t implementer = (uint32_t)((midr >> 24u) & 0xFFu);
        uint32_t part = (uint32_t)((midr >> 4u) & 0xFFFu);
        uint32_t variant = (uint32_t)((midr >> 20u) & 0xFu);
        uint32_t revision = (uint32_t)(midr & 0xFu);
        char tmp[32];

        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendText(out, cap, CpuImplementerName(implementer));
        AppendText(out, cap, " part ");
        FormatHex64(part, tmp, sizeof(tmp));
        AppendText(out, cap, tmp);
        AppendText(out, cap, " r");
        AppendUInt(out, cap, variant);
        AppendText(out, cap, "p");
        AppendUInt(out, cap, revision);
    }

    void TaskManager::FormatMpidrAffinity(uint64_t mpidr, char *out, uint32_t cap) const
    {
        uint32_t aff0 = (uint32_t)(mpidr & 0xFFu);
        uint32_t aff1 = (uint32_t)((mpidr >> 8u) & 0xFFu);
        uint32_t aff2 = (uint32_t)((mpidr >> 16u) & 0xFFu);
        uint32_t aff3 = (uint32_t)((mpidr >> 32u) & 0xFFu);
        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendUInt(out, cap, aff3);
        AppendText(out, cap, ":");
        AppendUInt(out, cap, aff2);
        AppendText(out, cap, ":");
        AppendUInt(out, cap, aff1);
        AppendText(out, cap, ":");
        AppendUInt(out, cap, aff0);
    }

    void TaskManager::FormatCoreSummary(const cpu_core_info &core, char *out, uint32_t cap) const
    {
        char tmp[32];
        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendText(out, cap, core.enabled ? "on aff " : "off aff ");
        FormatMpidrAffinity(core.mpidr, tmp, sizeof(tmp));
        AppendText(out, cap, tmp);
        AppendText(out, cap, " uid ");
        AppendUInt(out, cap, core.acpi_uid);
    }

    const char *TaskManager::SmpCoreStateName(uint32_t state) const
    {
        switch (state)
        {
        case SMP_CORE_ABSENT:
            return "absent";
        case SMP_CORE_BOOT:
            return "boot";
        case SMP_CORE_DISCOVERED:
            return "parked";
        case SMP_CORE_STARTING:
            return "starting";
        case SMP_CORE_ONLINE:
            return "worker";
        case SMP_CORE_FAILED:
            return "failed";
        default:
            return "unknown";
        }
    }

    void TaskManager::FormatSmpCoreSummary(const smp_core_status &core, char *out, uint32_t cap) const
    {
        char tmp[32];
        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendText(out, cap, SmpCoreStateName(core.state));
        if (core.busy)
            AppendText(out, cap, " busy");
        AppendText(out, cap, " aff ");
        FormatMpidrAffinity(core.mpidr, tmp, sizeof(tmp));
        AppendText(out, cap, tmp);
        if (core.state == SMP_CORE_FAILED)
        {
            AppendText(out, cap, " rc ");
            if (core.last_psci_status < 0)
            {
                AppendText(out, cap, "-");
                AppendUInt(out, cap, (uint32_t)(-core.last_psci_status));
            }
            else
                AppendUInt(out, cap, (uint32_t)core.last_psci_status);
        }
        else if (core.jobs_completed)
        {
            AppendText(out, cap, " jobs ");
            AppendUInt(out, cap, core.jobs_completed);
        }
        if (core.state == SMP_CORE_ONLINE)
        {
            AppendText(out, cap, " poll ");
            AppendUInt(out, cap, core.poll_ticks);
            AppendText(out, cap, " st ");
            AppendUInt(out, cap, core.worker_stage);
        }
    }

    void TaskManager::FormatBytes(uint64_t bytes, char *out, uint32_t cap) const
    {
        static const char *units[] = {"B", "KiB", "MiB", "GiB"};
        uint32_t unit = 0u;
        uint64_t whole = bytes;
        uint64_t rem = 0u;

        if (!out || cap == 0u)
            return;
        while (whole >= 1024u && unit < 3u)
        {
            rem = whole % 1024u;
            whole /= 1024u;
            ++unit;
        }
        out[0] = 0;
        AppendUInt(out, cap, (uint32_t)whole);
        if (unit && whole < 100u)
        {
            uint32_t tenth = (uint32_t)((rem * 10u) / 1024u);
            AppendText(out, cap, ".");
            AppendUInt(out, cap, tenth);
        }
        AppendText(out, cap, " ");
        AppendText(out, cap, units[unit]);
    }

    void TaskManager::FormatPercent(uint64_t value, uint64_t total, char *out, uint32_t cap) const
    {
        uint32_t pct = total ? (uint32_t)((value * 100u) / total) : 0u;
        if (!out || cap == 0u)
            return;
        out[0] = 0;
        AppendUInt(out, cap, pct);
        AppendText(out, cap, "%");
    }

    const char *TaskManager::Basename(const char *path) const
    {
        const char *last = path;
        if (!path || !path[0])
            return "SACX App";
        for (const char *p = path; *p; ++p)
            if (*p == '/' || *p == '\\')
                last = p + 1;
        return last && last[0] ? last : path;
    }

    void TaskManager::SetObjectVisible(kgfx_obj_handle obj, uint32_t visible)
    {
        kgfx_obj *o = kgfx_obj_ref(obj);
        if (o)
            o->visible = visible ? 1u : 0u;
    }

    void TaskManager::SetText(Label &label, const char *text)
    {
        CopyText(label.text, sizeof(label.text), text);
        kgfx_text_set(label.obj, label.text);
    }

    void TaskManager::SetTextEllipsized(Label &label, const char *text, uint32_t max_px)
    {
        kui_fit_desc fit = {};
        fit.max_w = max_px;
        fit.padding_x = 0u;
        fit.max_scale = kwindow_ui_text_scale(1u);
        fit.min_scale = ktext_scale_from_fp(560u);
        fit.flags = KUI_TEXT_FIT_ELLIPSIZE | KUI_TEXT_FIT_ALLOW_FRACTIONAL;
        (void)kui_label_fit(label.obj, label.text, sizeof(label.text), text, &fit);
    }

    void TaskManager::SetTextWrapped(Label &label, const char *text, uint32_t max_px, uint32_t max_lines)
    {
        kui_fit_desc fit = {};
        fit.max_w = max_px;
        fit.max_lines = max_lines;
        fit.max_scale = kwindow_ui_text_scale(1u);
        fit.min_scale = ktext_scale_from_fp(620u);
        fit.flags = KUI_TEXT_FIT_WRAP | KUI_TEXT_FIT_ALLOW_FRACTIONAL;
        (void)kui_label_fit(label.obj, label.text, sizeof(label.text), text, &fit);
    }

    void TaskManager::SetButtonText(Button &button, const char *text)
    {
        CopyText(button.text, sizeof(button.text), text);
        kui_fit_desc fit = {};
        fit.padding_x = 6u;
        fit.padding_y = 3u;
        fit.max_scale = kwindow_ui_text_scale(1u);
        fit.min_scale = 1u;
        fit.flags = KUI_TEXT_FIT_ELLIPSIZE | KUI_TEXT_FIT_ALLOW_FRACTIONAL;
        if (kui_button_fit_label(button.button, button.label, button.text, sizeof(button.text), text, &fit) != 0)
            kgfx_text_set(button.label, button.text);
    }

    Label TaskManager::AddLabel(kgfx_obj_handle parent, const char *text, int32_t x, int32_t y, kcolor color, uint32_t scale)
    {
        Label label;
        CopyText(label.text, sizeof(label.text), text);
        label.obj = kgfx_obj_add_text(font_, label.text, x, y, 4, color, 255u, scale, 0, 0, KTEXT_ALIGN_LEFT, 1u);
        kgfx_obj_set_parent(label.obj, parent);
        kgfx_obj_set_clip_to_parent(label.obj, 1u);
        return label;
    }

    Button TaskManager::AddButton(kgfx_obj_handle parent, const char *text, uint32_t action, uint32_t row)
    {
        Button button;
        button.button = kbutton_add_rect(0, 0, 60, 24, 5, &button_style_, 0, 0);
        button.label = kgfx_obj_add_text(font_, "", 0, 0, 6, rgb(238, 244, 250), 255u, kwindow_ui_text_scale(1u), 0, 0, KTEXT_ALIGN_CENTER, 1u);
        button.action = action;
        button.row = row;
        SetButtonText(button, text);
        kgfx_obj_set_parent(kbutton_root(button.button), parent);
        kgfx_obj_set_parent(button.label, kbutton_root(button.button));
        kgfx_obj_set_clip_to_parent(kbutton_root(button.button), 1u);
        kgfx_obj_set_clip_to_parent(button.label, 1u);
        return button;
    }

    void TaskManager::PositionButton(Button &button, int32_t x, int32_t y, uint32_t w, uint32_t h)
    {
        kgfx_obj *root = kgfx_obj_ref(kbutton_root(button.button));
        if (root && root->kind == KGFX_OBJ_RECT)
        {
            root->u.rect.x = x;
            root->u.rect.y = y;
            root->u.rect.w = w;
            root->u.rect.h = h;
        }
        kgfx_obj *label = kgfx_obj_ref(button.label);
        if (label && label->kind == KGFX_OBJ_TEXT)
        {
            kui_fit_desc fit = {};
            fit.max_w = w;
            fit.padding_x = 6u;
            fit.padding_y = 3u;
            fit.max_scale = kwindow_ui_text_scale(1u);
            fit.min_scale = 1u;
            fit.flags = KUI_TEXT_FIT_ELLIPSIZE | KUI_TEXT_FIT_ALLOW_FRACTIONAL;
            (void)kui_button_fit_label(button.button, button.label, button.text, sizeof(button.text), button.text, &fit);
        }
    }

    void TaskManager::Init(const kfont *font)
    {
        if (initialized_ || !font)
            return;

        font_ = font;
        window_.idx = -1;
        modal_window_.idx = -1;
        nav_.idx = -1;
        main_view_.idx = -1;
        active_view_ = VIEW_APPS;
        active_perf_tab_ = PERF_MEMORY;
        perf_history_pos_ = 0u;
        pending_kernel_row_ = 0xFFFFFFFFu;
        perf_detail_x_ = 0u;
        perf_detail_w_ = 0u;
        perf_detail_value_w_ = 0u;
        perf_top_value_w_ = 0u;
        for (uint32_t i = 0u; i < PERF_HISTORY_COUNT; ++i)
        {
            perf_mem_history_[i] = 0u;
            perf_cpu_history_[i] = 0u;
            perf_activity_history_[i] = 0u;
        }

        button_style_ = kbutton_style_default();
        button_style_.fill = rgb(34, 45, 63);
        button_style_.hover_fill = rgb(48, 63, 86);
        button_style_.pressed_fill = rgb(61, 82, 112);
        button_style_.outline = rgb(114, 145, 190);
        button_style_.outline_width = 1u;

        selected_style_ = button_style_;
        selected_style_.fill = rgb(43, 54, 68);
        selected_style_.hover_fill = rgb(56, 70, 88);
        selected_style_.pressed_fill = rgb(36, 47, 62);
        selected_style_.outline = rgb(80, 170, 245);

        danger_style_ = button_style_;
        danger_style_.fill = rgb(126, 48, 48);
        danger_style_.hover_fill = rgb(154, 62, 62);
        danger_style_.pressed_fill = rgb(96, 36, 36);
        danger_style_.outline = rgb(250, 178, 178);

        CreateWindow();
        CreateViews();
        CreateRows();
        CreateModal();
        Layout();
        Refresh();
        kwindow_set_visible(window_, 0u);
        initialized_ = 1u;
    }

    void TaskManager::CreateWindow()
    {
        kwindow_style style = kwindow_style_default();
        style.body_fill = rgb(13, 17, 23);
        style.body_outline = rgb(82, 112, 154);
        style.titlebar_fill = rgb(25, 34, 47);
        style.title_color = rgb(235, 241, 248);
        style.title_scale = 2u;
        window_ = kwindow_create(96, 74, 860, 560, 32, font_, "Task Manager", &style);
        root_ = kwindow_root(window_);
        kwindow_set_close_deferred(window_, 1u);
    }

    void TaskManager::CreateViews()
    {
        const char *nav_items[] = {"Apps", "Performance", "Devices"};
        kui_layout_desc desc = {};

        title_ = AddLabel(root_, "System Dashboard", 18, 26, rgb(233, 240, 248), kwindow_ui_text_scale(2u));
        summary_ = AddLabel(root_, "", 18, 78, rgb(175, 190, 210), kwindow_ui_text_scale(1u));

        (void)kui_radio_create(root_, 16, 112, 150, 36, 3, nav_items, 3u, VIEW_APPS, NavChanged, this, &nav_, &nav_root_);
        (void)kui_view_create_rect(root_, 184, 104, 650, 420, 2, rgb(18, 24, 32), 1u, &main_view_, &main_root_);

        apps_panel_ = kgfx_obj_add_rect(0, 0, 650, 452, 3, rgb(18, 24, 32), 1u);
        perf_panel_ = kgfx_obj_add_rect(0, 0, 650, 452, 3, rgb(18, 24, 32), 1u);
        devices_panel_ = kgfx_obj_add_rect(0, 0, 650, 452, 3, rgb(18, 24, 32), 1u);
        kgfx_obj_set_parent(apps_panel_, main_root_);
        kgfx_obj_set_parent(perf_panel_, main_root_);
        kgfx_obj_set_parent(devices_panel_, main_root_);
        kgfx_obj_set_clip_to_parent(apps_panel_, 1u);
        kgfx_obj_set_clip_to_parent(perf_panel_, 1u);
        kgfx_obj_set_clip_to_parent(devices_panel_, 1u);
        (void)kui_view_add_obj(main_view_, VIEW_APPS, apps_panel_);
        (void)kui_view_add_obj(main_view_, VIEW_PERF, perf_panel_);
        (void)kui_view_add_obj(main_view_, VIEW_DEVICES, devices_panel_);
        (void)kui_view_set_state(main_view_, VIEW_APPS);

        desc.kind = KUI_LAYOUT_COLUMN;
        desc.gap_y = 6u;
        (void)kui_view_set_layout(main_view_, &desc);
    }

    void TaskManager::CreateRows()
    {
        for (uint32_t i = 0u; i < MAX_APP_ROWS; ++i)
        {
            AppRow &row = app_rows_[i];
            row.root = kgfx_obj_add_rect(0, 0, 1, 1, 4, (i & 1u) ? rgb(22, 29, 38) : rgb(19, 25, 34), 0u);
            kgfx_obj_set_parent(row.root, apps_panel_);
            row.name = AddLabel(row.root, "", 8, 10, rgb(232, 239, 248), kwindow_ui_text_scale(1u));
            row.kind = AddLabel(row.root, "", 176, 10, rgb(175, 194, 214), kwindow_ui_text_scale(1u));
            row.state = AddLabel(row.root, "", 282, 10, rgb(184, 214, 190), kwindow_ui_text_scale(1u));
            row.memory = AddLabel(row.root, "", 380, 10, rgb(218, 208, 168), kwindow_ui_text_scale(1u));
            row.activity = AddLabel(row.root, "", 480, 10, rgb(174, 200, 230), kwindow_ui_text_scale(1u));
            row.focus = AddButton(row.root, "Focus", ACTION_FOCUS, i);
            row.action = AddButton(row.root, "End", ACTION_TERMINATE, i);
            kbutton_set_callback(row.focus.button, ButtonClicked, &row.focus);
            kbutton_set_callback(row.action.button, ButtonClicked, &row.action);
            kbutton_set_style(row.action.button, &danger_style_);
            SetRowVisible(row, 0u);
        }

        const char *perf_names[PERF_TAB_COUNT] = {"CPU", "Memory", "Storage", "Network", "Activity"};
        for (uint32_t i = 0u; i < PERF_TAB_COUNT; ++i)
        {
            perf_tabs_[i] = AddButton(perf_panel_, perf_names[i], ACTION_PERF_SELECT, i);
            kbutton_set_callback(perf_tabs_[i].button, ButtonClicked, &perf_tabs_[i]);
        }

        perf_heading_ = AddLabel(perf_panel_, "", 190, 18, rgb(242, 247, 255), kwindow_ui_text_scale(2u));
        perf_subheading_ = AddLabel(perf_panel_, "", 192, 58, rgb(176, 190, 208), kwindow_ui_text_scale(1u));
        perf_top_value_ = AddLabel(perf_panel_, "", 0, 22, rgb(236, 244, 255), kwindow_ui_text_scale(1u));
        perf_top_detail_ = AddLabel(perf_panel_, "", 0, 54, rgb(178, 195, 216), kwindow_ui_text_scale(1u));
        perf_graph_label_ = AddLabel(perf_panel_, "", 192, 88, rgb(210, 224, 240), kwindow_ui_text_scale(1u));
        perf_graph_left_ = AddLabel(perf_panel_, "60 seconds", 192, 0, rgb(178, 190, 204), 1u);
        perf_graph_right_ = AddLabel(perf_panel_, "now", 0, 0, rgb(178, 190, 204), 1u);
        perf_comp_label_ = AddLabel(perf_panel_, "Memory composition", 192, 0, rgb(210, 224, 240), 1u);
        perf_graph_bg_ = kgfx_obj_add_rect(190, 112, 420, 190, 4, rgb(13, 21, 34), 1u);
        kgfx_obj_set_parent(perf_graph_bg_, perf_panel_);
        kgfx_obj_set_outline(perf_graph_bg_, 1u, rgb(66, 118, 184));
        perf_comp_bg_ = kgfx_obj_add_rect(190, 330, 420, 46, 4, rgb(13, 21, 34), 1u);
        kgfx_obj_set_parent(perf_comp_bg_, perf_panel_);
        kgfx_obj_set_outline(perf_comp_bg_, 1u, rgb(66, 118, 184));
        for (uint32_t i = 0u; i < PERF_GRAPH_BARS; ++i)
        {
            perf_graph_bars_[i] = kgfx_obj_add_rect(0, 0, 1, 1, 5, rgb(66, 150, 245), 1u);
            kgfx_obj_set_parent(perf_graph_bars_[i], perf_panel_);
        }
        for (uint32_t i = 0u; i < PERF_GRID_LINES; ++i)
        {
            perf_grid_lines_[i] = kgfx_obj_add_rect(0, 0, 1, 1, 4, rgb(38, 50, 64), 1u);
            kgfx_obj_set_parent(perf_grid_lines_[i], perf_panel_);
        }
        for (uint32_t i = 0u; i < PERF_COMPOSITION_SEGMENTS; ++i)
        {
            perf_comp_segments_[i] = kgfx_obj_add_rect(0, 0, 1, 1, 5,
                                                       i == 0u ? rgb(47, 115, 210) : (i == 1u ? rgb(36, 72, 125) : rgb(20, 28, 40)),
                                                       1u);
            kgfx_obj_set_parent(perf_comp_segments_[i], perf_panel_);
        }
        for (uint32_t i = 0u; i < PERF_DETAIL_COUNT; ++i)
        {
            perf_detail_labels_[i] = AddLabel(perf_panel_, "", 192, 0, rgb(182, 196, 212), 1u);
            perf_detail_values_[i] = AddLabel(perf_panel_, "", 192, 0, rgb(238, 245, 252), kwindow_ui_text_scale(1u));
        }

        for (uint32_t i = 0u; i < PERF_LINE_COUNT; ++i)
        {
            PerfLine &line = perf_lines_[i];
            line.name = AddLabel(perf_panel_, "", 20, 20 + (int32_t)i * 34, rgb(218, 229, 240), 1u);
            line.value = AddLabel(perf_panel_, "", 300, 20 + (int32_t)i * 34, rgb(188, 207, 226), 1u);
            line.bar = kgfx_obj_add_rect(470, 22 + (int32_t)i * 34, 1, 14, 4, rgb(72, 126, 180), 0u);
            kgfx_obj_set_parent(line.bar, perf_panel_);
            SetObjectVisible(line.name.obj, 0u);
            SetObjectVisible(line.value.obj, 0u);
        }

        for (uint32_t i = 0u; i < MAX_DEVICE_ROWS; ++i)
        {
            DeviceRow &row = device_rows_[i];
            row.root = kgfx_obj_add_rect(0, 0, 1, 1, 4, (i & 1u) ? rgb(22, 29, 38) : rgb(19, 25, 34), 0u);
            kgfx_obj_set_parent(row.root, devices_panel_);
            row.group = AddLabel(row.root, "", 8, 8, rgb(180, 202, 230), kwindow_ui_text_scale(1u));
            row.name = AddLabel(row.root, "", 112, 8, rgb(232, 239, 248), kwindow_ui_text_scale(1u));
            row.status = AddLabel(row.root, "", 292, 8, rgb(184, 214, 190), kwindow_ui_text_scale(1u));
            row.detail = AddLabel(row.root, "", 18, 38, rgb(178, 190, 204), kwindow_ui_text_scale(1u));
            SetDeviceRowVisible(row, 0u);
        }
    }

    void TaskManager::CreateModal()
    {
        kwindow_style style = kwindow_style_default();
        style.body_fill = rgb(22, 20, 24);
        style.body_outline = rgb(210, 132, 88);
        style.titlebar_fill = rgb(76, 45, 36);
        style.title_color = rgb(255, 232, 210);
        style.title_scale = 1u;
        modal_window_ = kwindow_create(0, 0, 640, 280, 80, font_, "Kernel App Warning", &style);
        kwindow_set_close_deferred(modal_window_, 1u);
        kwindow_set_visible(modal_window_, 0u);
        modal_title_ = AddLabel(kwindow_root(modal_window_), "", 18, 54, rgb(255, 226, 190), kwindow_ui_text_scale(1u));
        modal_body_ = AddLabel(kwindow_root(modal_window_), "", 18, 70, rgb(236, 224, 214), 1u);
        modal_body_2_ = AddLabel(kwindow_root(modal_window_), "", 18, 132, rgb(236, 224, 214), 1u);
        modal_confirm_ = AddButton(kwindow_root(modal_window_), "Terminate", ACTION_CONFIRM_TERMINATE, 0);
        modal_cancel_ = AddButton(kwindow_root(modal_window_), "Cancel", ACTION_CANCEL_MODAL, 0);
        kbutton_set_callback(modal_confirm_.button, ButtonClicked, &modal_confirm_);
        kbutton_set_callback(modal_cancel_.button, ButtonClicked, &modal_cancel_);
        kbutton_set_style(modal_confirm_.button, &danger_style_);
        PositionButton(modal_confirm_, 330, 204, 160, 38);
        PositionButton(modal_cancel_, 508, 204, 108, 38);
    }

    void TaskManager::Layout()
    {
        kgfx_obj *root = kgfx_obj_ref(root_);
        uint32_t w = 860u;
        uint32_t h = 560u;
        uint32_t left_w = 162u;
        uint32_t main_x = left_w + 24u;
        uint32_t main_y = 124u;
        uint32_t main_w = 1u;
        uint32_t main_h = 1u;
        uint32_t row_w = 1u;
        uint32_t action_w = 88u;
        uint32_t focus_w = 76u;
        uint32_t actions_total = 178u;
        uint32_t app_cols_w = 1u;
        uint32_t x = 8u;

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        w = root->u.rect.w;
        h = root->u.rect.h;
        if (w < 620u)
            w = 620u;
        if (h < 430u)
            h = 430u;
        last_w_ = w;
        last_h_ = h;

        main_w = w > main_x + 18u ? w - main_x - 18u : 1u;
        main_h = h > main_y + 20u ? h - main_y - 20u : 1u;
        row_w = main_w > 20u ? main_w - 20u : 1u;
        if (main_w >= 760u)
        {
            focus_w = 84u;
            action_w = 96u;
            actions_total = 194u;
        }
        app_cols_w = row_w > actions_total + 22u ? row_w - actions_total - 22u : row_w;
        app_name_w_ = app_cols_w * 28u / 100u;
        app_kind_w_ = app_cols_w * 18u / 100u;
        app_state_w_ = app_cols_w * 15u / 100u;
        app_mem_w_ = app_cols_w * 16u / 100u;
        app_activity_w_ = app_cols_w > app_name_w_ + app_kind_w_ + app_state_w_ + app_mem_w_
                              ? app_cols_w - app_name_w_ - app_kind_w_ - app_state_w_ - app_mem_w_
                              : 64u;
        if (app_name_w_ < 110u)
            app_name_w_ = 110u;
        if (app_kind_w_ < 76u)
            app_kind_w_ = 76u;
        if (app_state_w_ < 70u)
            app_state_w_ = 70u;
        if (app_mem_w_ < 76u)
            app_mem_w_ = 76u;
        if (app_activity_w_ < 84u)
            app_activity_w_ = 84u;

        device_group_w_ = row_w * 16u / 100u;
        device_name_w_ = row_w * 30u / 100u;
        device_status_w_ = row_w * 16u / 100u;
        if (device_group_w_ < 72u)
            device_group_w_ = 72u;
        if (device_name_w_ < 140u)
            device_name_w_ = 140u;
        if (device_status_w_ < 82u)
            device_status_w_ = 82u;
        device_detail_w_ = row_w > 28u ? row_w - 28u : 1u;
        (void)kui_view_set_bounds(main_view_, (int32_t)main_x, (int32_t)main_y, main_w, main_h);

        kgfx_obj *panel = kgfx_obj_ref(apps_panel_);
        if (panel && panel->kind == KGFX_OBJ_RECT)
            panel->u.rect = {0, 0, main_w, main_h};
        panel = kgfx_obj_ref(perf_panel_);
        if (panel && panel->kind == KGFX_OBJ_RECT)
            panel->u.rect = {0, 0, main_w, main_h};
        panel = kgfx_obj_ref(devices_panel_);
        if (panel && panel->kind == KGFX_OBJ_RECT)
            panel->u.rect = {0, 0, main_w, main_h};

        kgfx_obj *nav = kgfx_obj_ref(nav_root_);
        if (nav && nav->kind == KGFX_OBJ_RECT)
            nav->u.rect = {16, 134, left_w - 28u, 120u};

        {
            uint32_t perf_side_w = main_w > 520u ? 176u : 146u;
            uint32_t detail_x = perf_side_w + 24u;
            uint32_t detail_w = main_w > detail_x + 20u ? main_w - detail_x - 20u : 1u;
            uint32_t graph_h = main_h > 330u ? (main_h * 32u) / 100u : 140u;
            if (graph_h < 140u)
                graph_h = 140u;
            if (graph_h > 190u)
                graph_h = 190u;
            uint32_t line_h = kui_text_line_height(kwindow_ui_text_scale(1u), 0);
            uint32_t heading_h = kui_text_line_height(kwindow_ui_text_scale(2u), 0);
            uint32_t header_bottom = 18u + heading_h;
            if (header_bottom < 60u + line_h)
                header_bottom = 60u + line_h;
            if (header_bottom < 54u + line_h)
                header_bottom = 54u + line_h;
            uint32_t graph_label_y = header_bottom + 26u;
            uint32_t graph_y = graph_label_y + line_h + 10u;
            uint32_t comp_y = graph_y + graph_h + 40u;
            uint32_t comp_h = 50u;
            kgfx_obj *o = 0;
            perf_detail_x_ = detail_x;
            perf_detail_w_ = detail_w;
            perf_top_value_w_ = detail_w > 720u ? 360u : (detail_w > 460u ? 280u : (detail_w > 2u ? detail_w / 2u : detail_w));

            for (uint32_t i = 0u; i < PERF_TAB_COUNT; ++i)
                PositionButton(perf_tabs_[i], 14, 24 + (int32_t)i * 62, perf_side_w > 22u ? perf_side_w - 22u : 1u, 50u);

            o = kgfx_obj_ref(perf_heading_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)detail_x;
                o->u.text.y = 18;
            }
            o = kgfx_obj_ref(perf_subheading_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)detail_x + 2;
                o->u.text.y = 60;
            }
            o = kgfx_obj_ref(perf_top_value_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)(detail_x + detail_w > perf_top_value_w_ ? detail_x + detail_w - perf_top_value_w_ : detail_x);
                o->u.text.y = 22;
            }
            o = kgfx_obj_ref(perf_top_detail_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)(detail_x + detail_w > perf_top_value_w_ ? detail_x + detail_w - perf_top_value_w_ : detail_x);
                o->u.text.y = 54;
            }
            o = kgfx_obj_ref(perf_graph_label_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)detail_x;
                o->u.text.y = (int32_t)graph_label_y;
            }
            o = kgfx_obj_ref(perf_graph_bg_);
            if (o && o->kind == KGFX_OBJ_RECT)
                o->u.rect = {(int32_t)detail_x, (int32_t)graph_y, detail_w, graph_h};
            o = kgfx_obj_ref(perf_graph_left_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)detail_x;
                o->u.text.y = (int32_t)(graph_y + graph_h + 8u);
            }
            o = kgfx_obj_ref(perf_graph_right_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)(detail_x + detail_w > 44u ? detail_x + detail_w - 44u : detail_x);
                o->u.text.y = (int32_t)(graph_y + graph_h + 8u);
            }
            o = kgfx_obj_ref(perf_comp_label_.obj);
            if (o && o->kind == KGFX_OBJ_TEXT)
            {
                o->u.text.x = (int32_t)detail_x;
                o->u.text.y = (int32_t)(comp_y - 20u);
            }
            o = kgfx_obj_ref(perf_comp_bg_);
            if (o && o->kind == KGFX_OBJ_RECT)
                o->u.rect = {(int32_t)detail_x, (int32_t)comp_y, detail_w, comp_h};

            uint32_t detail_y = comp_y + comp_h + 26u;
            uint32_t col_w = detail_w > 2u ? detail_w / 2u : detail_w;
            perf_detail_value_w_ = col_w > 134u ? col_w - 134u : 80u;
            uint32_t detail_count = active_perf_tab_ == PERF_CPU ? 4u : 8u;
            uint32_t detail_cols = 2u;
            if (active_perf_tab_ == PERF_CPU)
                detail_y = graph_y + graph_h + 48u;
            if (active_perf_tab_ == PERF_CPU)
            {
                col_w = detail_w > detail_cols ? detail_w / detail_cols : detail_w;
                perf_detail_value_w_ = col_w > 124u ? col_w - 124u : 72u;
            }
            for (uint32_t i = 0u; i < PERF_DETAIL_COUNT; ++i)
            {
                uint32_t col = active_perf_tab_ == PERF_CPU ? (i % detail_cols) : (i / 4u);
                uint32_t row = active_perf_tab_ == PERF_CPU ? (i / detail_cols) : (i % 4u);
                int32_t tx = (int32_t)(detail_x + col * col_w);
                int32_t ty = (int32_t)(detail_y + row * (active_perf_tab_ == PERF_CPU ? 30u : 34u));
                SetObjectVisible(perf_detail_labels_[i].obj, i < detail_count ? 1u : 0u);
                SetObjectVisible(perf_detail_values_[i].obj, i < detail_count ? 1u : 0u);
                o = kgfx_obj_ref(perf_detail_labels_[i].obj);
                if (o && o->kind == KGFX_OBJ_TEXT)
                    o->u.text.x = tx;
                if (o && o->kind == KGFX_OBJ_TEXT)
                    o->u.text.y = ty;
                o = kgfx_obj_ref(perf_detail_values_[i].obj);
                if (o && o->kind == KGFX_OBJ_TEXT)
                {
                    o->u.text.x = tx + 112;
                    o->u.text.y = ty;
                }
            }
        }

        for (uint32_t i = 0u; i < MAX_APP_ROWS; ++i)
        {
            AppRow &row = app_rows_[i];
            kgfx_obj *r = kgfx_obj_ref(row.root);
            if (r && r->kind == KGFX_OBJ_RECT)
                r->u.rect = {10, 54 + (int32_t)i * 60, row_w, 54u};
            kgfx_obj *t = kgfx_obj_ref(row.name.obj);
            x = 8u;
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += app_name_w_;
            t = kgfx_obj_ref(row.kind.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += app_kind_w_;
            t = kgfx_obj_ref(row.state.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += app_state_w_;
            t = kgfx_obj_ref(row.memory.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += app_mem_w_;
            t = kgfx_obj_ref(row.activity.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            PositionButton(row.focus, (int32_t)(row_w > actions_total ? row_w - actions_total : 0u), 10, focus_w, 34u);
            PositionButton(row.action, (int32_t)(row_w > action_w + 8u ? row_w - action_w - 8u : focus_w + 6u), 10, action_w, 34u);
        }

        for (uint32_t i = 0u; i < MAX_DEVICE_ROWS; ++i)
        {
            kgfx_obj *r = kgfx_obj_ref(device_rows_[i].root);
            if (r && r->kind == KGFX_OBJ_RECT)
                r->u.rect = {10, 54 + (int32_t)i * 88, row_w, 80u};
            kgfx_obj *t = kgfx_obj_ref(device_rows_[i].group.obj);
            x = 8u;
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += device_group_w_;
            t = kgfx_obj_ref(device_rows_[i].name.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            x += device_name_w_;
            t = kgfx_obj_ref(device_rows_[i].status.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
                t->u.text.x = (int32_t)x;
            t = kgfx_obj_ref(device_rows_[i].detail.obj);
            if (t && t->kind == KGFX_OBJ_TEXT)
            {
                t->u.text.x = 14;
                t->u.text.y = 38;
            }
        }
    }

    const char *TaskManager::SacxStateName(uint32_t state) const
    {
        switch (state)
        {
        case SACX_TASK_READY:
            return "ready";
        case SACX_TASK_SLEEPING:
            return "sleeping";
        case SACX_TASK_EXITED:
            return "exited";
        case SACX_TASK_FAULTED:
            return "faulted";
        default:
            return "unknown";
        }
    }

    void TaskManager::SetRowVisible(AppRow &row, uint32_t visible)
    {
        row.visible = visible ? 1u : 0u;
        SetObjectVisible(row.root, visible);
        SetObjectVisible(row.name.obj, visible);
        SetObjectVisible(row.kind.obj, visible);
        SetObjectVisible(row.state.obj, visible);
        SetObjectVisible(row.memory.obj, visible);
        SetObjectVisible(row.activity.obj, visible);
        SetObjectVisible(kbutton_root(row.focus.button), visible);
        SetObjectVisible(row.focus.label, visible);
        SetObjectVisible(kbutton_root(row.action.button), visible);
        SetObjectVisible(row.action.label, visible);
        kbutton_set_enabled(row.focus.button, visible ? 1u : 0u);
        kbutton_set_enabled(row.action.button, visible ? 1u : 0u);
    }

    void TaskManager::SetDeviceRowVisible(DeviceRow &row, uint32_t visible)
    {
        SetObjectVisible(row.root, visible);
        SetObjectVisible(row.group.obj, visible);
        SetObjectVisible(row.name.obj, visible);
        SetObjectVisible(row.status.obj, visible);
        SetObjectVisible(row.detail.obj, visible);
    }

    void TaskManager::RefreshApps()
    {
        sacx_task_info tasks[16];
        uint32_t task_count = sacx_runtime_task_snapshot(tasks, 16u);
        uint32_t row_idx = 0u;
        uint32_t running_sacx = 0u;
        uint32_t kernel_visible = 0u;
        char buf[TEXT_CAP];

        const struct KernelEntry
        {
            const char *name;
            uint32_t id;
            uint32_t visible;
            uint32_t protected_entry;
        } kernels[] = {
            {"Terminal", 1u, (uint32_t)terminal_visible(), 0u},
            {"File Explorer", 2u, (uint32_t)file_explorer_visible(), 0u},
            {"Text Editor", 3u, (uint32_t)text_editor_visible(), 0u},
            {"Desktop Shell", 4u, 1u, 1u},
            {"Task Manager", 5u, 1u, 1u},
            {"SACX Runtime", 6u, 1u, 1u},
        };

        for (uint32_t i = 0u; i < task_count && row_idx < MAX_APP_ROWS; ++i)
        {
            AppRow &row = app_rows_[row_idx];
            uint64_t mem = (uint64_t)tasks[i].arena_size + (uint64_t)tasks[i].image_size;
            row.row_type = ROW_SACX;
            row.task_id = tasks[i].task_id;
            row.kernel_id = 0u;
            row.protected_entry = 0u;
            SetTextEllipsized(row.name, Basename(tasks[i].friendly_path), app_name_w_ ? app_name_w_ - 8u : 120u);
            SetTextEllipsized(row.kind, "SACX", app_kind_w_ ? app_kind_w_ - 8u : 70u);
            SetTextEllipsized(row.state, SacxStateName(tasks[i].state), app_state_w_ ? app_state_w_ - 8u : 70u);
            FormatBytes(mem, buf, sizeof(buf));
            SetTextEllipsized(row.memory, buf, app_mem_w_ ? app_mem_w_ - 8u : 80u);
            buf[0] = 0;
            AppendText(buf, sizeof(buf), "preempt ");
            AppendUInt(buf, sizeof(buf), tasks[i].preemptions);
            SetTextEllipsized(row.activity, buf, app_activity_w_ ? app_activity_w_ - 8u : 90u);
            SetButtonText(row.focus, "Focus");
            SetButtonText(row.action, (tasks[i].state == SACX_TASK_EXITED || tasks[i].state == SACX_TASK_FAULTED) ? "Release" : "End");
            row.action.action = (tasks[i].state == SACX_TASK_EXITED || tasks[i].state == SACX_TASK_FAULTED) ? ACTION_RELEASE : ACTION_TERMINATE;
            SetRowVisible(row, 1u);
            if (tasks[i].state == SACX_TASK_READY || tasks[i].state == SACX_TASK_SLEEPING)
                ++running_sacx;
            ++row_idx;
        }

        for (uint32_t i = 0u; i < (uint32_t)(sizeof(kernels) / sizeof(kernels[0])) && row_idx < MAX_APP_ROWS; ++i)
        {
            AppRow &row = app_rows_[row_idx];
            row.row_type = kernels[i].protected_entry ? ROW_PROTECTED : ROW_KERNEL;
            row.task_id = 0u;
            row.kernel_id = kernels[i].id;
            row.protected_entry = kernels[i].protected_entry;
            SetTextEllipsized(row.name, kernels[i].name, app_name_w_ ? app_name_w_ - 8u : 120u);
            SetTextEllipsized(row.kind, kernels[i].protected_entry ? "System" : "Kernel App", app_kind_w_ ? app_kind_w_ - 8u : 80u);
            SetTextEllipsized(row.state, kernels[i].visible ? "visible" : "hidden", app_state_w_ ? app_state_w_ - 8u : 70u);
            SetTextEllipsized(row.memory, "kernel", app_mem_w_ ? app_mem_w_ - 8u : 80u);
            SetTextEllipsized(row.activity, "accounted", app_activity_w_ ? app_activity_w_ - 8u : 90u);
            SetButtonText(row.focus, "Focus");
            SetButtonText(row.action, kernels[i].protected_entry ? "Info" : "End");
            row.action.action = ACTION_TERMINATE;
            SetRowVisible(row, 1u);
            if (kernels[i].visible && !kernels[i].protected_entry)
                ++kernel_visible;
            ++row_idx;
        }

        while (row_idx < MAX_APP_ROWS)
            SetRowVisible(app_rows_[row_idx++], 0u);

        buf[0] = 0;
        AppendText(buf, sizeof(buf), "SACX running ");
        AppendUInt(buf, sizeof(buf), running_sacx);
        AppendText(buf, sizeof(buf), " | visible kernel apps ");
        AppendUInt(buf, sizeof(buf), kernel_visible);
        SetTextEllipsized(summary_, buf, last_w_ > 36u ? last_w_ - 36u : 560u);
    }

    void TaskManager::PushPerfHistory(uint32_t mem_value, uint32_t cpu_pct, uint32_t activity_pct)
    {
        perf_mem_history_[perf_history_pos_] = mem_value;
        perf_cpu_history_[perf_history_pos_] = cpu_pct > 100u ? 100u : cpu_pct;
        perf_activity_history_[perf_history_pos_] = activity_pct > 100u ? 100u : activity_pct;
        perf_history_pos_ = (perf_history_pos_ + 1u) % PERF_HISTORY_COUNT;
    }

    void TaskManager::DrawPerfGraph(uint32_t *history, uint32_t color_kind, uint32_t ceiling)
    {
        kgfx_obj *bg = kgfx_obj_ref(perf_graph_bg_);
        if (!bg || bg->kind != KGFX_OBJ_RECT || !history)
            return;

        kcolor color = rgb(66, 150, 245);
        if (color_kind == PERF_CPU)
            color = rgb(80, 200, 230);
        else if (color_kind == PERF_NETWORK)
            color = rgb(216, 80, 140);
        else if (color_kind == PERF_ACTIVITY)
            color = rgb(112, 200, 116);

        uint32_t graph_w = bg->u.rect.w > 8u ? bg->u.rect.w - 8u : 1u;
        uint32_t graph_h = bg->u.rect.h > 8u ? bg->u.rect.h - 8u : 1u;
        uint32_t bar_w = graph_w / PERF_GRAPH_BARS;
        uint32_t max_sample = 0u;
        uint32_t scale_top = 0u;
        if (bar_w == 0u)
            bar_w = 1u;

        for (uint32_t i = 0u; i < PERF_GRAPH_BARS; ++i)
        {
            uint32_t sample = (perf_history_pos_ + i) % PERF_HISTORY_COUNT;
            if (history[sample] > max_sample)
                max_sample = history[sample];
        }
        if (max_sample < 4u)
            max_sample = 4u;
        scale_top = (max_sample * 100u + 64u) / 65u;
        if (scale_top < 4u)
            scale_top = 4u;
        if (ceiling && scale_top > ceiling)
            scale_top = ceiling;
        if (ceiling && max_sample >= (ceiling * 95u) / 100u)
            scale_top = ceiling;
        if (scale_top < max_sample)
            scale_top = max_sample;

        for (uint32_t i = 0u; i < PERF_GRAPH_BARS; ++i)
        {
            uint32_t sample = (perf_history_pos_ + i) % PERF_HISTORY_COUNT;
            uint32_t value = history[sample] > scale_top ? scale_top : history[sample];
            uint32_t h = (value * graph_h) / scale_top;
            kgfx_obj *bar = kgfx_obj_ref(perf_graph_bars_[i]);
            if (!h)
                h = 1u;
            if (bar && bar->kind == KGFX_OBJ_RECT)
            {
                bar->u.rect.x = bg->u.rect.x + 4 + (int32_t)(i * bar_w);
                bar->u.rect.y = bg->u.rect.y + 4 + (int32_t)(graph_h - h);
                bar->u.rect.w = bar_w > 1u ? bar_w - 1u : 1u;
                bar->u.rect.h = h;
                bar->fill = color;
                bar->alpha = 190u;
                bar->visible = 1u;
            }
        }
        for (uint32_t i = 0u; i < PERF_GRID_LINES; ++i)
        {
            kgfx_obj *line = kgfx_obj_ref(perf_grid_lines_[i]);
            if (!line || line->kind != KGFX_OBJ_RECT)
                continue;
            if (i < PERF_GRID_LINES / 2u)
            {
                uint32_t y = 4u + ((i + 1u) * graph_h) / ((PERF_GRID_LINES / 2u) + 1u);
                line->u.rect = {bg->u.rect.x + 4, bg->u.rect.y + (int32_t)y, graph_w, 1u};
            }
            else
            {
                uint32_t col = i - (PERF_GRID_LINES / 2u);
                uint32_t x = 4u + ((col + 1u) * graph_w) / ((PERF_GRID_LINES / 2u) + 1u);
                line->u.rect = {bg->u.rect.x + (int32_t)x, bg->u.rect.y + 4, 1u, graph_h};
            }
            line->alpha = 140u;
            line->visible = 1u;
        }
    }

    void TaskManager::RefreshPerformanceLayoutText(uint64_t total_bytes, uint64_t used_bytes, uint64_t free_bytes,
                                                   const pmem_stats &stats, const task_accounting_snapshot &accounting,
                                                   uint64_t total_ticks)
    {
        char a[TEXT_CAP];
        char b[TEXT_CAP];
        uint32_t selected = active_perf_tab_;
        const char *heading = selected == PERF_CPU ? "CPU" :
                              selected == PERF_MEMORY ? "Memory" :
                              selected == PERF_STORAGE ? "Storage" :
                              selected == PERF_NETWORK ? "Network" : "Activity";
        const char *graph_label = selected == PERF_MEMORY ? "Memory usage" :
                                  selected == PERF_CPU ? "Processor activity" :
                                  selected == PERF_STORAGE ? "Storage activity" :
                                  selected == PERF_NETWORK ? "Network activity" : "Kernel activity";
        SetText(perf_heading_, heading);
        SetText(perf_graph_label_, graph_label);

        if (selected == PERF_MEMORY)
        {
            FormatBytes(used_bytes, a, sizeof(a));
            AppendText(a, sizeof(a), " / ");
            FormatBytes(total_bytes, b, sizeof(b));
            AppendText(a, sizeof(a), b);
            SetText(perf_subheading_, "Physical memory");
            SetTextEllipsized(perf_top_value_, a, perf_top_value_w_ ? perf_top_value_w_ : 210u);
            FormatBytes(free_bytes, b, sizeof(b));
            AppendText(b, sizeof(b), " free");
            SetTextEllipsized(perf_top_detail_, b, perf_top_value_w_ ? perf_top_value_w_ : 210u);

            const char *labels[8] = {"In use", "Available", "Total pages", "Free pages",
                                     "Low DMA", "Executable", "Page size", "Frames"};
            uint64_t vals[8] = {used_bytes, free_bytes, stats.total_pages, stats.free_pages,
                                stats.free_lowdma_pages * stats.page_size,
                                stats.free_executable_pages * stats.page_size,
                                stats.page_size, accounting.frame_count};
            for (uint32_t i = 0u; i < 8u; ++i)
            {
                SetText(perf_detail_labels_[i], labels[i]);
                if (i == 2u || i == 3u || i == 7u)
                {
                    a[0] = 0;
                    AppendUInt(a, sizeof(a), (uint32_t)vals[i]);
                }
                else
                    FormatBytes(vals[i], a, sizeof(a));
                SetTextEllipsized(perf_detail_values_[i], a, perf_detail_value_w_ ? perf_detail_value_w_ : 160u);
            }
        }
        else
        {
            cpu_info_snapshot cpu;
            smp_snapshot smp;
            kwork_stats work;
            cpu_info_get(&cpu);
            smp_get_snapshot(&smp);
            kwork_get_stats(&work);
            uint32_t idx = selected == PERF_CPU ? TASK_ACCOUNT_INPUT_UI :
                           selected == PERF_STORAGE ? TASK_ACCOUNT_OTHER :
                           selected == PERF_NETWORK ? TASK_ACCOUNT_OTHER :
                           TASK_ACCOUNT_KERNEL_APPS;
            FormatPercent(idx < TASK_ACCOUNT_BUCKET_COUNT ? accounting.bucket_ticks[idx] : 0u, total_ticks, a, sizeof(a));
            SetText(perf_subheading_, selected == PERF_CPU ? "Rolling kernel accounting" :
                                      selected == PERF_STORAGE ? "Boot/storage activity estimate" :
                                      selected == PERF_NETWORK ? "Driver/network activity estimate" :
                                      "Kernel and SACX runtime buckets");
            SetTextEllipsized(perf_top_value_, a, perf_top_value_w_ ? perf_top_value_w_ : 210u);
            SetTextEllipsized(perf_top_detail_, "last sample", perf_top_value_w_ ? perf_top_value_w_ : 210u);
            if (selected == PERF_CPU)
            {
                FormatMidrSummary(cpu.midr, a, sizeof(a));
                SetTextEllipsized(perf_top_value_, a, perf_top_value_w_ ? perf_top_value_w_ : 210u);
                a[0] = 0;
                AppendUInt(a, sizeof(a), smp.worker_count);
                AppendText(a, sizeof(a), " workers / ");
                AppendUInt(a, sizeof(a), smp.online_count);
                AppendText(a, sizeof(a), " online");
                SetTextEllipsized(perf_top_detail_, a, perf_top_value_w_ ? perf_top_value_w_ : 210u);

                const char *labels[4] = {"Boot MPIDR", "PSCI", "Work", "MADT"};
                for (uint32_t i = 0u; i < 4u; ++i)
                {
                    SetText(perf_detail_labels_[i], labels[i]);
                    if (i == 0u)
                        FormatHex64(cpu.boot_mpidr, a, sizeof(a));
                    else if (i == 1u)
                    {
                        CopyText(a, sizeof(a), smp.psci_conduit == 1u ? "SMC" : (smp.psci_conduit == 2u ? "HVC" : "none"));
                    }
                    else if (i == 2u)
                    {
                        a[0] = 0;
                        AppendText(a, sizeof(a), "q ");
                        AppendUInt(a, sizeof(a), work.queued);
                        AppendText(a, sizeof(a), " r ");
                        AppendUInt(a, sizeof(a), work.running);
                        AppendText(a, sizeof(a), " d ");
                        AppendUInt(a, sizeof(a), work.completed_total);
                        AppendText(a, sizeof(a), " claim ");
                        AppendUInt(a, sizeof(a), work.claimed_total);
                        AppendText(a, sizeof(a), " can ");
                        AppendUInt(a, sizeof(a), work.cancelled_total);
                    }
                    else if (i == 3u)
                    {
                        a[0] = 0;
                        AppendUInt(a, sizeof(a), cpu.core_count);
                        AppendText(a, sizeof(a), cpu.acpi_madt_found ? " entries" : " unavailable");
                    }
                    SetTextEllipsized(perf_detail_values_[i], a, perf_detail_value_w_ ? perf_detail_value_w_ : 160u);
                }
            }
            else
            {
                const char *labels[8] = {"Input/UI", "Kernel Apps", "Terminal", "SACX Runtime",
                                         "Rendering", "Other", "Frames", "SACX Tasks"};
                for (uint32_t i = 0u; i < 8u; ++i)
                {
                    SetText(perf_detail_labels_[i], labels[i]);
                    if (i < TASK_ACCOUNT_BUCKET_COUNT)
                        FormatPercent(accounting.bucket_ticks[i], total_ticks, a, sizeof(a));
                    else if (i == 6u)
                    {
                        a[0] = 0;
                        AppendUInt(a, sizeof(a), (uint32_t)accounting.frame_count);
                    }
                    else
                    {
                        sacx_task_info tasks[16];
                        a[0] = 0;
                        AppendUInt(a, sizeof(a), sacx_runtime_task_snapshot(tasks, 16u));
                    }
                    SetTextEllipsized(perf_detail_values_[i], a, perf_detail_value_w_ ? perf_detail_value_w_ : 160u);
                }
            }
        }
    }

    void TaskManager::RefreshPerformance()
    {
        pmem_stats stats;
        task_accounting_snapshot accounting;
        char value[TEXT_CAP];
        uint64_t total_ticks = 0u;
        uint64_t total_bytes = 0u;
        uint64_t free_bytes = 0u;
        uint64_t used_bytes = 0u;

        pmem_get_stats(&stats);
        task_accounting_get(&accounting);
        for (uint32_t i = 0u; i < TASK_ACCOUNT_BUCKET_COUNT; ++i)
            total_ticks += accounting.bucket_ticks[i];

        total_bytes = stats.total_pages * stats.page_size;
        free_bytes = stats.free_pages * stats.page_size;
        used_bytes = total_bytes > free_bytes ? total_bytes - free_bytes : 0u;

        uint32_t mem_pct = total_bytes ? (uint32_t)((used_bytes * 100u) / total_bytes) : 0u;
        uint32_t mem_mib = (uint32_t)(used_bytes / (1024u * 1024u));
        uint32_t cpu_pct = 0u;
        uint32_t activity_pct = 0u;
        if (total_ticks)
        {
            cpu_pct = (uint32_t)(((accounting.bucket_ticks[TASK_ACCOUNT_INPUT_UI] +
                                    accounting.bucket_ticks[TASK_ACCOUNT_RENDER]) * 100u) / total_ticks);
            activity_pct = (uint32_t)(((accounting.bucket_ticks[TASK_ACCOUNT_KERNEL_APPS] +
                                         accounting.bucket_ticks[TASK_ACCOUNT_SACX]) * 100u) / total_ticks);
        }
        PushPerfHistory(mem_mib, cpu_pct, activity_pct);

        for (uint32_t i = 0u; i < PERF_TAB_COUNT; ++i)
        {
            kbutton_set_style(perf_tabs_[i].button, i == active_perf_tab_ ? &selected_style_ : &button_style_);
            if (i == PERF_MEMORY)
            {
                FormatBytes(used_bytes, value, sizeof(value));
                AppendText(value, sizeof(value), " used");
            }
            else if (i == PERF_CPU)
                FormatPercent(cpu_pct, 100u, value, sizeof(value));
            else if (i == PERF_ACTIVITY)
                FormatPercent(activity_pct, 100u, value, sizeof(value));
            else if (i == PERF_STORAGE)
                CopyText(value, sizeof(value), "boot volume");
            else
                CopyText(value, sizeof(value), "driver status");
            SetButtonText(perf_tabs_[i], i == PERF_CPU ? "CPU" :
                                       i == PERF_MEMORY ? "Memory" :
                                       i == PERF_STORAGE ? "Disk" :
                                       i == PERF_NETWORK ? "Network" : "Activity");
        }

        RefreshPerformanceLayoutText(total_bytes, used_bytes, free_bytes, stats, accounting, total_ticks);

        uint32_t show_comp = active_perf_tab_ == PERF_MEMORY ? 1u : 0u;
        SetObjectVisible(perf_comp_label_.obj, show_comp);
        SetObjectVisible(perf_comp_bg_, show_comp);
        for (uint32_t i = 0u; i < PERF_COMPOSITION_SEGMENTS; ++i)
            SetObjectVisible(perf_comp_segments_[i], show_comp);

        if (active_perf_tab_ == PERF_MEMORY)
            DrawPerfGraph(perf_mem_history_, PERF_MEMORY, (uint32_t)(total_bytes / (1024u * 1024u)));
        else if (active_perf_tab_ == PERF_CPU)
            DrawPerfGraph(perf_cpu_history_, PERF_CPU, 100u);
        else if (active_perf_tab_ == PERF_ACTIVITY)
            DrawPerfGraph(perf_activity_history_, PERF_ACTIVITY, 100u);
        else
            DrawPerfGraph(perf_activity_history_, active_perf_tab_, 100u);

        kgfx_obj *comp = kgfx_obj_ref(perf_comp_bg_);
        if (show_comp && comp && comp->kind == KGFX_OBJ_RECT)
        {
            uint32_t used_w = total_bytes ? (uint32_t)(((uint64_t)comp->u.rect.w * used_bytes) / total_bytes) : 1u;
            uint32_t free_w = comp->u.rect.w > used_w ? comp->u.rect.w - used_w : 1u;
            kgfx_obj *seg0 = kgfx_obj_ref(perf_comp_segments_[0]);
            kgfx_obj *seg1 = kgfx_obj_ref(perf_comp_segments_[1]);
            kgfx_obj *seg2 = kgfx_obj_ref(perf_comp_segments_[2]);
            if (seg0 && seg0->kind == KGFX_OBJ_RECT)
                seg0->u.rect = {comp->u.rect.x, comp->u.rect.y, used_w, comp->u.rect.h};
            if (seg1 && seg1->kind == KGFX_OBJ_RECT)
                seg1->u.rect = {comp->u.rect.x + (int32_t)used_w, comp->u.rect.y, free_w, comp->u.rect.h};
            if (seg2 && seg2->kind == KGFX_OBJ_RECT)
                seg2->u.rect = {comp->u.rect.x + (int32_t)(comp->u.rect.w > 4u ? comp->u.rect.w - 4u : 0u), comp->u.rect.y, 4u, comp->u.rect.h};
        }
    }

    void TaskManager::RefreshDevices()
    {
        device_inventory_row rows[MAX_DEVICE_ROWS];
        uint32_t count = device_inventory_snapshot(rows, MAX_DEVICE_ROWS);

        for (uint32_t i = 0u; i < MAX_DEVICE_ROWS; ++i)
        {
            if (i < count)
            {
                SetTextEllipsized(device_rows_[i].group, rows[i].group, device_group_w_ ? device_group_w_ - 8u : 80u);
                SetTextEllipsized(device_rows_[i].name, rows[i].name, device_name_w_ ? device_name_w_ - 8u : 140u);
                SetTextEllipsized(device_rows_[i].status, rows[i].status, device_status_w_ ? device_status_w_ - 8u : 80u);
                SetTextWrapped(device_rows_[i].detail, rows[i].detail, device_detail_w_ ? device_detail_w_ : 420u, 2u);
                SetDeviceRowVisible(device_rows_[i], 1u);
            }
            else
            {
                SetDeviceRowVisible(device_rows_[i], 0u);
            }
        }
    }

    void TaskManager::Refresh()
    {
        RefreshApps();
        RefreshPerformance();
        RefreshDevices();
    }

    void TaskManager::Update()
    {
        kgfx_obj *root = 0;

        if (!initialized_)
            return;
        if (kwindow_close_requested(window_))
        {
            kwindow_set_visible(window_, 0u);
            kwindow_close_accept(window_);
        }
        if (kwindow_close_requested(modal_window_))
        {
            HideModal();
            kwindow_close_accept(modal_window_);
        }
        if (!Visible())
            return;

        root = kgfx_obj_ref(root_);
        if (root && root->kind == KGFX_OBJ_RECT && (root->u.rect.w != last_w_ || root->u.rect.h != last_h_))
            Layout();

        if ((++refresh_tick_ % 20u) == 0u)
            Refresh();
    }

    void TaskManager::Activate()
    {
        if (!initialized_ || window_.idx < 0)
            return;
        kwindow_set_visible(window_, 1u);
        (void)kwindow_raise(window_);
        Layout();
        Refresh();
    }

    int TaskManager::Visible() const
    {
        return (initialized_ && window_.idx >= 0) ? kwindow_visible(window_) : 0;
    }

    void TaskManager::FocusKernel(uint32_t kernel_id)
    {
        switch (kernel_id)
        {
        case 1u:
            terminal_activate();
            break;
        case 2u:
            file_explorer_activate();
            break;
        case 3u:
            text_editor_activate();
            break;
        case 5u:
            Activate();
            break;
        default:
            break;
        }
    }

    void TaskManager::PerformKernelTerminate(uint32_t kernel_id)
    {
        switch (kernel_id)
        {
        case 1u:
            terminal_set_visible(0u);
            break;
        case 2u:
            file_explorer_hide();
            break;
        case 3u:
            text_editor_hide();
            break;
        default:
            break;
        }
        Refresh();
    }

    void TaskManager::ShowKernelWarning(uint32_t row_index)
    {
        char body[TEXT_CAP];
        if (row_index >= MAX_APP_ROWS)
            return;
        pending_kernel_row_ = row_index;
        body[0] = 0;
        AppendText(body, sizeof(body), "Terminate kernel app ");
        AppendText(body, sizeof(body), app_rows_[row_index].name.text);
        AppendText(body, sizeof(body), "?");
        SetText(modal_title_, "");
        SetTextWrapped(modal_body_, body, 590u, 3u);
        SetTextWrapped(modal_body_2_, "This hides its window/state but keeps kernel code loaded.", 590u, 3u);
        SetButtonText(modal_confirm_, "Terminate");
        SetButtonText(modal_cancel_, "Cancel");
        SetObjectVisible(kbutton_root(modal_confirm_.button), 1u);
        SetObjectVisible(modal_confirm_.label, 1u);
        kbutton_set_enabled(modal_confirm_.button, 1u);
        kwindow_center_on_parent(modal_window_, window_);
        (void)kwindow_set_modal_child(window_, modal_window_);
        kwindow_set_visible(modal_window_, 1u);
        (void)kwindow_raise(modal_window_);
    }

    void TaskManager::ShowProtectedWarning(const char *name)
    {
        char body[TEXT_CAP];
        pending_kernel_row_ = 0xFFFFFFFFu;
        body[0] = 0;
        AppendText(body, sizeof(body), name ? name : "This system entry");
        AppendText(body, sizeof(body), " is protected and cannot be terminated.");
        SetText(modal_title_, "");
        SetTextWrapped(modal_body_, body, 590u, 3u);
        SetText(modal_body_2_, "");
        SetButtonText(modal_cancel_, "OK");
        SetObjectVisible(kbutton_root(modal_confirm_.button), 0u);
        SetObjectVisible(modal_confirm_.label, 0u);
        kbutton_set_enabled(modal_confirm_.button, 0u);
        kwindow_center_on_parent(modal_window_, window_);
        (void)kwindow_set_modal_child(window_, modal_window_);
        kwindow_set_visible(modal_window_, 1u);
        (void)kwindow_raise(modal_window_);
    }

    void TaskManager::HideModal()
    {
        pending_kernel_row_ = 0xFFFFFFFFu;
        kwindow_set_visible(modal_window_, 0u);
        (void)kwindow_clear_modal_child(window_);
    }

    void TaskManager::HandleButton(Button *button)
    {
        if (!button)
            return;

        if (button->action == ACTION_CANCEL_MODAL)
        {
            HideModal();
            return;
        }
        if (button->action == ACTION_CONFIRM_TERMINATE)
        {
            if (pending_kernel_row_ < MAX_APP_ROWS)
                PerformKernelTerminate(app_rows_[pending_kernel_row_].kernel_id);
            HideModal();
            return;
        }
        if (button->action == ACTION_PERF_SELECT)
        {
            if (button->row < PERF_TAB_COUNT)
            {
                active_perf_tab_ = button->row;
                RefreshPerformance();
            }
            return;
        }

        if (button->row >= MAX_APP_ROWS || !app_rows_[button->row].visible)
            return;

        AppRow &row = app_rows_[button->row];
        if (button->action == ACTION_FOCUS)
        {
            if (row.row_type == ROW_KERNEL || row.row_type == ROW_PROTECTED)
                FocusKernel(row.kernel_id);
            return;
        }
        if (button->action == ACTION_RELEASE && row.row_type == ROW_SACX)
        {
            (void)sacx_runtime_task_release(row.task_id);
            Refresh();
            return;
        }
        if (button->action == ACTION_TERMINATE)
        {
            if (row.row_type == ROW_SACX)
            {
                (void)sacx_runtime_task_cancel(row.task_id, -1, "terminated by task manager");
                Refresh();
            }
            else if (row.row_type == ROW_KERNEL)
            {
                ShowKernelWarning(button->row);
            }
            else if (row.row_type == ROW_PROTECTED)
            {
                ShowProtectedWarning(row.name.text);
            }
        }
    }

    void TaskManager::NavChanged(uint32_t widget, int32_t value, void *user)
    {
        TaskManager *self = (TaskManager *)user;
        (void)widget;
        if (!self || value < 0 || value > 2)
            return;
        self->active_view_ = (uint32_t)value;
        (void)kui_view_set_state(self->main_view_, self->active_view_);
        self->Refresh();
    }

    void TaskManager::ButtonClicked(kbutton_handle button, void *user)
    {
        Button *info = (Button *)user;
        (void)button;
        g_task_manager.HandleButton(info);
    }
}

extern "C" void task_manager_init(const kfont *font)
{
    g_task_manager.Init(font);
}

extern "C" void task_manager_update(void)
{
    g_task_manager.Update();
}

extern "C" void task_manager_activate(void)
{
    g_task_manager.Activate();
}

extern "C" int task_manager_visible(void)
{
    return g_task_manager.Visible();
}
