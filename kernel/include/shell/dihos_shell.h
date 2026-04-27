#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DIHOS_SHELL_LINE_CAP 256u
#define DIHOS_SHELL_PATH_CAP 256u
#define DIHOS_SHELL_PROMPT_CAP 320u

    void dihos_shell_init(void);
    int dihos_shell_execute_line(const char *line);
    const char *dihos_shell_prompt(void);
    int dihos_shell_history_prev(const char *current_text, char *out, uint32_t out_cap);
    int dihos_shell_history_next(const char *current_text, char *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif
