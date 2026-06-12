#pragma once

#include "kwrappers/kimg.h"

#ifdef __cplusplus
extern "C"
{
#endif

int kimage_clipboard_set(const kimg *image);
int kimage_clipboard_copy(kimg *out);
int kimage_clipboard_has_image(void);
void kimage_clipboard_clear(void);

#ifdef __cplusplus
}
#endif
