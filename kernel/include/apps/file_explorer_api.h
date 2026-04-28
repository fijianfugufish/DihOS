#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;
    typedef enum file_explorer_dialog_mode
    {
        FILE_EXPLORER_DIALOG_NONE = 0,
        FILE_EXPLORER_DIALOG_OPEN_FILE = 1,
        FILE_EXPLORER_DIALOG_SAVE_FILE = 2
    } file_explorer_dialog_mode;

    typedef void (*file_explorer_dialog_callback)(int accepted,
                                                  const char *raw_path,
                                                  const char *friendly_path,
                                                  void *user);

    void file_explorer_init(const kfont *font);
    void file_explorer_update(void);
    void file_explorer_activate(void);
    int file_explorer_visible(void);
    int file_explorer_begin_dialog(file_explorer_dialog_mode mode,
                                   const char *initial_dir,
                                   const char *suggested_name,
                                   file_explorer_dialog_callback on_result,
                                   void *user);
    int file_explorer_dialog_active(void);

#ifdef __cplusplus
}
#endif
