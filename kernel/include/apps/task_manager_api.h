#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kfont kfont;

    void task_manager_init(const kfont *font);
    void task_manager_update(void);
    void task_manager_activate(void);
    int task_manager_visible(void);

#ifdef __cplusplus
}
#endif
