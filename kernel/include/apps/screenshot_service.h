#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

struct kfont;

void screenshot_service_init(const struct kfont *font);
int screenshot_service_update(void);

#ifdef __cplusplus
}
#endif
