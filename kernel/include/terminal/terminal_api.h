#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "kwrappers/ktext.h"

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
    void terminal_capture_begin(uint8_t mirror_to_terminal, terminal_capture_sink_fn sink, void *user);
    void terminal_capture_end(void);

#ifdef __cplusplus
}
#endif
