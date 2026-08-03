#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "kwrappers/ktext.h"

    enum
    {
        TERMINAL_OPEN_FLAG_NONE = 0u,
        TERMINAL_OPEN_FLAG_NO_WINDOW = 1u << 0,
    };

    typedef void (*terminal_capture_sink_fn)(const char *text, uint32_t len, void *user);

    void terminal_initialize(kfont *font);
    void terminal_clear(void);
    void terminal_clear_no_flush(void);
    void terminal_flush_log(void);

    void terminal_print(const char *s);
    void terminal_print_inline(const char *s);
    void terminal_warn(const char *s);
    void terminal_error(const char *s);
    void terminal_success(const char *s);
    void terminal_update_input(void);

    void terminal_print_hex64(uint64_t v);
    void terminal_print_hex32(uint32_t v);
    void terminal_print_hex8(uint32_t v);
    void terminal_print_inline_hex64(uint64_t v);
    void terminal_print_inline_hex32(uint32_t v);
    void terminal_print_inline_hex8(uint32_t v);

    void terminal_toggle_quiet();
    void terminal_set_quiet();
    void terminal_set_loud();
    void terminal_activate(void);
    void terminal_set_visible(uint32_t visible);
    int terminal_visible(void);
    int terminal_open_script_ex(const char *raw_path, const char *friendly_path, uint32_t flags);
    int terminal_open_program_ex(const char *raw_path, const char *friendly_path, uint32_t flags);
    int terminal_open_program_with_arg_ex(const char *raw_path, const char *friendly_path,
                                          const char *arg_raw_path, const char *arg_friendly_path,
                                          uint32_t flags);
    int terminal_open_script(const char *raw_path, const char *friendly_path);
    int terminal_open_program(const char *raw_path, const char *friendly_path);
    int terminal_open_program_with_arg(const char *raw_path, const char *friendly_path,
                                       const char *arg_raw_path, const char *arg_friendly_path);
    void terminal_capture_begin(uint8_t mirror_to_terminal, terminal_capture_sink_fn sink, void *user);
    void terminal_capture_end(void);

    enum
    {
        TERMINAL_VISUAL_PROGRESS_CLASSIC = 0u,
        TERMINAL_VISUAL_PROGRESS_BLOCKS = 1u,
    };

    enum
    {
        TERMINAL_VISUAL_FLAG_INLINE = 0u,
        TERMINAL_VISUAL_FLAG_FULLSCREEN = 1u << 0,
    };

    void terminal_visual_begin(const char *title);
    void terminal_visual_begin_ex(const char *title, uint32_t flags, uint32_t rows);
    void terminal_visual_end(void);
    void terminal_visual_clear(void);
    void terminal_visual_put(int32_t x, int32_t y, char ch, kcolor fg, kcolor bg);
    void terminal_visual_text(int32_t x, int32_t y, const char *text, kcolor fg, kcolor bg);
    void terminal_visual_progress(uint32_t id, int32_t x, int32_t y, int32_t w,
                                  uint32_t percent, uint32_t style);
    void terminal_visual_spinner(uint32_t id, int32_t x, int32_t y, uint32_t frame);
    void terminal_visual_present(void);
    int32_t terminal_visual_cols(void);
    int32_t terminal_visual_rows(void);
    int terminal_demo_installfx_start(uint32_t flags);

#ifdef __cplusplus
}
#endif
