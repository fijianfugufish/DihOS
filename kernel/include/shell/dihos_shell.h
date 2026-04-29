#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DIHOS_SHELL_LINE_CAP 256u
#define DIHOS_SHELL_PATH_CAP 256u
#define DIHOS_SHELL_PROMPT_CAP 320u
#define DIHOS_SHELL_HISTORY_MAX 32u
#define DIHOS_SHELL_HISTORY_ENTRY_CAP DIHOS_SHELL_LINE_CAP
#define DIHOS_SHELL_CAPTURE_CAP 65536u

#define DIHOS_SCRIPT_MAX_LINES 256u
#define DIHOS_SCRIPT_LINE_CAP DIHOS_SHELL_LINE_CAP
#define DIHOS_SCRIPT_MAX_VARS 32u
#define DIHOS_SCRIPT_VAR_NAME_CAP 32u
#define DIHOS_SCRIPT_VAR_VALUE_CAP 256u
#define DIHOS_SCRIPT_MAX_LABELS 64u
#define DIHOS_SCRIPT_LABEL_CAP 32u
#define DIHOS_SCRIPT_MAX_ARGS 16u
#define DIHOS_SCRIPT_MAX_FUNCS 32u
#define DIHOS_SCRIPT_FUNC_NAME_CAP 32u
#define DIHOS_SCRIPT_FUNC_MAX_ARGS 8u
#define DIHOS_SCRIPT_MAX_CALL_DEPTH 8u
#define DIHOS_SCRIPT_MAX_LOCAL_VARS 32u
#define DIHOS_SCRIPT_MAX_LOOPS 16u
#define DIHOS_SCRIPT_STEP_BUDGET 24u
#define DIHOS_SCRIPT_INSTRUCTION_LIMIT 10000u

    typedef void (*dihos_shell_text_fn)(const char *text, void *user);
    typedef void (*dihos_shell_clear_fn)(void *user);
    typedef void (*dihos_shell_capture_sink_fn)(const char *text, uint32_t len, void *user);

    typedef struct dihos_shell_io
    {
        dihos_shell_text_fn print;
        dihos_shell_text_fn print_inline;
        dihos_shell_text_fn warn;
        dihos_shell_text_fn error;
        dihos_shell_text_fn success;
        dihos_shell_clear_fn clear;
        void *user;
    } dihos_shell_io;

    typedef struct dihos_shell_session
    {
        char cwd[DIHOS_SHELL_PATH_CAP];
        char prompt[DIHOS_SHELL_PROMPT_CAP];
        char history[DIHOS_SHELL_HISTORY_MAX][DIHOS_SHELL_HISTORY_ENTRY_CAP];
        char history_draft[DIHOS_SHELL_HISTORY_ENTRY_CAP];
        uint8_t history_count;
        int history_browse_index;
        dihos_shell_io io;
        dihos_shell_capture_sink_fn capture_sink;
        void *capture_user;
        uint8_t capture_mirror;
        char pipe_buffers[2][DIHOS_SHELL_CAPTURE_CAP];
        char run_status_text[DIHOS_SCRIPT_VAR_VALUE_CAP];
    } dihos_shell_session;

    typedef struct dihos_script_var
    {
        char name[DIHOS_SCRIPT_VAR_NAME_CAP];
        char value[DIHOS_SCRIPT_VAR_VALUE_CAP];
    } dihos_script_var;

    typedef struct dihos_script_label
    {
        char name[DIHOS_SCRIPT_LABEL_CAP];
        uint32_t line_index;
    } dihos_script_label;

    typedef struct dihos_script_function
    {
        char name[DIHOS_SCRIPT_FUNC_NAME_CAP];
        char arg_names[DIHOS_SCRIPT_FUNC_MAX_ARGS][DIHOS_SCRIPT_VAR_NAME_CAP];
        uint32_t entry_line;
        uint32_t end_line;
        uint32_t arg_count;
    } dihos_script_function;

    typedef struct dihos_script_call_frame
    {
        dihos_script_var vars[DIHOS_SCRIPT_MAX_LOCAL_VARS];
        uint32_t var_count;
        uint32_t return_pc;
        uint32_t function_index;
        uint32_t loop_depth_base;
        char return_target[DIHOS_SCRIPT_VAR_NAME_CAP];
    } dihos_script_call_frame;

    typedef struct dihos_script_loop_frame
    {
        uint8_t type;
        uint32_t start_line;
        uint32_t end_line;
        char for_var[DIHOS_SCRIPT_VAR_NAME_CAP];
        double for_current;
        double for_end;
        double for_step;
        uint8_t for_initialized;
    } dihos_script_loop_frame;

    typedef struct dihos_script_runner
    {
        dihos_shell_session *session;
        char raw_path[DIHOS_SHELL_PATH_CAP];
        char friendly_path[DIHOS_SHELL_PATH_CAP];
        char script_dir[DIHOS_SHELL_PATH_CAP];
        char stdin_text[DIHOS_SCRIPT_VAR_VALUE_CAP];
        char status_text[DIHOS_SCRIPT_VAR_VALUE_CAP];
        char exit_text[DIHOS_SCRIPT_VAR_VALUE_CAP];
        char input_var[DIHOS_SCRIPT_VAR_NAME_CAP];
        char input_prompt[DIHOS_SCRIPT_VAR_VALUE_CAP];
        char lines[DIHOS_SCRIPT_MAX_LINES][DIHOS_SCRIPT_LINE_CAP];
        dihos_script_var vars[DIHOS_SCRIPT_MAX_VARS];
        dihos_script_label labels[DIHOS_SCRIPT_MAX_LABELS];
        dihos_script_function functions[DIHOS_SCRIPT_MAX_FUNCS];
        dihos_script_call_frame call_frames[DIHOS_SCRIPT_MAX_CALL_DEPTH];
        dihos_script_loop_frame loop_frames[DIHOS_SCRIPT_MAX_LOOPS];
        char args[DIHOS_SCRIPT_MAX_ARGS][DIHOS_SCRIPT_VAR_VALUE_CAP];
        uint32_t line_count;
        uint32_t var_count;
        uint32_t label_count;
        uint32_t function_count;
        uint32_t call_depth;
        uint32_t loop_depth;
        uint32_t arg_count;
        uint32_t pc;
        uint32_t instruction_count;
        uint32_t rng_state;
        int last_status;
        int exit_status;
        uint8_t loaded;
        uint8_t finished;
        uint8_t waiting_input;
        uint8_t rng_seeded;
    } dihos_script_runner;

    void dihos_shell_init(void);
    int dihos_shell_execute_line(const char *line);
    const char *dihos_shell_prompt(void);
    int dihos_shell_history_prev(const char *current_text, char *out, uint32_t out_cap);
    int dihos_shell_history_next(const char *current_text, char *out, uint32_t out_cap);

    void dihos_shell_session_init(dihos_shell_session *session, const dihos_shell_io *io);
    int dihos_shell_session_execute_line(dihos_shell_session *session, const char *line);
    const char *dihos_shell_session_prompt(dihos_shell_session *session);
    int dihos_shell_session_history_prev(dihos_shell_session *session, const char *current_text, char *out, uint32_t out_cap);
    int dihos_shell_session_history_next(dihos_shell_session *session, const char *current_text, char *out, uint32_t out_cap);
    void dihos_shell_session_print(dihos_shell_session *session, const char *text);
    void dihos_shell_session_print_inline(dihos_shell_session *session, const char *text);
    void dihos_shell_session_warn(dihos_shell_session *session, const char *text);
    void dihos_shell_session_error(dihos_shell_session *session, const char *text);
    void dihos_shell_session_success(dihos_shell_session *session, const char *text);

    int dihos_script_load_file(dihos_script_runner *runner, dihos_shell_session *session,
                               const char *raw_path, const char *friendly_path);
    int dihos_script_load_file_with_stdin(dihos_script_runner *runner, dihos_shell_session *session,
                                          const char *raw_path, const char *friendly_path,
                                          const char *stdin_text);
    int dihos_script_load_file_with_args(dihos_script_runner *runner, dihos_shell_session *session,
                                         const char *raw_path, const char *friendly_path,
                                         const char *stdin_text,
                                         const char *const *args, uint32_t arg_count);
    int dihos_script_step(dihos_script_runner *runner, uint32_t budget);
    int dihos_script_finished(const dihos_script_runner *runner);
    int dihos_script_exit_status(const dihos_script_runner *runner);
    const char *dihos_script_exit_text(const dihos_script_runner *runner);
    int dihos_script_waiting_input(const dihos_script_runner *runner);
    const char *dihos_script_input_prompt(const dihos_script_runner *runner);
    int dihos_script_submit_input(dihos_script_runner *runner, const char *text);

#ifdef __cplusplus
}
#endif
