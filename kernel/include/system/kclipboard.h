#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t kclipboard_set_text(const char *text, uint32_t len);
uint32_t kclipboard_copy_text(char *out, uint32_t cap);
uint32_t kclipboard_length(void);

#ifdef __cplusplus
}
#endif

