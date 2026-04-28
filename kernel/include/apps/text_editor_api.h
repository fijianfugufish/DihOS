#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;

    void text_editor_init(const kfont *font);
    void text_editor_update(void);
    void text_editor_activate(void);
    int text_editor_visible(void);
    int text_editor_open_path(const char *raw_path, const char *friendly_path);

#ifdef __cplusplus
}
#endif
