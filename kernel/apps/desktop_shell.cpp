#include "apps/desktop_shell_api.h"
#include "apps/file_explorer_api.h"
#include "apps/text_editor_api.h"
#include "terminal/terminal_api.h"

extern "C"
{
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/ktext.h"
#include "kwrappers/string.h"
}

namespace
{
    static const char *kWallpaperPath = "0:/OS/System/Images/bgpaper.bmp";

    static kcolor rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        kcolor c = {r, g, b};
        return c;
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

    static int load_first_bmp(kimg *out, const char *a, const char *b, const char *c)
    {
        if (out && a && kimg_load_bmp_flags(out, a, KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) == 0)
            return 0;
        if (out && b && kimg_load_bmp_flags(out, b, KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) == 0)
            return 0;
        if (out && c && kimg_load_bmp_flags(out, c, KIMG_BMP_FLAG_MAGENTA_TRANSPARENT) == 0)
            return 0;
        return -1;
    }

    class DesktopShell
    {
    public:
        void Init(const kfont *font);
        void Update();

    private:
        struct AppButton
        {
            kbutton_handle button;
            kgfx_obj_handle icon_image;
            kgfx_obj_handle icon_fallback;
            kgfx_obj_handle label;
            kimg icon;
            uint8_t icon_loaded;
        };

        void CreateWallpaper(void);
        void CreateTaskbar(void);
        void Layout(void);
        void LayoutButton(AppButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h,
                          const char *label_text, const char *fallback_text);
        void UpdateButtonStyles(void);
        void SetObjectVisible(kgfx_obj_handle handle, uint8_t visible);

        static void TerminalClickThunk(kbutton_handle button, void *user);
        static void ExplorerClickThunk(kbutton_handle button, void *user);
        static void EditorClickThunk(kbutton_handle button, void *user);

    private:
        uint8_t initialized_;
        const kfont *font_;
        uint32_t last_screen_w_;
        uint32_t last_screen_h_;
        kgfx_obj_handle wallpaper_frame_;
        kgfx_obj_handle wallpaper_image_;
        kimg wallpaper_;
        uint8_t wallpaper_loaded_;
        kgfx_obj_handle taskbar_;
        AppButton terminal_button_;
        AppButton explorer_button_;
        AppButton editor_button_;
        uint8_t terminal_active_;
        uint8_t explorer_active_;
        uint8_t editor_active_;
        kbutton_style button_style_;
        kbutton_style active_style_;
    };

    static DesktopShell g_desktop_shell = {};

    void DesktopShell::Init(const kfont *font)
    {
        if (initialized_)
            return;

        font_ = font;
        last_screen_w_ = 0u;
        last_screen_h_ = 0u;
        wallpaper_frame_.idx = -1;
        wallpaper_image_.idx = -1;
        wallpaper_loaded_ = 0u;
        kimg_zero(&wallpaper_);

        terminal_button_.button.idx = -1;
        terminal_button_.icon_image.idx = -1;
        terminal_button_.icon_fallback.idx = -1;
        terminal_button_.label.idx = -1;
        terminal_button_.icon_loaded = 0u;
        kimg_zero(&terminal_button_.icon);

        explorer_button_.button.idx = -1;
        explorer_button_.icon_image.idx = -1;
        explorer_button_.icon_fallback.idx = -1;
        explorer_button_.label.idx = -1;
        explorer_button_.icon_loaded = 0u;
        kimg_zero(&explorer_button_.icon);
        editor_button_.button.idx = -1;
        editor_button_.icon_image.idx = -1;
        editor_button_.icon_fallback.idx = -1;
        editor_button_.label.idx = -1;
        editor_button_.icon_loaded = 0u;
        kimg_zero(&editor_button_.icon);
        terminal_active_ = 0xFFu;
        explorer_active_ = 0xFFu;
        editor_active_ = 0xFFu;

        button_style_ = kbutton_style_default();
        button_style_.fill = rgb(28, 33, 45);
        button_style_.hover_fill = rgb(44, 54, 72);
        button_style_.pressed_fill = rgb(58, 74, 98);
        button_style_.outline = rgb(102, 120, 156);
        button_style_.outline_alpha = 255u;
        button_style_.outline_width = 1u;
        button_style_.alpha = 220u;

        active_style_ = button_style_;
        active_style_.fill = rgb(44, 74, 120);
        active_style_.hover_fill = rgb(60, 92, 146);
        active_style_.pressed_fill = rgb(76, 110, 168);
        active_style_.outline = rgb(155, 208, 255);

        CreateWallpaper();
        CreateTaskbar();
        Layout();
        UpdateButtonStyles();
        initialized_ = 1u;
    }

    void DesktopShell::CreateWallpaper(void)
    {
        wallpaper_frame_ = kgfx_obj_add_rect(0, 0, 1, 1, -200, rgb(10, 13, 18), 1);
        kgfx_obj_ref(wallpaper_frame_)->outline_width = 0u;
        kgfx_obj_ref(wallpaper_frame_)->alpha = 255u;

        if (kimg_load_bmp(&wallpaper_, kWallpaperPath) == 0)
        {
            wallpaper_loaded_ = 1u;
            wallpaper_image_ = kgfx_obj_add_image(wallpaper_.px, wallpaper_.w, wallpaper_.h, 0, 0, wallpaper_.w);
            kgfx_obj_set_parent(wallpaper_image_, wallpaper_frame_);
            kgfx_obj_set_clip_to_parent(wallpaper_image_, 1u);
            kgfx_image_set_sample_mode(wallpaper_image_, KGFX_IMAGE_SAMPLE_BILINEAR);
            kgfx_obj_ref(wallpaper_image_)->z = 1;
        }
    }

    void DesktopShell::CreateTaskbar(void)
    {
        taskbar_ = kgfx_obj_add_rect(0, 0, 1, 1, 180, rgb(16, 20, 29), 1);
        kgfx_obj_ref(taskbar_)->alpha = 228u;
        kgfx_obj_ref(taskbar_)->outline = rgb(90, 106, 137);
        kgfx_obj_ref(taskbar_)->outline_width = 1u;

        terminal_button_.button = kbutton_add_rect(0, 0, 72, 48, 181, &button_style_, TerminalClickThunk, this);
        explorer_button_.button = kbutton_add_rect(0, 0, 72, 48, 181, &button_style_, ExplorerClickThunk, this);
        editor_button_.button = kbutton_add_rect(0, 0, 72, 48, 181, &button_style_, EditorClickThunk, this);

        kgfx_obj_set_parent(kbutton_root(terminal_button_.button), taskbar_);
        kgfx_obj_set_parent(kbutton_root(explorer_button_.button), taskbar_);
        kgfx_obj_set_parent(kbutton_root(editor_button_.button), taskbar_);
        kgfx_obj_set_clip_to_parent(kbutton_root(terminal_button_.button), 1u);
        kgfx_obj_set_clip_to_parent(kbutton_root(explorer_button_.button), 1u);
        kgfx_obj_set_clip_to_parent(kbutton_root(editor_button_.button), 1u);

        if (font_)
        {
            terminal_button_.icon_fallback = kgfx_obj_add_text(font_, ">", 0, 0, 1, rgb(224, 238, 248), 255, 2u, 0, 0, KTEXT_ALIGN_CENTER, 1);
            terminal_button_.label = kgfx_obj_add_text(font_, "Terminal", 0, 0, 1, rgb(228, 236, 245), 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
            explorer_button_.icon_fallback = kgfx_obj_add_text(font_, "F", 0, 0, 1, rgb(224, 238, 248), 255, 2u, 0, 0, KTEXT_ALIGN_CENTER, 1);
            explorer_button_.label = kgfx_obj_add_text(font_, "Files", 0, 0, 1, rgb(228, 236, 245), 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);
            editor_button_.icon_fallback = kgfx_obj_add_text(font_, "N", 0, 0, 1, rgb(224, 238, 248), 255, 2u, 0, 0, KTEXT_ALIGN_CENTER, 1);
            editor_button_.label = kgfx_obj_add_text(font_, "Notes", 0, 0, 1, rgb(228, 236, 245), 255, 1u, 0, 0, KTEXT_ALIGN_CENTER, 1);

            kgfx_obj_set_parent(terminal_button_.icon_fallback, kbutton_root(terminal_button_.button));
            kgfx_obj_set_parent(terminal_button_.label, kbutton_root(terminal_button_.button));
            kgfx_obj_set_parent(explorer_button_.icon_fallback, kbutton_root(explorer_button_.button));
            kgfx_obj_set_parent(explorer_button_.label, kbutton_root(explorer_button_.button));
            kgfx_obj_set_parent(editor_button_.icon_fallback, kbutton_root(editor_button_.button));
            kgfx_obj_set_parent(editor_button_.label, kbutton_root(editor_button_.button));
            kgfx_obj_set_clip_to_parent(terminal_button_.icon_fallback, 1u);
            kgfx_obj_set_clip_to_parent(terminal_button_.label, 1u);
            kgfx_obj_set_clip_to_parent(explorer_button_.icon_fallback, 1u);
            kgfx_obj_set_clip_to_parent(explorer_button_.label, 1u);
            kgfx_obj_set_clip_to_parent(editor_button_.icon_fallback, 1u);
            kgfx_obj_set_clip_to_parent(editor_button_.label, 1u);
        }

        if (load_first_bmp(&terminal_button_.icon,
                           "0:/OS/System/Images/Taskbar/terminal.bmp",
                           "0:/OS/System/Images/Apps/terminal.bmp",
                           0) == 0)
        {
            terminal_button_.icon_loaded = 1u;
            terminal_button_.icon_image = kgfx_obj_add_image(terminal_button_.icon.px, terminal_button_.icon.w, terminal_button_.icon.h, 0, 0, terminal_button_.icon.w);
            kgfx_obj_set_parent(terminal_button_.icon_image, kbutton_root(terminal_button_.button));
            kgfx_obj_set_clip_to_parent(terminal_button_.icon_image, 1u);
            kgfx_image_set_sample_mode(terminal_button_.icon_image, KGFX_IMAGE_SAMPLE_NEAREST);
            kgfx_obj_ref(terminal_button_.icon_image)->z = 1;
        }

        if (load_first_bmp(&explorer_button_.icon,
                           "0:/OS/System/Images/Taskbar/file_explorer.bmp",
                           "0:/OS/System/Images/Taskbar/explorer.bmp",
                           "0:/OS/System/Images/Apps/file_explorer.bmp") == 0)
        {
            explorer_button_.icon_loaded = 1u;
            explorer_button_.icon_image = kgfx_obj_add_image(explorer_button_.icon.px, explorer_button_.icon.w, explorer_button_.icon.h, 0, 0, explorer_button_.icon.w);
            kgfx_obj_set_parent(explorer_button_.icon_image, kbutton_root(explorer_button_.button));
            kgfx_obj_set_clip_to_parent(explorer_button_.icon_image, 1u);
            kgfx_image_set_sample_mode(explorer_button_.icon_image, KGFX_IMAGE_SAMPLE_NEAREST);
            kgfx_obj_ref(explorer_button_.icon_image)->z = 1;
        }

        if (load_first_bmp(&editor_button_.icon,
                           "0:/OS/System/Images/Taskbar/text_editor.bmp",
                           "0:/OS/System/Images/Taskbar/notepad.bmp",
                           "0:/OS/System/Images/Apps/text_editor.bmp") == 0)
        {
            editor_button_.icon_loaded = 1u;
            editor_button_.icon_image = kgfx_obj_add_image(editor_button_.icon.px, editor_button_.icon.w, editor_button_.icon.h, 0, 0, editor_button_.icon.w);
            kgfx_obj_set_parent(editor_button_.icon_image, kbutton_root(editor_button_.button));
            kgfx_obj_set_clip_to_parent(editor_button_.icon_image, 1u);
            kgfx_image_set_sample_mode(editor_button_.icon_image, KGFX_IMAGE_SAMPLE_NEAREST);
            kgfx_obj_ref(editor_button_.icon_image)->z = 1;
        }
    }

    void DesktopShell::SetObjectVisible(kgfx_obj_handle handle, uint8_t visible)
    {
        kgfx_obj *obj = kgfx_obj_ref(handle);
        if (obj)
            obj->visible = visible ? 1u : 0u;
    }

    void DesktopShell::LayoutButton(AppButton &button, int32_t x, int32_t y, uint32_t w, uint32_t h,
                                    const char *label_text, const char *fallback_text)
    {
        kgfx_obj *root = kgfx_obj_ref(kbutton_root(button.button));
        uint32_t label_h = (font_ && button.label.idx >= 0) ? ktext_scale_mul_px(font_->h, 1u) : 0u;
        uint32_t fallback_h = (font_ && button.icon_fallback.idx >= 0) ? ktext_scale_mul_px(font_->h, 2u) : 0u;
        int32_t icon_area_top = 6;
        int32_t icon_area_bottom = (int32_t)h - 6 - (int32_t)label_h;
        int32_t icon_area_h = icon_area_bottom - icon_area_top;

        (void)label_text;
        (void)fallback_text;

        if (!root || root->kind != KGFX_OBJ_RECT)
            return;

        root->u.rect.x = x;
        root->u.rect.y = y;
        root->u.rect.w = w;
        root->u.rect.h = h;

        if (button.label.idx >= 0 && font_)
        {
            kgfx_obj *label = kgfx_obj_ref(button.label);
            if (label && label->kind == KGFX_OBJ_TEXT)
            {
                label->u.text.x = (int32_t)w / 2;
                label->u.text.y = (int32_t)h - (int32_t)label_h - 5;
            }
        }

        if (button.icon_loaded && button.icon_image.idx >= 0)
        {
            kgfx_obj *image = kgfx_obj_ref(button.icon_image);
            uint32_t draw_w = button.icon.w;
            uint32_t draw_h = button.icon.h;
            uint32_t max_w = (w > 16u) ? (w - 16u) : w;
            uint32_t max_h = (icon_area_h > 4) ? (uint32_t)(icon_area_h - 4) : (uint32_t)icon_area_h;

            if (image && image->kind == KGFX_OBJ_IMAGE && button.icon.w > 0u && button.icon.h > 0u)
            {
                if (draw_w > max_w || draw_h > max_h)
                {
                    uint64_t scale_w = max_w ? ((uint64_t)max_w * 1000ull) / (uint64_t)button.icon.w : 1000ull;
                    uint64_t scale_h = max_h ? ((uint64_t)max_h * 1000ull) / (uint64_t)button.icon.h : 1000ull;
                    uint64_t scale = scale_w < scale_h ? scale_w : scale_h;

                    if (scale == 0ull)
                        scale = 1ull;

                    draw_w = (uint32_t)(((uint64_t)button.icon.w * scale) / 1000ull);
                    draw_h = (uint32_t)(((uint64_t)button.icon.h * scale) / 1000ull);
                    if (draw_w == 0u)
                        draw_w = 1u;
                    if (draw_h == 0u)
                        draw_h = 1u;
                }

                image->u.image.w = draw_w;
                image->u.image.h = draw_h;
                image->u.image.x = ((int32_t)w - (int32_t)draw_w) / 2;
                image->u.image.y = icon_area_top + (icon_area_h - (int32_t)draw_h) / 2;
            }
        }

        if (button.icon_fallback.idx >= 0 && font_)
        {
            kgfx_obj *fallback = kgfx_obj_ref(button.icon_fallback);
            if (fallback && fallback->kind == KGFX_OBJ_TEXT)
            {
                fallback->u.text.x = (int32_t)w / 2;
                fallback->u.text.y = icon_area_top + (icon_area_h - (int32_t)fallback_h) / 2;
            }
        }

        SetObjectVisible(button.icon_image, button.icon_loaded ? 1u : 0u);
        SetObjectVisible(button.icon_fallback, button.icon_loaded ? 0u : 1u);
    }

    void DesktopShell::Layout(void)
    {
        const kfb *fb = kgfx_info();
        uint32_t screen_w = 0u;
        uint32_t screen_h = 0u;
        uint32_t bar_h = 58u;
        uint32_t button_w = 88u;
        uint32_t button_h = 46u;
        int32_t button_y = 6;
        int32_t gap = 10;

        if (!fb)
            return;

        screen_w = fb->width;
        screen_h = fb->height;
        last_screen_w_ = screen_w;
        last_screen_h_ = screen_h;

        if (kgfx_obj_ref(wallpaper_frame_) && kgfx_obj_ref(wallpaper_frame_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(wallpaper_frame_)->u.rect.x = 0;
            kgfx_obj_ref(wallpaper_frame_)->u.rect.y = 0;
            kgfx_obj_ref(wallpaper_frame_)->u.rect.w = screen_w;
            kgfx_obj_ref(wallpaper_frame_)->u.rect.h = screen_h;
        }

        if (wallpaper_loaded_ && wallpaper_image_.idx >= 0)
        {
            kgfx_obj *image = kgfx_obj_ref(wallpaper_image_);
            uint32_t draw_w = screen_w;
            uint32_t draw_h = screen_h;

            if (image && image->kind == KGFX_OBJ_IMAGE && wallpaper_.w > 0u && wallpaper_.h > 0u)
            {
                uint64_t scale_w = ((uint64_t)screen_w * 1000ull) / (uint64_t)wallpaper_.w;
                uint64_t scale_h = ((uint64_t)screen_h * 1000ull) / (uint64_t)wallpaper_.h;
                uint64_t scale = scale_w > scale_h ? scale_w : scale_h;

                draw_w = (uint32_t)(((uint64_t)wallpaper_.w * scale) / 1000ull);
                draw_h = (uint32_t)(((uint64_t)wallpaper_.h * scale) / 1000ull);
                if (draw_w == 0u)
                    draw_w = 1u;
                if (draw_h == 0u)
                    draw_h = 1u;

                image->u.image.w = draw_w;
                image->u.image.h = draw_h;
                image->u.image.x = ((int32_t)screen_w - (int32_t)draw_w) / 2;
                image->u.image.y = ((int32_t)screen_h - (int32_t)draw_h) / 2;
            }
        }

        if (kgfx_obj_ref(taskbar_) && kgfx_obj_ref(taskbar_)->kind == KGFX_OBJ_RECT)
        {
            kgfx_obj_ref(taskbar_)->u.rect.x = 0;
            kgfx_obj_ref(taskbar_)->u.rect.y = (int32_t)screen_h - (int32_t)bar_h;
            kgfx_obj_ref(taskbar_)->u.rect.w = screen_w;
            kgfx_obj_ref(taskbar_)->u.rect.h = bar_h;
        }

        LayoutButton(terminal_button_, 10, button_y, button_w, button_h, "Terminal", ">");
        LayoutButton(explorer_button_, 10 + (int32_t)button_w + gap, button_y, button_w, button_h, "Files", "F");
        LayoutButton(editor_button_, 10 + (int32_t)(button_w + gap) * 2, button_y, button_w, button_h, "Notes", "N");
    }

    void DesktopShell::UpdateButtonStyles(void)
    {
        uint8_t terminal_active = terminal_visible() ? 1u : 0u;
        uint8_t explorer_active = file_explorer_visible() ? 1u : 0u;
        uint8_t editor_active = text_editor_visible() ? 1u : 0u;

        if (terminal_active_ != terminal_active)
        {
            terminal_active_ = terminal_active;
            kbutton_set_style(terminal_button_.button, terminal_active ? &active_style_ : &button_style_);
        }

        if (explorer_active_ != explorer_active)
        {
            explorer_active_ = explorer_active;
            kbutton_set_style(explorer_button_.button, explorer_active ? &active_style_ : &button_style_);
        }

        if (editor_active_ != editor_active)
        {
            editor_active_ = editor_active;
            kbutton_set_style(editor_button_.button, editor_active ? &active_style_ : &button_style_);
        }
    }

    void DesktopShell::Update()
    {
        const kfb *fb = kgfx_info();

        if (!initialized_ || !fb)
            return;

        if (fb->width != last_screen_w_ || fb->height != last_screen_h_)
            Layout();

        UpdateButtonStyles();
    }

    void DesktopShell::TerminalClickThunk(kbutton_handle button, void *user)
    {
        (void)button;
        (void)user;
        terminal_activate();
    }

    void DesktopShell::ExplorerClickThunk(kbutton_handle button, void *user)
    {
        (void)button;
        (void)user;
        file_explorer_activate();
    }

    void DesktopShell::EditorClickThunk(kbutton_handle button, void *user)
    {
        (void)button;
        (void)user;
        text_editor_activate();
    }
}

extern "C" void desktop_shell_init(const kfont *font)
{
    g_desktop_shell.Init(font);
}

extern "C" void desktop_shell_update(void)
{
    g_desktop_shell.Update();
}
