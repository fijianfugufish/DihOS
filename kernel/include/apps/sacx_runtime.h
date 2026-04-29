#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct kfont;

    typedef void (*sacx_runtime_print_fn)(const char *text, void *user);
    typedef void (*sacx_runtime_console_visible_fn)(uint8_t visible, void *user);

    typedef struct sacx_runtime_io
    {
        sacx_runtime_print_fn print;
        sacx_runtime_console_visible_fn set_console_visible;
        void *user;
    } sacx_runtime_io;

    enum
    {
        SACX_TASK_UNUSED = 0,
        SACX_TASK_READY = 1,
        SACX_TASK_SLEEPING = 2,
        SACX_TASK_EXITED = 3,
        SACX_TASK_FAULTED = 4,
    };

    typedef struct sacx_task_status
    {
        uint32_t task_id;
        uint32_t state;
        int32_t exit_status;
        uint64_t wake_tick;
        char message[128];
    } sacx_task_status;

    int sacx_runtime_init(const struct kfont *font);
    void sacx_runtime_set_font(const struct kfont *font);
    void sacx_runtime_update(void);

    int sacx_runtime_launch(const char *raw_path,
                            const char *friendly_path,
                            const sacx_runtime_io *io,
                            uint32_t *out_task_id);
    int sacx_runtime_launch_ex(const char *raw_path,
                               const char *friendly_path,
                               const char *arg_raw_path,
                               const char *arg_friendly_path,
                               const sacx_runtime_io *io,
                               uint32_t *out_task_id);

    int sacx_runtime_task_status(uint32_t task_id, sacx_task_status *out_status);
    int sacx_runtime_task_release(uint32_t task_id);

#ifdef __cplusplus
}
#endif
