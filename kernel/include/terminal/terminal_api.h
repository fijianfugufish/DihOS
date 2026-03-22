#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "kwrappers/ktext.h"

    void terminal_initialize(kfont *font);
    void terminal_clear(void);
    void terminal_clear_no_flush(void);
    void terminal_flush_log(void);

    void terminal_print(const char *s);
    void terminal_print_inline(const char *s);
    void terminal_warn(const char *s);
    void terminal_error(const char *s);
    void terminal_success(const char *s);

    void terminal_print_hex64(uint64_t v);
    void terminal_print_hex32(uint32_t v);
    void terminal_print_hex8(uint32_t v);
    void terminal_print_inline_hex64(uint64_t v);
    void terminal_print_inline_hex32(uint32_t v);
    void terminal_print_inline_hex8(uint32_t v);

#ifdef __cplusplus
}
#endif