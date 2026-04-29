#include "shell/dihos_shell.h"

#include "bootinfo.h"
#include "filesystem/dihos_path.h"
#include "gpio/gpio.h"
#include "hardware_probes/acpi_dump.h"
#include "hardware_probes/acpi_probe_hid.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "hardware_probes/acpi_probe_i2c.h"
#include "hardware_probes/touchpad_dsm.h"
#include "hardware_probes/touchpad_ps0.h"
#include "hardware_probes/touchpad_wake.h"
#include "i2c/i2c1_hidi2c.h"
#include "kwrappers/kfile.h"
#include "kwrappers/string.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"
#include <stddef.h>
#include <stdint.h>

extern const boot_info *k_bootinfo_ptr;

#define DIHOS_SHELL_TOKEN_MAX 64u
#define DIHOS_SHELL_ARG_MAX 24u
#define DIHOS_SHELL_PIPELINE_MAX 8u
#define DIHOS_SHELL_TAIL_RING_MAX 128u

typedef enum
{
    DIHOS_TOK_WORD = 1,
    DIHOS_TOK_PIPE,
    DIHOS_TOK_AND,
    DIHOS_TOK_OR,
    DIHOS_TOK_SEMI
} dihos_token_type;

typedef struct
{
    uint8_t type;
    char *text;
} dihos_token;

typedef struct
{
    const char *key;
    const char *value;
} dihos_named_arg;

typedef struct dihos_shell_stage
{
    dihos_shell_session *session;
    const char *name;
    const char *stdin_text;
    uint32_t stdin_len;
    const char *positional[DIHOS_SHELL_ARG_MAX];
    dihos_named_arg named[DIHOS_SHELL_ARG_MAX];
    uint8_t positional_count;
    uint8_t named_count;
    uint8_t capture_only;
    uint8_t has_stdin;
} dihos_shell_stage;

typedef int (*dihos_shell_handler_fn)(dihos_shell_stage *stage);

typedef struct
{
    const char *name;
    const char *usage;
    const char *summary;
    uint8_t accepts_stdin;
    dihos_shell_handler_fn handler;
} dihos_shell_command;

typedef struct
{
    char cwd[DIHOS_SHELL_PATH_CAP];
    char prompt[DIHOS_SHELL_PROMPT_CAP];
    char history[DIHOS_SHELL_HISTORY_MAX][DIHOS_SHELL_HISTORY_ENTRY_CAP];
    char history_draft[DIHOS_SHELL_HISTORY_ENTRY_CAP];
    uint8_t history_count;
    int history_browse_index;
} dihos_shell_state;

typedef struct
{
    char *dst;
    uint32_t cap;
    uint32_t len;
    uint8_t truncated;
} dihos_capture_buffer;

static dihos_shell_session G_default_shell;
static dihos_shell_session *G_active_shell = &G_default_shell;

static void dihos_shell_default_print(const char *text, void *user);
static void dihos_shell_default_print_inline(const char *text, void *user);
static void dihos_shell_default_warn(const char *text, void *user);
static void dihos_shell_default_error(const char *text, void *user);
static void dihos_shell_default_success(const char *text, void *user);
static void dihos_shell_default_clear(void *user);
static void dihos_terminal_capture_begin_raw(uint8_t mirror_to_terminal, terminal_capture_sink_fn sink, void *user);
static void dihos_terminal_capture_end_raw(void);
static dihos_shell_session *dihos_current_shell(void);
static void dihos_shell_output_print(dihos_shell_session *session, const char *text);
static void dihos_shell_output_print_inline(dihos_shell_session *session, const char *text);
static void dihos_shell_output_warn(dihos_shell_session *session, const char *text);
static void dihos_shell_output_error(dihos_shell_session *session, const char *text);
static void dihos_shell_output_success(dihos_shell_session *session, const char *text);
static void dihos_shell_output_clear(dihos_shell_session *session);
static void dihos_shell_output_inline_hex64(dihos_shell_session *session, uint64_t value);
static void dihos_shell_output_inline_hex32(dihos_shell_session *session, uint32_t value);
static void dihos_shell_output_inline_hex8(dihos_shell_session *session, uint32_t value);
static void dihos_shell_capture_begin(dihos_shell_session *session, uint8_t mirror_to_terminal,
                                      dihos_shell_capture_sink_fn sink, void *user);
static void dihos_shell_capture_end(dihos_shell_session *session);

#define G_shell (*dihos_current_shell())
#define terminal_print(text) dihos_shell_output_print(dihos_current_shell(), (text))
#define terminal_print_inline(text) dihos_shell_output_print_inline(dihos_current_shell(), (text))
#define terminal_warn(text) dihos_shell_output_warn(dihos_current_shell(), (text))
#define terminal_error(text) dihos_shell_output_error(dihos_current_shell(), (text))
#define terminal_success(text) dihos_shell_output_success(dihos_current_shell(), (text))
#define terminal_clear_no_flush() dihos_shell_output_clear(dihos_current_shell())
#define terminal_print_inline_hex64(value) dihos_shell_output_inline_hex64(dihos_current_shell(), (value))
#define terminal_print_inline_hex32(value) dihos_shell_output_inline_hex32(dihos_current_shell(), (value))
#define terminal_print_inline_hex8(value) dihos_shell_output_inline_hex8(dihos_current_shell(), (value))
#define terminal_capture_begin(mirror, sink, user) dihos_shell_capture_begin(dihos_current_shell(), (mirror), (sink), (user))
#define terminal_capture_end() dihos_shell_capture_end(dihos_current_shell())

static int dihos_cmd_sys_help(dihos_shell_stage *stage);
static int dihos_cmd_sys_clear(dihos_shell_stage *stage);
static int dihos_cmd_sys_echo(dihos_shell_stage *stage);
static int dihos_cmd_sys_about(dihos_shell_stage *stage);
static int dihos_cmd_sys_boot(dihos_shell_stage *stage);
static int dihos_cmd_sys_history(dihos_shell_stage *stage);
static int dihos_cmd_sys_status(dihos_shell_stage *stage);
static int dihos_cmd_sys_time(dihos_shell_stage *stage);
static int dihos_cmd_sys_run(dihos_shell_stage *stage);
static int dihos_cmd_fs_pwd(dihos_shell_stage *stage);
static int dihos_cmd_fs_cd(dihos_shell_stage *stage);
static int dihos_cmd_fs_list(dihos_shell_stage *stage);
static int dihos_cmd_fs_read(dihos_shell_stage *stage);
static int dihos_cmd_fs_stat(dihos_shell_stage *stage);
static int dihos_cmd_fs_make(dihos_shell_stage *stage);
static int dihos_cmd_fs_write(dihos_shell_stage *stage);
static int dihos_cmd_fs_copy(dihos_shell_stage *stage);
static int dihos_cmd_fs_move(dihos_shell_stage *stage);
static int dihos_cmd_fs_remove(dihos_shell_stage *stage);
static int dihos_cmd_hw_acpi(dihos_shell_stage *stage);
static int dihos_cmd_hw_touchpad(dihos_shell_stage *stage);
static int dihos_cmd_hw_gpio(dihos_shell_stage *stage);
static int dihos_cmd_text_head(dihos_shell_stage *stage);
static int dihos_cmd_text_tail(dihos_shell_stage *stage);
static int dihos_cmd_text_match(dihos_shell_stage *stage);
static int dihos_cmd_text_count(dihos_shell_stage *stage);

static const dihos_shell_command G_commands[] = {
    {"sys:help", "sys:help [command]", "Show DIHOS shell help.", 0u, dihos_cmd_sys_help},
    {"sys:clear", "sys:clear", "Clear the terminal surface.", 0u, dihos_cmd_sys_clear},
    {"sys:echo", "sys:echo [text...]", "Print text or forwarded stdin.", 1u, dihos_cmd_sys_echo},
    {"sys:about", "sys:about", "Show DIHOS shell info.", 0u, dihos_cmd_sys_about},
    {"sys:boot", "sys:boot", "Show boot and firmware hints.", 0u, dihos_cmd_sys_boot},
    {"sys:history", "sys:history", "Show recent DIHOS commands.", 0u, dihos_cmd_sys_history},
    {"sys:status", "sys:status", "Show current shell and device status.", 0u, dihos_cmd_sys_status},
    {"sys:time", "sys:time [mode=ticks|seconds|fattime] [base=dec|hex]", "Show kernel time counters.", 0u, dihos_cmd_sys_time},
    {"sys:run", "sys:run [path] [args...] [out=name] [window=yes|no]", "Run a .sac script or .sacx app.", 1u, dihos_cmd_sys_run},
    {"fs:pwd", "fs:pwd", "Print the current friendly working directory.", 0u, dihos_cmd_fs_pwd},
    {"fs:cd", "fs:cd [path]", "Change the current working directory.", 0u, dihos_cmd_fs_cd},
    {"fs:list", "fs:list [path] [view=long]", "List directory entries.", 0u, dihos_cmd_fs_list},
    {"fs:read", "fs:read [path]", "Read a text file and print it.", 0u, dihos_cmd_fs_read},
    {"fs:stat", "fs:stat [path]", "Show file or directory details.", 0u, dihos_cmd_fs_stat},
    {"fs:make", "fs:make [path] unsafe=yes", "Create a directory.", 0u, dihos_cmd_fs_make},
    {"fs:write", "fs:write [path] [text...] unsafe=yes", "Write stdin or text into a file.", 1u, dihos_cmd_fs_write},
    {"fs:copy", "fs:copy [src] [dst] unsafe=yes", "Copy one file to another path.", 0u, dihos_cmd_fs_copy},
    {"fs:move", "fs:move [src] [dst] unsafe=yes", "Rename or move a path.", 0u, dihos_cmd_fs_move},
    {"fs:remove", "fs:remove [path] [recursive=yes] unsafe=yes", "Remove a file or directory.", 0u, dihos_cmd_fs_remove},
    {"hw:acpi", "hw:acpi [dump|hid|i2c]", "Run ACPI inspection helpers.", 0u, dihos_cmd_hw_acpi},
    {"hw:touchpad", "hw:touchpad [status|wake|ps0|dsm] [unsafe=yes]", "Inspect or poke HID-I2C touchpad state.", 0u, dihos_cmd_hw_touchpad},
    {"hw:gpio", "hw:gpio [read|mode|write] ...", "Inspect or change GPIO state.", 0u, dihos_cmd_hw_gpio},
    {"text:head", "text:head [lines=N]", "Keep the first N lines from stdin.", 1u, dihos_cmd_text_head},
    {"text:tail", "text:tail [lines=N]", "Keep the last N lines from stdin.", 1u, dihos_cmd_text_tail},
    {"text:match", "text:match needle=TEXT", "Filter stdin to matching lines.", 1u, dihos_cmd_text_match},
    {"text:count", "text:count [mode=lines|words|chars]", "Count stdin units.", 1u, dihos_cmd_text_count},
};

static int dihos_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char dihos_lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (char)(ch - 'A' + 'a');
    return ch;
}

static int dihos_has_sac_extension(const char *path)
{
    uint32_t len = 0u;

    if (!path)
        return 0;

    len = (uint32_t)strlen(path);
    if (len < 4u)
        return 0;

    path += len - 4u;
    return dihos_lower_ascii(path[0]) == '.' &&
           dihos_lower_ascii(path[1]) == 's' &&
           dihos_lower_ascii(path[2]) == 'a' &&
           dihos_lower_ascii(path[3]) == 'c';
}

static int dihos_has_sacx_extension(const char *path)
{
    uint32_t len = 0u;

    if (!path)
        return 0;

    len = (uint32_t)strlen(path);
    if (len < 5u)
        return 0;

    path += len - 5u;
    return dihos_lower_ascii(path[0]) == '.' &&
           dihos_lower_ascii(path[1]) == 's' &&
           dihos_lower_ascii(path[2]) == 'a' &&
           dihos_lower_ascii(path[3]) == 'c' &&
           dihos_lower_ascii(path[4]) == 'x';
}

static uint8_t dihos_is_yes(const char *value)
{
    if (!value)
        return 0u;

    return strcmp(value, "1") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "on") == 0;
}

static uint8_t dihos_is_no(const char *value)
{
    if (!value)
        return 0u;

    return strcmp(value, "0") == 0 ||
           strcmp(value, "no") == 0 ||
           strcmp(value, "false") == 0 ||
           strcmp(value, "off") == 0 ||
           strcmp(value, "hidden") == 0;
}

static void dihos_copy_trunc(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;

    if (!dst || cap == 0u)
        return;

    if (!src)
        src = "";

    while (src[i] && i + 1u < cap)
    {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = 0;
}

static int dihos_append_cstr(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0;
    uint32_t i = 0;

    if (!dst || !src || cap == 0u)
        return -1;

    len = (uint32_t)strlen(dst);
    while (src[i] && len + i + 1u < cap)
    {
        dst[len + i] = src[i];
        ++i;
    }

    if (src[i] != 0)
        return -1;

    dst[len + i] = 0;
    return 0;
}

static void dihos_shell_default_print(const char *text, void *user)
{
    (void)user;
    (terminal_print)(text);
}

static void dihos_shell_default_print_inline(const char *text, void *user)
{
    (void)user;
    (terminal_print_inline)(text);
}

static void dihos_shell_default_warn(const char *text, void *user)
{
    (void)user;
    (terminal_warn)(text);
}

static void dihos_shell_default_error(const char *text, void *user)
{
    (void)user;
    (terminal_error)(text);
}

static void dihos_shell_default_success(const char *text, void *user)
{
    (void)user;
    (terminal_success)(text);
}

static void dihos_shell_default_clear(void *user)
{
    (void)user;
    (terminal_clear_no_flush)();
}

static void dihos_terminal_capture_begin_raw(uint8_t mirror_to_terminal, terminal_capture_sink_fn sink, void *user)
{
    (terminal_capture_begin)(mirror_to_terminal, sink, user);
}

static void dihos_terminal_capture_end_raw(void)
{
    (terminal_capture_end)();
}

static dihos_shell_session *dihos_current_shell(void)
{
    return G_active_shell ? G_active_shell : &G_default_shell;
}

static void dihos_shell_capture_feed(dihos_shell_session *session, const char *prefix,
                                     const char *text, uint8_t append_newline)
{
    uint32_t text_len = 0u;

    if (!session || !session->capture_sink)
        return;

    if (prefix && prefix[0])
        session->capture_sink(prefix, (uint32_t)strlen(prefix), session->capture_user);
    if (text && text[0])
    {
        text_len = (uint32_t)strlen(text);
        session->capture_sink(text, text_len, session->capture_user);
    }
    if (append_newline && (!text || text_len == 0u || text[text_len - 1u] != '\n'))
        session->capture_sink("\n", 1u, session->capture_user);
}

static void dihos_shell_output_print(dihos_shell_session *session, const char *text)
{
    if (!session)
        session = &G_default_shell;
    dihos_shell_capture_feed(session, "", text, 1u);
    if (session->capture_sink && !session->capture_mirror)
        return;
    if (session->io.print)
        session->io.print(text ? text : "", session->io.user);
}

static void dihos_shell_output_print_inline(dihos_shell_session *session, const char *text)
{
    if (!session)
        session = &G_default_shell;
    dihos_shell_capture_feed(session, "", text, 0u);
    if (session->capture_sink && !session->capture_mirror)
        return;
    if (session->io.print_inline)
        session->io.print_inline(text ? text : "", session->io.user);
}

static void dihos_shell_output_warn(dihos_shell_session *session, const char *text)
{
    if (!session)
        session = &G_default_shell;
    dihos_shell_capture_feed(session, "[WARN] ", text, 1u);
    if (session->capture_sink && !session->capture_mirror)
        return;
    if (session->io.warn)
        session->io.warn(text ? text : "", session->io.user);
}

static void dihos_shell_output_error(dihos_shell_session *session, const char *text)
{
    if (!session)
        session = &G_default_shell;
    dihos_shell_capture_feed(session, "[ERROR] ", text, 1u);
    if (session->capture_sink && !session->capture_mirror)
        return;
    if (session->io.error)
        session->io.error(text ? text : "", session->io.user);
}

static void dihos_shell_output_success(dihos_shell_session *session, const char *text)
{
    if (!session)
        session = &G_default_shell;
    dihos_shell_capture_feed(session, "[SUCCESS] ", text, 1u);
    if (session->capture_sink && !session->capture_mirror)
        return;
    if (session->io.success)
        session->io.success(text ? text : "", session->io.user);
}

static void dihos_shell_output_clear(dihos_shell_session *session)
{
    if (!session)
        session = &G_default_shell;
    if (session->io.clear)
        session->io.clear(session->io.user);
}

static void dihos_shell_hex_to_text(uint64_t value, uint8_t digits, char *out)
{
    const char *hex = "0123456789ABCDEF";

    out[0] = '0';
    out[1] = 'x';
    for (uint8_t i = 0u; i < digits; ++i)
    {
        uint8_t shift = (uint8_t)((digits - 1u - i) * 4u);
        out[2u + i] = hex[(value >> shift) & 0xFu];
    }
    out[2u + digits] = 0;
}

static void dihos_shell_output_inline_hex64(dihos_shell_session *session, uint64_t value)
{
    char buf[19];
    dihos_shell_hex_to_text(value, 16u, buf);
    dihos_shell_output_print_inline(session, buf);
}

static void dihos_shell_output_inline_hex32(dihos_shell_session *session, uint32_t value)
{
    char buf[11];
    dihos_shell_hex_to_text(value, 8u, buf);
    dihos_shell_output_print_inline(session, buf);
}

static void dihos_shell_output_inline_hex8(dihos_shell_session *session, uint32_t value)
{
    char buf[5];
    dihos_shell_hex_to_text((uint8_t)value, 2u, buf);
    dihos_shell_output_print_inline(session, buf);
}

static void dihos_shell_capture_begin(dihos_shell_session *session, uint8_t mirror_to_terminal,
                                      dihos_shell_capture_sink_fn sink, void *user)
{
    if (!session)
        session = &G_default_shell;
    session->capture_mirror = mirror_to_terminal ? 1u : 0u;
    session->capture_sink = sink;
    session->capture_user = user;
    dihos_terminal_capture_begin_raw(mirror_to_terminal, sink, user);
}

static void dihos_shell_capture_end(dihos_shell_session *session)
{
    if (!session)
        session = &G_default_shell;
    dihos_terminal_capture_end_raw();
    session->capture_mirror = 0u;
    session->capture_sink = 0;
    session->capture_user = 0;
}

static void dihos_update_prompt(void)
{
    ksb b;

    ksb_init(&b, G_shell.prompt, sizeof(G_shell.prompt));
    ksb_puts(&b, "dihos:");
    ksb_puts(&b, G_shell.cwd[0] ? G_shell.cwd : "/");
    ksb_puts(&b, "> ");
}

static void dihos_history_reset_browse(void)
{
    G_shell.history_browse_index = -1;
    G_shell.history_draft[0] = 0;
}

static void dihos_history_push(const char *line)
{
    if (!line || !line[0])
        return;

    if (G_shell.history_count > 0 &&
        strcmp(G_shell.history[G_shell.history_count - 1u], line) == 0)
    {
        dihos_history_reset_browse();
        return;
    }

    if (G_shell.history_count >= DIHOS_SHELL_HISTORY_MAX)
    {
        memmove(G_shell.history[0], G_shell.history[1],
                (DIHOS_SHELL_HISTORY_MAX - 1u) * DIHOS_SHELL_HISTORY_ENTRY_CAP);
        G_shell.history_count = DIHOS_SHELL_HISTORY_MAX - 1u;
    }

    dihos_copy_trunc(G_shell.history[G_shell.history_count],
                     DIHOS_SHELL_HISTORY_ENTRY_CAP, line);
    G_shell.history_count++;
    dihos_history_reset_browse();
}

static void dihos_u64_to_dec(uint64_t value, char *out, uint32_t cap)
{
    char tmp[32];
    uint32_t len = 0;
    uint32_t i = 0;

    if (!out || cap == 0u)
        return;

    if (value == 0u)
    {
        if (cap > 1u)
        {
            out[0] = '0';
            out[1] = 0;
        }
        else
        {
            out[0] = 0;
        }
        return;
    }

    while (value && len < sizeof(tmp))
    {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (len > 0u && i + 1u < cap)
        out[i++] = tmp[--len];

    out[i] = 0;
}

static void dihos_print_dec_value(uint64_t value)
{
    char buf[32];
    dihos_u64_to_dec(value, buf, sizeof(buf));
    terminal_print_inline(buf);
}

static void dihos_emit_text_span(const char *text, uint32_t len, uint8_t append_newline)
{
    char buf[256];
    uint32_t copied = 0;
    uint32_t chunk = 0;

    while (copied < len)
    {
        chunk = len - copied;
        if (chunk >= sizeof(buf))
            chunk = (uint32_t)sizeof(buf) - 1u;

        memcpy(buf, text + copied, chunk);
        buf[chunk] = 0;
        terminal_print_inline(buf);
        copied += chunk;
    }

    if (append_newline)
        terminal_print_inline("\n");
}

static void dihos_print_label_value(const char *label, const char *value)
{
    terminal_print_inline(label);
    terminal_print_inline(value ? value : "");
    terminal_print_inline("\n");
}

static void dihos_print_label_hex32(const char *label, uint32_t value)
{
    terminal_print_inline(label);
    terminal_print_inline_hex32(value);
    terminal_print_inline("\n");
}

static void dihos_print_label_hex64(const char *label, uint64_t value)
{
    terminal_print_inline(label);
    terminal_print_inline_hex64(value);
    terminal_print_inline("\n");
}

static void dihos_error3(const char *a, const char *b, const char *c)
{
    char buf[256];
    ksb builder;

    ksb_init(&builder, buf, sizeof(buf));
    if (a)
        ksb_puts(&builder, a);
    if (b)
        ksb_puts(&builder, b);
    if (c)
        ksb_puts(&builder, c);

    terminal_error(buf);
}

static int dihos_parse_u32(const char *text, uint32_t *out)
{
    uint64_t value = 0;
    uint32_t base = 10u;
    uint32_t i = 0;

    if (!text || !text[0] || !out)
        return -1;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16u;
        i = 2u;
        if (!text[i])
            return -1;
    }

    for (; text[i]; ++i)
    {
        uint32_t digit = 0;
        char ch = text[i];

        if (ch >= '0' && ch <= '9')
            digit = (uint32_t)(ch - '0');
        else if (base == 16u && ch >= 'a' && ch <= 'f')
            digit = 10u + (uint32_t)(ch - 'a');
        else if (base == 16u && ch >= 'A' && ch <= 'F')
            digit = 10u + (uint32_t)(ch - 'A');
        else
            return -1;

        if (digit >= base)
            return -1;

        value = (value * base) + digit;
        if (value > 0xFFFFFFFFu)
            return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static const char *dihos_stage_named(const dihos_shell_stage *stage, const char *key)
{
    uint32_t i = 0;

    if (!stage || !key)
        return 0;

    for (i = 0; i < stage->named_count; ++i)
    {
        if (strcmp(stage->named[i].key, key) == 0)
            return stage->named[i].value;
    }

    return 0;
}

static uint8_t dihos_stage_has_unsafe(const dihos_shell_stage *stage)
{
    return dihos_is_yes(dihos_stage_named(stage, "unsafe"));
}

static int dihos_require_unsafe(const dihos_shell_stage *stage, const char *label)
{
    if (dihos_stage_has_unsafe(stage))
        return 0;

    if (label && label[0])
        dihos_error3(label, " needs unsafe=yes", 0);
    else
        terminal_error("unsafe command requires unsafe=yes");

    return -1;
}

static const dihos_shell_command *dihos_find_command(const char *name)
{
    uint32_t i = 0;

    for (i = 0; i < (uint32_t)(sizeof(G_commands) / sizeof(G_commands[0])); ++i)
    {
        if (strcmp(G_commands[i].name, name) == 0)
            return &G_commands[i];
    }

    return 0;
}

static int dihos_resolve_raw_path(const char *input, char *friendly, char *raw)
{
    return dihos_path_resolve_raw(G_shell.cwd, input,
                                  friendly, DIHOS_SHELL_PATH_CAP,
                                  raw, DIHOS_SHELL_PATH_CAP);
}

static int dihos_join_raw_path(const char *base, const char *name, char *out, uint32_t cap)
{
    return dihos_path_join_raw(base, name, out, cap);
}

static void dihos_capture_sink(const char *text, uint32_t len, void *user)
{
    dihos_capture_buffer *capture = (dihos_capture_buffer *)user;
    uint32_t room = 0;

    if (!capture || !capture->dst || capture->cap == 0u || !text || len == 0u)
        return;

    if (capture->len + 1u >= capture->cap)
    {
        capture->truncated = 1u;
        return;
    }

    room = capture->cap - capture->len - 1u;
    if (len > room)
    {
        len = room;
        capture->truncated = 1u;
    }

    memcpy(capture->dst + capture->len, text, len);
    capture->len += len;
    capture->dst[capture->len] = 0;
}

static void dihos_external_terminal_sink(const char *text, uint32_t len, void *user)
{
    dihos_shell_session *session = (dihos_shell_session *)user;
    char chunk[256];
    uint32_t copied = 0u;

    if (!session || !text || len == 0u)
        return;

    while (copied < len)
    {
        uint32_t take = len - copied;
        if (take >= sizeof(chunk))
            take = (uint32_t)sizeof(chunk) - 1u;

        memcpy(chunk, text + copied, take);
        chunk[take] = 0;
        dihos_shell_output_print_inline(session, chunk);
        copied += take;
    }
}

static int dihos_tokenize(char *line, dihos_token *tokens, uint32_t *out_count)
{
    char *read = line;
    char *write = line;
    char *token_start = 0;
    uint32_t count = 0;
    uint8_t in_quote = 0;

    while (*read)
    {
        if (!in_quote && dihos_is_space(*read))
        {
            if (token_start)
            {
                *write++ = 0;
                token_start = 0;
            }
            ++read;
            continue;
        }

        if (!in_quote && (*read == ';' || *read == '|' || *read == '&'))
        {
            if (token_start)
            {
                *write++ = 0;
                token_start = 0;
            }

            if (count >= DIHOS_SHELL_TOKEN_MAX)
                return -1;

            if (*read == ';')
            {
                tokens[count].type = DIHOS_TOK_SEMI;
                tokens[count].text = 0;
                ++count;
                ++read;
                continue;
            }

            if (*read == '|')
            {
                if (read[1] == '|')
                {
                    tokens[count].type = DIHOS_TOK_OR;
                    tokens[count].text = 0;
                    ++count;
                    read += 2;
                }
                else
                {
                    tokens[count].type = DIHOS_TOK_PIPE;
                    tokens[count].text = 0;
                    ++count;
                    ++read;
                }
                continue;
            }

            if (*read == '&')
            {
                if (read[1] != '&')
                    return -1;

                tokens[count].type = DIHOS_TOK_AND;
                tokens[count].text = 0;
                ++count;
                read += 2;
                continue;
            }
        }

        if (!token_start)
        {
            if (count >= DIHOS_SHELL_TOKEN_MAX)
                return -1;

            token_start = write;
            tokens[count].type = DIHOS_TOK_WORD;
            tokens[count].text = token_start;
            ++count;
        }

        if (*read == '"')
        {
            in_quote = in_quote ? 0u : 1u;
            ++read;
            continue;
        }

        if (*read == '\\')
        {
            ++read;
            if (!*read)
                return -1;
        }

        *write++ = *read++;
    }

    if (in_quote)
        return -1;

    if (token_start)
        *write++ = 0;

    *write = 0;
    *out_count = count;
    return 0;
}

static int dihos_parse_pipeline(const dihos_token *tokens, uint32_t start, uint32_t end,
                                uint32_t *cmd_starts, uint32_t *cmd_ends, uint32_t *out_count)
{
    uint32_t stage_count = 0;
    uint32_t i = start;
    uint32_t cmd_start = start;

    if (start >= end)
        return -1;

    while (i < end)
    {
        if (tokens[i].type == DIHOS_TOK_PIPE)
        {
            if (i == cmd_start || stage_count >= DIHOS_SHELL_PIPELINE_MAX)
                return -1;

            cmd_starts[stage_count] = cmd_start;
            cmd_ends[stage_count] = i;
            ++stage_count;
            cmd_start = i + 1u;
        }
        else if (tokens[i].type != DIHOS_TOK_WORD)
        {
            return -1;
        }

        ++i;
    }

    if (cmd_start >= end || stage_count >= DIHOS_SHELL_PIPELINE_MAX)
        return -1;

    cmd_starts[stage_count] = cmd_start;
    cmd_ends[stage_count] = end;
    ++stage_count;
    *out_count = stage_count;
    return 0;
}

static int dihos_execute_stage(dihos_token *tokens, uint32_t start, uint32_t end,
                               const char *stdin_text, uint32_t stdin_len, uint8_t stdin_present,
                               uint8_t capture_only, char *capture_out, uint32_t capture_cap)
{
    dihos_shell_stage stage;
    const dihos_shell_command *command = 0;
    uint32_t i = 0;
    int rc = 0;

    if (start >= end || tokens[start].type != DIHOS_TOK_WORD)
        return -1;

    memset(&stage, 0, sizeof(stage));
    stage.session = dihos_current_shell();
    stage.name = tokens[start].text;
    stage.stdin_text = stdin_text ? stdin_text : "";
    stage.stdin_len = stdin_present ? stdin_len : 0u;
    stage.capture_only = capture_only ? 1u : 0u;
    stage.has_stdin = stdin_present ? 1u : 0u;

    for (i = start + 1u; i < end; ++i)
    {
        char *word = tokens[i].text;
        char *eq = 0;

        if (tokens[i].type != DIHOS_TOK_WORD || !word)
            return -1;

        eq = strchr(word, '=');
        if (eq && eq != word)
        {
            if (stage.named_count >= DIHOS_SHELL_ARG_MAX)
                return -1;
            *eq = 0;
            stage.named[stage.named_count].key = word;
            stage.named[stage.named_count].value = eq + 1u;
            stage.named_count++;
        }
        else
        {
            if (stage.positional_count >= DIHOS_SHELL_ARG_MAX)
                return -1;
            stage.positional[stage.positional_count++] = word;
        }
    }

    command = dihos_find_command(stage.name);
    if (!command)
    {
        dihos_error3("unknown DIHOS command ", stage.name, 0);
        return -1;
    }

    if (stage.has_stdin && !command->accepts_stdin)
    {
        dihos_error3("command does not accept piped stdin ", command->name, 0);
        return -1;
    }

    if (capture_only)
    {
        dihos_capture_buffer capture;

        memset(&capture, 0, sizeof(capture));
        capture.dst = capture_out;
        capture.cap = capture_cap;
        if (capture.dst && capture.cap > 0u)
            capture.dst[0] = 0;

        terminal_capture_begin(0u, dihos_capture_sink, &capture);
        rc = command->handler(&stage);
        terminal_capture_end();

        if (capture.truncated)
        {
            terminal_error("DIHOS pipe buffer overflow");
            rc = -1;
        }

        if (rc != 0 && capture.len > 0u)
        {
            terminal_print_inline(capture.dst);
            if (capture.dst[capture.len - 1u] != '\n')
                terminal_print_inline("\n");
        }
    }
    else
    {
        dihos_terminal_capture_begin_raw(0u, dihos_external_terminal_sink, dihos_current_shell());
        rc = command->handler(&stage);
        dihos_terminal_capture_end_raw();
    }

    return rc;
}

static int dihos_execute_pipeline(dihos_token *tokens, uint32_t *cmd_starts,
                                  uint32_t *cmd_ends, uint32_t stage_count)
{
    const char *stdin_text = 0;
    uint32_t stdin_len = 0;
    uint8_t stdin_present = 0u;
    uint32_t i = 0;
    int rc = 0;

    for (i = 0; i < stage_count; ++i)
    {
        uint8_t capture_only = (i + 1u < stage_count) ? 1u : 0u;
        char *capture_dst = capture_only ? G_shell.pipe_buffers[i & 1u] : 0;

        if (capture_only && capture_dst)
            capture_dst[0] = 0;

        rc = dihos_execute_stage(tokens, cmd_starts[i], cmd_ends[i],
                                 stdin_text, stdin_len, stdin_present,
                                 capture_only, capture_dst, DIHOS_SHELL_CAPTURE_CAP);
        if (rc != 0)
            return rc;

        if (capture_only)
        {
            stdin_text = capture_dst;
            stdin_len = (uint32_t)strlen(capture_dst);
            stdin_present = 1u;
        }
    }

    return 0;
}

static int dihos_execute_tokens(dihos_token *tokens, uint32_t count)
{
    uint8_t gate_op = DIHOS_TOK_SEMI;
    uint32_t index = 0;
    int last_status = 0;

    while (index < count)
    {
        uint32_t cmd_starts[DIHOS_SHELL_PIPELINE_MAX];
        uint32_t cmd_ends[DIHOS_SHELL_PIPELINE_MAX];
        uint32_t stage_count = 0;
        uint32_t range_end = index;
        uint8_t next_gate = 0u;
        int should_run = 1;

        while (range_end < count &&
               tokens[range_end].type != DIHOS_TOK_AND &&
               tokens[range_end].type != DIHOS_TOK_OR &&
               tokens[range_end].type != DIHOS_TOK_SEMI)
        {
            ++range_end;
        }

        if (dihos_parse_pipeline(tokens, index, range_end,
                                 cmd_starts, cmd_ends, &stage_count) != 0)
        {
            terminal_error("DIHOS syntax error");
            return -1;
        }

        if (gate_op == DIHOS_TOK_AND && last_status != 0)
            should_run = 0;
        else if (gate_op == DIHOS_TOK_OR && last_status == 0)
            should_run = 0;

        if (should_run)
            last_status = dihos_execute_pipeline(tokens, cmd_starts, cmd_ends, stage_count);

        if (range_end >= count)
            break;

        next_gate = tokens[range_end].type;
        index = range_end + 1u;
        gate_op = next_gate;

        if (index >= count)
        {
            terminal_error("DIHOS syntax error");
            return -1;
        }
    }

    return last_status;
}

static int dihos_resolve_path_or_cwd(const char *input, char *friendly, char *raw)
{
    if (dihos_resolve_raw_path(input ? input : G_shell.cwd, friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    return 0;
}

static int dihos_join_positionals(const dihos_shell_stage *stage, uint32_t start, char *out, uint32_t cap)
{
    uint32_t i = 0;
    uint32_t len = 0;

    if (!out || cap == 0u)
        return -1;

    out[0] = 0;

    for (i = start; i < stage->positional_count; ++i)
    {
        const char *piece = stage->positional[i];
        uint32_t piece_len = 0;

        if (!piece)
            continue;

        piece_len = (uint32_t)strlen(piece);
        if (len + piece_len + 2u >= cap)
            return -1;

        if (len > 0u)
            out[len++] = ' ';

        memcpy(out + len, piece, piece_len);
        len += piece_len;
        out[len] = 0;
    }

    return 0;
}

static int dihos_remove_path_recursive(const char *raw_path)
{
    KDir dir;
    kdirent ent;
    char child[DIHOS_SHELL_PATH_CAP];

    if (!raw_path || strcmp(raw_path, "0:/") == 0)
        return -1;

    if (kdir_open(&dir, raw_path) != 0)
        return kfile_unlink(raw_path);

    while (kdir_next(&dir, &ent) > 0)
    {
        if (dihos_join_raw_path(raw_path, ent.name, child, sizeof(child)) != 0)
        {
            kdir_close(&dir);
            return -1;
        }

        if (ent.is_dir)
        {
            if (dihos_remove_path_recursive(child) != 0)
            {
                kdir_close(&dir);
                return -1;
            }
        }
        else if (kfile_unlink(child) != 0)
        {
            kdir_close(&dir);
            return -1;
        }
    }

    kdir_close(&dir);
    return kfile_unlink(raw_path);
}

static int dihos_require_stdin(const dihos_shell_stage *stage, const char *label)
{
    if (stage && stage->has_stdin)
        return 0;

    if (label)
        terminal_error(label);
    else
        terminal_error("command requires stdin");
    return -1;
}

static int dihos_cmd_sys_help(dihos_shell_stage *stage)
{
    uint32_t i = 0;

    if (stage->positional_count > 0)
    {
        const dihos_shell_command *cmd = dihos_find_command(stage->positional[0]);
        if (!cmd)
        {
            terminal_error("no help for unknown command");
            return -1;
        }

        terminal_print_inline("\n");

        dihos_print_label_value("name: ", cmd->name);
        dihos_print_label_value("usage: ", cmd->usage);
        dihos_print_label_value("about: ", cmd->summary);
        terminal_print_inline("stdin: ");
        terminal_print_inline(cmd->accepts_stdin ? "yes\n" : "no\n");
        return 0;
    }

    terminal_print("DIHOS commands:");
    for (i = 0; i < (uint32_t)(sizeof(G_commands) / sizeof(G_commands[0])); ++i)
    {
        dihos_print_label_value("  ", G_commands[i].usage);
    }
    terminal_print("operators: ;  &&  ||  |");
    terminal_print("unsafe actions require unsafe=yes");
    return 0;
}

static int dihos_cmd_sys_clear(dihos_shell_stage *stage)
{
    (void)stage;

    if (stage->capture_only)
    {
        terminal_error("sys:clear cannot run inside a pipe");
        return -1;
    }

    terminal_clear_no_flush();
    return 0;
}

static int dihos_cmd_sys_echo(dihos_shell_stage *stage)
{
    char joined[1024];

    if (stage->positional_count > 0)
    {
        if (dihos_join_positionals(stage, 0u, joined, sizeof(joined)) != 0)
        {
            terminal_error("echo text is too long");
            return -1;
        }

        terminal_print(joined);
        return 0;
    }

    if (stage->has_stdin)
    {
        terminal_print_inline(stage->stdin_text);
        if (stage->stdin_len > 0u && stage->stdin_text[stage->stdin_len - 1u] != '\n')
            terminal_print_inline("\n");
        return 0;
    }

    terminal_print("");
    return 0;
}

static int dihos_cmd_sys_about(dihos_shell_stage *stage)
{
    (void)stage;

    terminal_print("DIHOS shell v1");
    terminal_print("syntax: namespace:verb positional... key=value");
    terminal_print("features: prompt, cwd, history, &&, ||, ;, |");
    return 0;
}

static int dihos_cmd_sys_boot(dihos_shell_stage *stage)
{
    (void)stage;

    if (!k_bootinfo_ptr)
    {
        terminal_error("boot info is unavailable");
        return -1;
    }

    terminal_print_inline("\n");
    dihos_print_label_hex64("fb_base: ", k_bootinfo_ptr->fb.fb_base);
    terminal_print_inline("fb_size: ");
    dihos_print_dec_value(k_bootinfo_ptr->fb.fb_size);
    terminal_print_inline("\n");
    terminal_print_inline("fb_dims: ");
    dihos_print_dec_value(k_bootinfo_ptr->fb.width);
    terminal_print_inline(" x ");
    dihos_print_dec_value(k_bootinfo_ptr->fb.height);
    terminal_print_inline("\n");
    dihos_print_label_hex64("acpi_rsdp: ", k_bootinfo_ptr->acpi_rsdp);
    dihos_print_label_hex64("xhci_mmio: ", k_bootinfo_ptr->xhci_mmio_base);
    dihos_print_label_hex64("tlmm_mmio: ", k_bootinfo_ptr->tlmm_mmio_base);
    dihos_print_label_hex64("kernel_base: ", k_bootinfo_ptr->kernel_base_phys);
    dihos_print_label_hex64("sacx_exec_pool: ", k_bootinfo_ptr->sacx_exec_pool_base_phys);
    dihos_print_label_hex64("sacx_exec_size: ", k_bootinfo_ptr->sacx_exec_pool_size_bytes);
    return 0;
}

static int dihos_cmd_sys_history(dihos_shell_stage *stage)
{
    uint32_t i = 0;
    (void)stage;

    for (i = 0; i < G_shell.history_count; ++i)
    {
        terminal_print_inline("#");
        dihos_print_dec_value(i + 1u);
        terminal_print_inline(": ");
        terminal_print_inline(G_shell.history[i]);
        terminal_print_inline("\n");
    }

    return 0;
}

static int dihos_cmd_sys_status(dihos_shell_stage *stage)
{
    const hidi2c_device *kbd = i2c1_hidi2c_keyboard();
    const hidi2c_device *tpd = i2c1_hidi2c_touchpad();
    (void)stage;

    dihos_print_label_value("cwd: ", G_shell.cwd);
    terminal_print_inline("history_count: ");
    dihos_print_dec_value(G_shell.history_count);
    terminal_print_inline("\n");
    terminal_print_inline("acpi_rsdp_present: ");
    terminal_print_inline((k_bootinfo_ptr && k_bootinfo_ptr->acpi_rsdp) ? "yes\n" : "no\n");
    terminal_print_inline("keyboard_online: ");
    terminal_print_inline((kbd && kbd->online) ? "yes\n" : "no\n");
    terminal_print_inline("touchpad_online: ");
    terminal_print_inline((tpd && tpd->online) ? "yes\n" : "no\n");
    return 0;
}

static int dihos_cmd_sys_time(dihos_shell_stage *stage)
{
    const char *mode = dihos_stage_named(stage, "mode");
    const char *base = dihos_stage_named(stage, "base");
    uint64_t value = 0u;

    if (!mode || strcmp(mode, "ticks") == 0)
        value = dihos_time_ticks();
    else if (strcmp(mode, "seconds") == 0)
        value = dihos_time_seconds();
    else if (strcmp(mode, "fattime") == 0)
        value = (uint64_t)dihos_time_fattime();
    else
    {
        terminal_error("sys:time mode must be ticks, seconds, or fattime");
        return -1;
    }

    if (base && strcmp(base, "hex") == 0)
    {
        terminal_print_inline_hex64(value);
        terminal_print_inline("\n");
    }
    else
    {
        dihos_print_dec_value(value);
        terminal_print_inline("\n");
    }

    return 0;
}

static int dihos_cmd_sys_run(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    char arg_storage[DIHOS_SCRIPT_MAX_ARGS][DIHOS_SCRIPT_VAR_VALUE_CAP];
    const char *script_args[DIHOS_SCRIPT_MAX_ARGS];
    const char *stdin_text = 0;
    uint32_t stdin_len = 0u;
    uint32_t stdin_pos = 0u;
    uint32_t arg_count = 0u;
    uint32_t launch_flags = TERMINAL_OPEN_FLAG_NONE;
    uint8_t have_stdin = 0u;
    uint8_t hide_window = 0u;
    char stdin_line[DIHOS_SCRIPT_VAR_VALUE_CAP];
    const char *window_mode = 0;
    dihos_script_runner runner;
    int rc = 0;

    G_shell.run_status_text[0] = 0;

    if (stage->positional_count == 0u)
    {
        terminal_error("sys:run needs a script/app path");
        return -1;
    }

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid script/app path");
        return -1;
    }

    if (dihos_has_sacx_extension(friendly))
    {
        window_mode = dihos_stage_named(stage, "window");
        if (window_mode && dihos_is_no(window_mode))
            hide_window = 1u;
        if (dihos_is_yes(dihos_stage_named(stage, "hidden")) ||
            dihos_is_yes(dihos_stage_named(stage, "no_window")))
            hide_window = 1u;
        if (hide_window)
            launch_flags |= TERMINAL_OPEN_FLAG_NO_WINDOW;

        if (stage->positional_count > 1u)
            terminal_warn("sys:run .sacx currently ignores extra args/stdin");

        if (terminal_open_program_ex(raw, friendly, launch_flags) != 0)
        {
            terminal_error("unable to launch .sacx app");
            return -1;
        }

        if (hide_window)
            dihos_copy_trunc(G_shell.run_status_text, sizeof(G_shell.run_status_text), "launched sacx app (hidden window)");
        else
            dihos_copy_trunc(G_shell.run_status_text, sizeof(G_shell.run_status_text), "launched sacx app");
        return 0;
    }

    if (!dihos_has_sac_extension(friendly))
    {
        terminal_error("sys:run supports .sac and .sacx files");
        return -1;
    }

    if (stage->positional_count > 1u + DIHOS_SCRIPT_MAX_ARGS)
    {
        terminal_error("sys:run supports up to 16 script args");
        return -1;
    }

    for (uint32_t i = 1u; i < stage->positional_count; ++i)
    {
        const char *pos = stage->positional[i];
        uint32_t len = 0u;

        if (pos && strcmp(pos, "out") == 0)
        {
            if (i + 2u < stage->positional_count &&
                stage->positional[i + 1u] &&
                stage->positional[i + 2u] &&
                strcmp(stage->positional[i + 1u], "=") == 0)
            {
                i += 2u;
                continue;
            }

            if (i + 1u < stage->positional_count &&
                stage->positional[i + 1u] &&
                stage->positional[i + 1u][0] == '=')
            {
                ++i;
                continue;
            }
        }

        dihos_copy_trunc(arg_storage[arg_count], sizeof(arg_storage[arg_count]), stage->positional[i]);
        len = (uint32_t)strlen(arg_storage[arg_count]);
        while (len > 0u && arg_storage[arg_count][len - 1u] == ',')
            arg_storage[arg_count][--len] = 0;

        if (!arg_storage[arg_count][0])
            continue;

        script_args[arg_count] = arg_storage[arg_count];
        ++arg_count;
    }

    if (stage->has_stdin)
    {
        have_stdin = 1u;
        stdin_text = stage->stdin_text ? stage->stdin_text : "";
        stdin_len = stage->stdin_len;
    }
    else if (dihos_stage_named(stage, "stdin"))
    {
        have_stdin = 1u;
        stdin_text = dihos_stage_named(stage, "stdin");
        stdin_len = (uint32_t)strlen(stdin_text);
    }
    else
    {
        stdin_text = "";
        stdin_len = 0u;
    }

    if (dihos_script_load_file_with_args(&runner, &G_shell, raw, friendly, stdin_text, script_args, arg_count) != 0)
        return -1;

    for (;;)
    {
        rc = dihos_script_step(&runner, DIHOS_SCRIPT_STEP_BUDGET);
        if (rc < 0)
            return -1;
        if (rc > 0)
            break;

        if (dihos_script_waiting_input(&runner))
        {
            if (have_stdin)
            {
                uint32_t out_len = 0u;
                uint8_t have_line = 0u;
                if (stdin_pos < stdin_len)
                {
                    have_line = 1u;
                    while (stdin_pos < stdin_len)
                    {
                        char ch = stdin_text[stdin_pos++];

                        if (ch == '\r')
                        {
                            if (stdin_pos < stdin_len && stdin_text[stdin_pos] == '\n')
                                ++stdin_pos;
                            break;
                        }
                        if (ch == '\n')
                            break;

                        if (out_len + 1u < sizeof(stdin_line))
                            stdin_line[out_len++] = ch;
                    }
                }

                stdin_line[out_len] = 0;

                if (have_line)
                {
                    if (dihos_script_submit_input(&runner, stdin_line) != 0)
                    {
                        terminal_error("failed to feed script input");
                        return -1;
                    }
                    continue;
                }

                terminal_error("sys:run stdin exhausted while script requested input");
                return -1;
            }

            terminal_error("sys:run requires piped stdin for input");
            return -1;
        }
    }

    dihos_copy_trunc(G_shell.run_status_text, sizeof(G_shell.run_status_text), dihos_script_exit_text(&runner));
    return dihos_script_exit_status(&runner);
}

static int dihos_cmd_fs_pwd(dihos_shell_stage *stage)
{
    (void)stage;
    terminal_print(G_shell.cwd);
    return 0;
}

static int dihos_cmd_fs_cd(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KDir dir;

    if (dihos_resolve_path_or_cwd(stage->positional_count > 0 ? stage->positional[0] : G_shell.cwd,
                                  friendly, raw) != 0)
        return -1;

    if (kdir_open(&dir, raw) != 0)
    {
        terminal_error("target is not a directory");
        return -1;
    }

    kdir_close(&dir);
    dihos_copy_trunc(G_shell.cwd, sizeof(G_shell.cwd), friendly);
    dihos_update_prompt();
    return 0;
}

static int dihos_cmd_fs_list(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KDir dir;
    kdirent ent;
    const char *view = dihos_stage_named(stage, "view");

    if (dihos_resolve_path_or_cwd(stage->positional_count > 0 ? stage->positional[0] : G_shell.cwd,
                                  friendly, raw) != 0)
        return -1;

    if (kdir_open(&dir, raw) != 0)
    {
        terminal_error("cannot open directory");
        return -1;
    }

    while (kdir_next(&dir, &ent) > 0)
    {
        if (view && strcmp(view, "long") == 0)
        {
            terminal_print_inline(ent.is_dir ? "[dir] " : "[file] ");
            if (!ent.is_dir)
            {
                dihos_print_dec_value(ent.size);
                terminal_print_inline(" ");
            }
        }

        terminal_print_inline(ent.name);
        terminal_print_inline("\n");
    }

    kdir_close(&dir);
    return 0;
}

static int dihos_cmd_fs_read(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KFile file;
    char buf[512];
    uint32_t got = 0;
    uint8_t saw_output = 0;
    uint8_t ended_with_newline = 1u;

    if (stage->positional_count == 0)
    {
        terminal_error("fs:read needs a path");
        return -1;
    }

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (kfile_open(&file, raw, KFILE_READ) != 0)
    {
        terminal_error("cannot open file");
        return -1;
    }

    for (;;)
    {
        if (kfile_read(&file, buf, (uint32_t)sizeof(buf) - 1u, &got) != 0 || got == 0u)
            break;

        buf[got] = 0;
        terminal_print_inline(buf);
        ended_with_newline = (buf[got - 1u] == '\n') ? 1u : 0u;
        saw_output = 1u;
    }

    kfile_close(&file);

    if (saw_output && !ended_with_newline)
        terminal_print_inline("\n");

    return 0;
}

static int dihos_cmd_fs_stat(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KDir dir;
    KFile file;

    if (dihos_resolve_path_or_cwd(stage->positional_count > 0 ? stage->positional[0] : G_shell.cwd,
                                  friendly, raw) != 0)
        return -1;

    dihos_print_label_value("path: ", friendly);
    if (kdir_open(&dir, raw) == 0)
    {
        kdir_close(&dir);
        terminal_print("type: dir");
        return 0;
    }

    if (kfile_open(&file, raw, KFILE_READ) == 0)
    {
        terminal_print("type: file");
        terminal_print_inline("size: ");
        dihos_print_dec_value(kfile_size(&file));
        terminal_print_inline("\n");
        kfile_close(&file);
        return 0;
    }

    terminal_error("path not found");
    return -1;
}

static int dihos_cmd_fs_make(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];

    if (stage->positional_count == 0)
    {
        terminal_error("fs:make needs a path");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:make") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (kfile_mkdir(raw) != 0)
    {
        terminal_error("failed to create directory");
        return -1;
    }

    terminal_success("directory created");
    return 0;
}

static int dihos_cmd_fs_write(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    char joined[1024];
    const char *text = 0;
    KFile file;
    uint32_t wrote = 0;

    if (stage->positional_count == 0)
    {
        terminal_error("fs:write needs a path");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:write") != 0)
        return -1;

    if (stage->has_stdin)
    {
        text = stage->stdin_text;
    }
    else if (dihos_stage_named(stage, "text"))
    {
        text = dihos_stage_named(stage, "text");
    }
    else
    {
        if (dihos_join_positionals(stage, 1u, joined, sizeof(joined)) != 0)
        {
            terminal_error("write text is too long");
            return -1;
        }
        text = joined;
    }

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (kfile_open(&file, raw, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
    {
        terminal_error("cannot open file for writing");
        return -1;
    }

    if (text && text[0] && kfile_write(&file, text, (uint32_t)strlen(text), &wrote) != 0)
    {
        kfile_close(&file);
        terminal_error("write failed");
        return -1;
    }

    kfile_close(&file);
    terminal_success("file written");
    return 0;
}

static int dihos_copy_file_raw(const char *src, const char *dst)
{
    KFile in;
    KFile out;
    char buf[1024];
    uint32_t got = 0;
    uint32_t wrote = 0;

    if (kfile_open(&in, src, KFILE_READ) != 0)
        return -1;

    if (kfile_open(&out, dst, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
    {
        kfile_close(&in);
        return -1;
    }

    for (;;)
    {
        if (kfile_read(&in, buf, (uint32_t)sizeof(buf), &got) != 0 || got == 0u)
            break;

        if (kfile_write(&out, buf, got, &wrote) != 0 || wrote != got)
        {
            kfile_close(&in);
            kfile_close(&out);
            return -1;
        }
    }

    kfile_close(&in);
    kfile_close(&out);
    return 0;
}

static int dihos_cmd_fs_copy(dihos_shell_stage *stage)
{
    char src_friendly[DIHOS_SHELL_PATH_CAP];
    char src_raw[DIHOS_SHELL_PATH_CAP];
    char dst_friendly[DIHOS_SHELL_PATH_CAP];
    char dst_raw[DIHOS_SHELL_PATH_CAP];

    if (stage->positional_count < 2u)
    {
        terminal_error("fs:copy needs src and dst");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:copy") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], src_friendly, src_raw) != 0 ||
        dihos_resolve_raw_path(stage->positional[1], dst_friendly, dst_raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (dihos_copy_file_raw(src_raw, dst_raw) != 0)
    {
        terminal_error("copy failed");
        return -1;
    }

    terminal_success("copy complete");
    return 0;
}

static int dihos_cmd_fs_move(dihos_shell_stage *stage)
{
    char src_friendly[DIHOS_SHELL_PATH_CAP];
    char src_raw[DIHOS_SHELL_PATH_CAP];
    char dst_friendly[DIHOS_SHELL_PATH_CAP];
    char dst_raw[DIHOS_SHELL_PATH_CAP];

    if (stage->positional_count < 2u)
    {
        terminal_error("fs:move needs src and dst");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:move") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], src_friendly, src_raw) != 0 ||
        dihos_resolve_raw_path(stage->positional[1], dst_friendly, dst_raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (kfile_rename(src_raw, dst_raw) != 0)
    {
        terminal_error("move failed");
        return -1;
    }

    terminal_success("move complete");
    return 0;
}

static int dihos_cmd_fs_remove(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    uint8_t recursive = dihos_is_yes(dihos_stage_named(stage, "recursive"));

    if (stage->positional_count == 0u)
    {
        terminal_error("fs:remove needs a path");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:remove") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (strcmp(friendly, "/") == 0)
    {
        terminal_error("refusing to remove shell root");
        return -1;
    }

    if (recursive)
    {
        if (dihos_remove_path_recursive(raw) != 0)
        {
            terminal_error("recursive remove failed");
            return -1;
        }
    }
    else if (kfile_unlink(raw) != 0)
    {
        terminal_error("remove failed");
        return -1;
    }

    terminal_success("path removed");
    return 0;
}

static int dihos_cmd_hw_acpi(dihos_shell_stage *stage)
{
    uint64_t rsdp = (k_bootinfo_ptr ? k_bootinfo_ptr->acpi_rsdp : 0u);

    if (stage->positional_count == 0u)
    {
        terminal_error("hw:acpi needs dump, hid, or i2c");
        return -1;
    }

    if (!rsdp)
    {
        terminal_error("ACPI RSDP is unavailable");
        return -1;
    }

    if (strcmp(stage->positional[0], "dump") == 0)
    {
        acpi_dump_all(rsdp);
        return 0;
    }
    if (strcmp(stage->positional[0], "hid") == 0)
    {
        acpi_probe_hid_from_rsdp(rsdp);
        return 0;
    }
    if (strcmp(stage->positional[0], "i2c") == 0)
    {
        acpi_probe_i2c_from_rsdp(rsdp);
        return 0;
    }

    terminal_error("unknown hw:acpi mode");
    return -1;
}

static int dihos_cmd_hw_touchpad(dihos_shell_stage *stage)
{
    const hidi2c_device *kbd = i2c1_hidi2c_keyboard();
    const hidi2c_device *tpd = i2c1_hidi2c_touchpad();
    uint64_t rsdp = (k_bootinfo_ptr ? k_bootinfo_ptr->acpi_rsdp : 0u);

    if (stage->positional_count == 0u)
    {
        terminal_error("hw:touchpad needs status, wake, ps0, or dsm");
        return -1;
    }

    if (strcmp(stage->positional[0], "status") == 0)
    {
        if (kbd)
        {
            terminal_print("keyboard:");
            dihos_print_label_value("  name: ", kbd->name ? kbd->name : "unknown");
            dihos_print_label_hex32("  addr: ", kbd->i2c_addr_7bit);
            dihos_print_label_hex32("  desc_reg: ", kbd->hid_desc_reg);
            terminal_print_inline("  online: ");
            terminal_print_inline(kbd->online ? "yes\n" : "no\n");
        }

        if (tpd)
        {
            terminal_print("touchpad:");
            dihos_print_label_value("  name: ", tpd->name ? tpd->name : "unknown");
            dihos_print_label_hex32("  addr: ", tpd->i2c_addr_7bit);
            dihos_print_label_hex32("  desc_reg: ", tpd->hid_desc_reg);
            terminal_print_inline("  online: ");
            terminal_print_inline(tpd->online ? "yes\n" : "no\n");
            terminal_print_inline("  report_desc_len: ");
            dihos_print_dec_value(tpd->report_desc_len);
            terminal_print_inline("\n");
        }

        if (!kbd && !tpd)
            terminal_warn("no HID-I2C devices are currently cached");
        return 0;
    }

    if (!rsdp)
    {
        terminal_error("ACPI RSDP is unavailable");
        return -1;
    }

    if (dihos_require_unsafe(stage, "hw:touchpad") != 0)
        return -1;

    if (strcmp(stage->positional[0], "wake") == 0)
        return touchpad_try_wake_from_acpi(rsdp);
    if (strcmp(stage->positional[0], "ps0") == 0)
        return touchpad_run_ps0(rsdp);
    if (strcmp(stage->positional[0], "dsm") == 0)
        return touchpad_run_dsm(rsdp);

    terminal_error("unknown hw:touchpad mode");
    return -1;
}

static int dihos_cmd_hw_gpio(dihos_shell_stage *stage)
{
    const char *pin_text = dihos_stage_named(stage, "pin");
    uint32_t pin = 0;

    if (stage->positional_count == 0u)
    {
        terminal_error("hw:gpio needs read, mode, or write");
        return -1;
    }

    if (!pin_text || dihos_parse_u32(pin_text, &pin) != 0)
    {
        terminal_error("hw:gpio needs pin=VALUE");
        return -1;
    }

    if (strcmp(stage->positional[0], "read") == 0)
    {
        uint32_t level = 0;
        if (gpio_read(pin, &level) != 0)
        {
            terminal_error("gpio read failed");
            return -1;
        }

        dihos_print_label_hex32("pin: ", pin);
        dihos_print_label_hex32("value: ", level);
        return 0;
    }

    if (dihos_require_unsafe(stage, "hw:gpio") != 0)
        return -1;

    if (strcmp(stage->positional[0], "mode") == 0)
    {
        const char *dir = dihos_stage_named(stage, "dir");
        int rc = -1;

        if (!dir)
        {
            terminal_error("hw:gpio mode needs dir=in or dir=out");
            return -1;
        }

        if (strcmp(dir, "in") == 0 || strcmp(dir, "input") == 0)
            rc = gpio_set_input(pin);
        else if (strcmp(dir, "out") == 0 || strcmp(dir, "output") == 0)
            rc = gpio_set_output(pin);
        else
            rc = -1;

        if (rc != 0)
        {
            terminal_error("gpio mode change failed");
            return -1;
        }

        terminal_success("gpio mode updated");
        return 0;
    }

    if (strcmp(stage->positional[0], "write") == 0)
    {
        const char *value_text = dihos_stage_named(stage, "value");
        gpio_value value = GPIO_VALUE_LOW;

        if (!value_text)
        {
            terminal_error("hw:gpio write needs value=0 or value=1");
            return -1;
        }

        if (strcmp(value_text, "1") == 0 || strcmp(value_text, "high") == 0)
            value = GPIO_VALUE_HIGH;
        else if (strcmp(value_text, "0") == 0 || strcmp(value_text, "low") == 0)
            value = GPIO_VALUE_LOW;
        else
        {
            terminal_error("invalid gpio value");
            return -1;
        }

        if (gpio_write(pin, value) != 0)
        {
            terminal_error("gpio write failed");
            return -1;
        }

        terminal_success("gpio write complete");
        return 0;
    }

    terminal_error("unknown hw:gpio mode");
    return -1;
}

static int dihos_text_resolve_lines(const dihos_shell_stage *stage, uint32_t *out_lines)
{
    const char *lines_text = dihos_stage_named(stage, "lines");
    uint32_t lines = 10u;

    if (lines_text && dihos_parse_u32(lines_text, &lines) != 0)
        return -1;

    if (lines == 0u)
        lines = 1u;

    if (lines > DIHOS_SHELL_TAIL_RING_MAX)
        lines = DIHOS_SHELL_TAIL_RING_MAX;

    *out_lines = lines;
    return 0;
}

static int dihos_cmd_text_head(dihos_shell_stage *stage)
{
    uint32_t lines = 10u;
    uint32_t start = 0u;
    uint32_t i = 0u;
    uint32_t printed = 0u;

    if (dihos_require_stdin(stage, "text:head needs piped stdin") != 0)
        return -1;

    if (dihos_text_resolve_lines(stage, &lines) != 0)
    {
        terminal_error("invalid lines value");
        return -1;
    }

    for (i = 0u; i < stage->stdin_len && printed < lines; ++i)
    {
        if (stage->stdin_text[i] == '\n')
        {
            dihos_emit_text_span(stage->stdin_text + start, i - start, 1u);
            start = i + 1u;
            ++printed;
        }
    }

    if (printed < lines && start < stage->stdin_len)
        dihos_emit_text_span(stage->stdin_text + start, stage->stdin_len - start, 1u);

    return 0;
}

static int dihos_cmd_text_tail(dihos_shell_stage *stage)
{
    uint32_t lines = 10u;
    uint32_t starts[DIHOS_SHELL_TAIL_RING_MAX];
    uint32_t count = 0u;
    uint32_t ring_index = 0u;
    uint32_t line_start = 0u;
    uint32_t i = 0u;
    uint32_t begin = 0u;

    if (dihos_require_stdin(stage, "text:tail needs piped stdin") != 0)
        return -1;

    if (dihos_text_resolve_lines(stage, &lines) != 0)
    {
        terminal_error("invalid lines value");
        return -1;
    }

    starts[0] = 0u;
    count = 1u;

    for (i = 0u; i < stage->stdin_len; ++i)
    {
        if (stage->stdin_text[i] != '\n')
            continue;

        line_start = i + 1u;
        if (line_start >= stage->stdin_len)
            continue;

        if (count < lines)
        {
            starts[count++] = line_start;
        }
        else
        {
            starts[ring_index] = line_start;
            ring_index = (ring_index + 1u) % lines;
        }
    }

    if (count >= lines)
        begin = starts[ring_index % lines];
    else
        begin = starts[0];

    if (begin < stage->stdin_len)
    {
        terminal_print_inline(stage->stdin_text + begin);
        if (stage->stdin_text[stage->stdin_len - 1u] != '\n')
            terminal_print_inline("\n");
    }

    return 0;
}

static int dihos_cmd_text_match(dihos_shell_stage *stage)
{
    const char *needle = dihos_stage_named(stage, "needle");
    uint32_t start = 0u;
    uint32_t i = 0u;

    if (dihos_require_stdin(stage, "text:match needs piped stdin") != 0)
        return -1;

    if (!needle && stage->positional_count > 0u)
        needle = stage->positional[0];

    if (!needle || !needle[0])
    {
        terminal_error("text:match needs needle=TEXT");
        return -1;
    }

    for (i = 0u; i <= stage->stdin_len; ++i)
    {
        if (i != stage->stdin_len && stage->stdin_text[i] != '\n')
            continue;

        {
            char line[512];
            uint32_t len = i - start;
            if (len >= sizeof(line))
                len = (uint32_t)sizeof(line) - 1u;
            memcpy(line, stage->stdin_text + start, len);
            line[len] = 0;

            if (strstr(line, needle))
                terminal_print(line);
        }

        start = i + 1u;
    }

    return 0;
}

static int dihos_cmd_text_count(dihos_shell_stage *stage)
{
    const char *mode = dihos_stage_named(stage, "mode");
    uint64_t count = 0u;
    uint32_t i = 0u;
    uint8_t in_word = 0u;

    if (dihos_require_stdin(stage, "text:count needs piped stdin") != 0)
        return -1;

    if (!mode || strcmp(mode, "lines") == 0)
    {
        for (i = 0u; i < stage->stdin_len; ++i)
            if (stage->stdin_text[i] == '\n')
                ++count;

        if (stage->stdin_len > 0u && stage->stdin_text[stage->stdin_len - 1u] != '\n')
            ++count;
    }
    else if (strcmp(mode, "chars") == 0)
    {
        count = stage->stdin_len;
    }
    else if (strcmp(mode, "words") == 0)
    {
        for (i = 0u; i < stage->stdin_len; ++i)
        {
            if (dihos_is_space(stage->stdin_text[i]))
                in_word = 0u;
            else if (!in_word)
            {
                in_word = 1u;
                ++count;
            }
        }
    }
    else
    {
        terminal_error("invalid count mode");
        return -1;
    }

    terminal_print_inline("count: ");
    dihos_print_dec_value(count);
    terminal_print_inline("\n");
    return 0;
}

static dihos_shell_session *dihos_shell_enter(dihos_shell_session *session)
{
    dihos_shell_session *previous = G_active_shell;
    G_active_shell = session ? session : &G_default_shell;
    return previous;
}

static void dihos_shell_leave(dihos_shell_session *previous)
{
    G_active_shell = previous ? previous : &G_default_shell;
}

static void dihos_shell_set_io_defaults(dihos_shell_session *session, const dihos_shell_io *io)
{
    memset(&session->io, 0, sizeof(session->io));
    if (io)
        session->io = *io;

    if (!session->io.print)
        session->io.print = dihos_shell_default_print;
    if (!session->io.print_inline)
        session->io.print_inline = dihos_shell_default_print_inline;
    if (!session->io.warn)
        session->io.warn = dihos_shell_default_warn;
    if (!session->io.error)
        session->io.error = dihos_shell_default_error;
    if (!session->io.success)
        session->io.success = dihos_shell_default_success;
    if (!session->io.clear)
        session->io.clear = dihos_shell_default_clear;
}

void dihos_shell_session_init(dihos_shell_session *session, const dihos_shell_io *io)
{
    dihos_shell_session *previous = 0;

    if (!session)
        return;

    memset(session, 0, sizeof(*session));
    dihos_shell_set_io_defaults(session, io);
    dihos_copy_trunc(session->cwd, sizeof(session->cwd), "/");
    previous = dihos_shell_enter(session);
    dihos_history_reset_browse();
    dihos_update_prompt();
    dihos_shell_leave(previous);
}

void dihos_shell_init(void)
{
    dihos_shell_session_init(&G_default_shell, 0);
}

static int dihos_shell_execute_line_current(const char *line)
{
    char buffer[DIHOS_SHELL_LINE_CAP];
    dihos_token tokens[DIHOS_SHELL_TOKEN_MAX];
    uint32_t count = 0u;
    uint32_t i = 0u;
    uint8_t has_text = 0u;
    int rc = 0;

    if (!line)
        return 0;

    dihos_copy_trunc(buffer, sizeof(buffer), line);
    if (!buffer[0])
    {
        dihos_history_reset_browse();
        return 0;
    }

    for (i = 0u; buffer[i]; ++i)
    {
        if (!dihos_is_space(buffer[i]))
        {
            has_text = 1u;
            break;
        }
    }

    if (!has_text)
    {
        dihos_history_reset_browse();
        return 0;
    }

    dihos_history_push(buffer);

    if (dihos_tokenize(buffer, tokens, &count) != 0)
    {
        terminal_error("DIHOS parse error");
        return -1;
    }

    if (count == 0u)
        return 0;

    rc = dihos_execute_tokens(tokens, count);
    dihos_update_prompt();
    return rc;
}

int dihos_shell_session_execute_line(dihos_shell_session *session, const char *line)
{
    dihos_shell_session *previous = dihos_shell_enter(session ? session : &G_default_shell);
    int rc = dihos_shell_execute_line_current(line);
    dihos_shell_leave(previous);
    return rc;
}

int dihos_shell_execute_line(const char *line)
{
    return dihos_shell_session_execute_line(&G_default_shell, line);
}

const char *dihos_shell_session_prompt(dihos_shell_session *session)
{
    dihos_shell_session *previous = 0;

    if (!session)
        session = &G_default_shell;

    previous = dihos_shell_enter(session);
    if (!G_shell.prompt[0])
        dihos_update_prompt();
    dihos_shell_leave(previous);
    return session->prompt;
}

const char *dihos_shell_prompt(void)
{
    return dihos_shell_session_prompt(&G_default_shell);
}

int dihos_shell_session_history_prev(dihos_shell_session *session, const char *current_text, char *out, uint32_t out_cap)
{
    if (!session)
        session = &G_default_shell;

    if (!out || out_cap == 0u || session->history_count == 0u)
        return 0;

    if (session->history_browse_index < 0)
    {
        dihos_copy_trunc(session->history_draft, sizeof(session->history_draft),
                         current_text ? current_text : "");
        session->history_browse_index = (int)session->history_count - 1;
    }
    else if (session->history_browse_index > 0)
    {
        session->history_browse_index--;
    }

    dihos_copy_trunc(out, out_cap, session->history[session->history_browse_index]);
    return 1;
}

int dihos_shell_history_prev(const char *current_text, char *out, uint32_t out_cap)
{
    return dihos_shell_session_history_prev(&G_default_shell, current_text, out, out_cap);
}

int dihos_shell_session_history_next(dihos_shell_session *session, const char *current_text, char *out, uint32_t out_cap)
{
    (void)current_text;

    if (!session)
        session = &G_default_shell;

    if (!out || out_cap == 0u || session->history_browse_index < 0)
        return 0;

    if (session->history_browse_index < (int)session->history_count - 1)
    {
        session->history_browse_index++;
        dihos_copy_trunc(out, out_cap, session->history[session->history_browse_index]);
        return 1;
    }

    session->history_browse_index = -1;
    dihos_copy_trunc(out, out_cap, session->history_draft);
    return 1;
}

int dihos_shell_history_next(const char *current_text, char *out, uint32_t out_cap)
{
    return dihos_shell_session_history_next(&G_default_shell, current_text, out, out_cap);
}

void dihos_shell_session_print(dihos_shell_session *session, const char *text)
{
    dihos_shell_output_print(session ? session : &G_default_shell, text);
}

void dihos_shell_session_print_inline(dihos_shell_session *session, const char *text)
{
    dihos_shell_output_print_inline(session ? session : &G_default_shell, text);
}

void dihos_shell_session_warn(dihos_shell_session *session, const char *text)
{
    dihos_shell_output_warn(session ? session : &G_default_shell, text);
}

void dihos_shell_session_error(dihos_shell_session *session, const char *text)
{
    dihos_shell_output_error(session ? session : &G_default_shell, text);
}

void dihos_shell_session_success(dihos_shell_session *session, const char *text)
{
    dihos_shell_output_success(session ? session : &G_default_shell, text);
}
