#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;

    void file_explorer_init(const kfont *font);
    void file_explorer_update(void);
    void file_explorer_activate(void);
    int file_explorer_visible(void);

#ifdef __cplusplus
}
#endif
