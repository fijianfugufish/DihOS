#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;

    void desktop_shell_init(const kfont *font);
    void desktop_shell_update(void);

#ifdef __cplusplus
}
#endif
