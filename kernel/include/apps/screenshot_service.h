#pragma once

#include <stdint.h>
#include "kwrappers/kgfx.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct kfont;

void screenshot_service_init(const struct kfont *font);
int screenshot_service_update(void);
uint32_t screenshot_service_collect_gpu_foreground_roots(
    kgfx_obj_handle *out_roots, uint32_t capacity);

#ifdef __cplusplus
}
#endif
