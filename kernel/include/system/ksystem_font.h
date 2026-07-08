#pragma once
#include <stdint.h>
#include "kwrappers/ktext.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int ksystem_font_init_fallback(kfont *out);
    int ksystem_font_load_system_file(const char *path, kfont *out, void **out_blob, uint32_t *out_size);

#ifdef __cplusplus
}
#endif
