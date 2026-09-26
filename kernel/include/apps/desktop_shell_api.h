#pragma once

#include <stdint.h>
#include "kwrappers/kgfx.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;

    void desktop_shell_init(const kfont *font);
    void desktop_shell_update(void);
    void desktop_shell_set_fps_visible(uint8_t visible);
    uint8_t desktop_shell_fps_visible(void);
    uint32_t desktop_shell_collect_gpu_foreground_roots(
        kgfx_obj_handle *out_roots, uint32_t capacity);

#ifdef __cplusplus
}
#endif
