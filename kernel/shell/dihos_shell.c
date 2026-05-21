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
#include "wifi/kwifi.h"
#include <stddef.h>
#include <stdint.h>

extern const boot_info *k_bootinfo_ptr;

#define DIHOS_SHELL_TOKEN_MAX 64u
#define DIHOS_SHELL_ARG_MAX 24u
#define DIHOS_SHELL_PIPELINE_MAX 8u
#define DIHOS_SHELL_TAIL_RING_MAX 128u

static const char *xhci_source_name(uint32_t source)
{
    switch (source)
    {
    case BOOTINFO_XHCI_SOURCE_DISCOVERED:
        return "discovered";
    case BOOTINFO_XHCI_SOURCE_FALLBACK_BUILTIN:
        return "fallback:builtin";
    default:
        return "unknown";
    }
}

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
static char G_console_scratch[DIHOS_SHELL_CAPTURE_CAP];
static uint8_t G_shell_trace = 0u;
static uint8_t G_shell_fallback_depth = 0u;

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
static int dihos_cmd_sys_flush(dihos_shell_stage *stage);
static int dihos_cmd_sys_echo(dihos_shell_stage *stage);
static int dihos_cmd_sys_about(dihos_shell_stage *stage);
static int dihos_cmd_sys_boot(dihos_shell_stage *stage);
static int dihos_cmd_sys_history(dihos_shell_stage *stage);
static int dihos_cmd_sys_status(dihos_shell_stage *stage);
static int dihos_cmd_sys_time(dihos_shell_stage *stage);
static int dihos_cmd_sys_run(dihos_shell_stage *stage);
static int dihos_cmd_sys_which(dihos_shell_stage *stage);
static int dihos_cmd_sys_trace(dihos_shell_stage *stage);
static int dihos_cmd_wifi_networks(dihos_shell_stage *stage);
static int dihos_cmd_wifi_connect(dihos_shell_stage *stage);
static int dihos_cmd_wifi_current(dihos_shell_stage *stage);
static int dihos_cmd_wifi_supplicant(dihos_shell_stage *stage);
static int dihos_cmd_wifi_get(dihos_shell_stage *stage);
static int dihos_cmd_wifi_rx(dihos_shell_stage *stage);
static int dihos_cmd_wifi_group(dihos_shell_stage *stage);
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
static int dihos_cmd_fs_touch(dihos_shell_stage *stage);
static int dihos_cmd_fs_append(dihos_shell_stage *stage);
static int dihos_cmd_fs_find(dihos_shell_stage *stage);
static int dihos_cmd_fs_tree(dihos_shell_stage *stage);
static int dihos_cmd_hw_acpi(dihos_shell_stage *stage);
static int dihos_cmd_hw_touchpad(dihos_shell_stage *stage);
static int dihos_cmd_hw_gpio(dihos_shell_stage *stage);
static int dihos_cmd_hw_group(dihos_shell_stage *stage);
static int dihos_cmd_text_head(dihos_shell_stage *stage);
static int dihos_cmd_text_tail(dihos_shell_stage *stage);
static int dihos_cmd_text_match(dihos_shell_stage *stage);
static int dihos_cmd_text_count(dihos_shell_stage *stage);
static int dihos_cmd_text_wc(dihos_shell_stage *stage);
static int dihos_cmd_text_sort(dihos_shell_stage *stage);
static int dihos_cmd_text_uniq(dihos_shell_stage *stage);
static int dihos_cmd_text_tee(dihos_shell_stage *stage);
static int dihos_cmd_text_yes(dihos_shell_stage *stage);
static int dihos_cmd_text_cut(dihos_shell_stage *stage);
static int dihos_cmd_file_hexdump(dihos_shell_stage *stage);
static int dihos_cmd_file_strings(dihos_shell_stage *stage);
static int dihos_cmd_test_assert(dihos_shell_stage *stage);
static int dihos_cmd_test_assert_eq(dihos_shell_stage *stage);
static int dihos_cmd_test_fail(dihos_shell_stage *stage);
static int dihos_cmd_shell_fallback(dihos_shell_stage *stage);
static int dihos_shell_fallback_available(const char *name, char *friendly, char *raw);

static const dihos_shell_command G_commands[] = {
    {"sys:help", "sys:help [command]", "Show DIHOS shell help.", 0u, dihos_cmd_sys_help},
    {"sys:clear", "sys:clear", "Clear the terminal surface.", 0u, dihos_cmd_sys_clear},
    {"sys:flush", "sys:flush", "Flush the terminal log to storage.", 0u, dihos_cmd_sys_flush},
    {"sys:echo", "sys:echo [text...]", "Print text or forwarded stdin.", 1u, dihos_cmd_sys_echo},
    {"sys:about", "sys:about", "Show DIHOS shell info.", 0u, dihos_cmd_sys_about},
    {"sys:boot", "sys:boot", "Show boot and firmware hints.", 0u, dihos_cmd_sys_boot},
    {"sys:history", "sys:history", "Show recent DIHOS commands.", 0u, dihos_cmd_sys_history},
    {"sys:status", "sys:status", "Show current shell and device status.", 0u, dihos_cmd_sys_status},
    {"sys:time", "sys:time [mode=ticks|seconds|fattime] [base=dec|hex]", "Show kernel time counters.", 0u, dihos_cmd_sys_time},
    {"sys:run", "sys:run [path] [args...] [out=name] [window=yes|no]", "Run a .sac script or .sacx app.", 1u, dihos_cmd_sys_run},
    {"sys:which", "sys:which [command]", "Show how a command resolves.", 0u, dihos_cmd_sys_which},
    {"sys:trace", "sys:trace [on|off|status]", "Toggle shell execution trace output.", 0u, dihos_cmd_sys_trace},
    {"wifi:networks", "wifi:networks [refresh=yes]", "Print WiFi networks, auto-scanning when the cache is empty.", 0u, dihos_cmd_wifi_networks},
    {"wifi:connect", "wifi:connect ssid=NAME password=PASS [username=USER] [bssid=AA:BB:CC:DD:EE:FF] [channel=40|5200] [automate=yes|no]", "Save a WiFi connect request and optionally pin AP BSSID/channel; enterprise mode polls state but does not fake PEAP key install.", 0u, dihos_cmd_wifi_connect},
    {"wifi:current", "wifi:current", "Show current WiFi connect target and state.", 0u, dihos_cmd_wifi_current},
    {"wifi:supplicant", "wifi:supplicant ready=yes|no", "Set enterprise supplicant readiness hint (does not by itself prove firmware key install).", 0u, dihos_cmd_wifi_supplicant},
    {"wifi:get", "wifi:get [url] [max=8192]", "Fetch text content for file:// URLs; reports HTTP transport readiness for http(s).", 0u, dihos_cmd_wifi_get},
    {"wifi:rx", "wifi:rx [drain=yes|no] [max=4]", "Show WiFi RX queue state and optionally drain captured frames.", 0u, dihos_cmd_wifi_rx},
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
    {"help", "help [command]", "Show shell help.", 0u, dihos_cmd_sys_help},
    {"man", "man [command]", "Show shell help for a command.", 0u, dihos_cmd_sys_help},
    {"clear", "clear", "Clear the terminal surface.", 0u, dihos_cmd_sys_clear},
    {"cls", "cls", "Clear the terminal surface.", 0u, dihos_cmd_sys_clear},
    {"flush", "flush", "Flush the terminal log to storage.", 0u, dihos_cmd_sys_flush},
    {"echo", "echo [text...]", "Print text or forwarded stdin.", 1u, dihos_cmd_sys_echo},
    {"about", "about", "Show DIHOS shell info.", 0u, dihos_cmd_sys_about},
    {"boot", "boot", "Show boot and firmware hints.", 0u, dihos_cmd_sys_boot},
    {"history", "history", "Show recent commands.", 0u, dihos_cmd_sys_history},
    {"status", "status", "Show current shell and device status.", 0u, dihos_cmd_sys_status},
    {"date", "date [mode=ticks|seconds|fattime] [base=dec|hex]", "Show kernel time counters.", 0u, dihos_cmd_sys_time},
    {"time", "time [mode=ticks|seconds|fattime] [base=dec|hex]", "Show kernel time counters.", 0u, dihos_cmd_sys_time},
    {"run", "run [path] [args...] [out=name] [window=yes|no]", "Run a .sac script or .sacx app.", 1u, dihos_cmd_sys_run},
    {"which", "which [command]", "Show how a command resolves.", 0u, dihos_cmd_sys_which},
    {"type", "type [command]", "Show how a command resolves.", 0u, dihos_cmd_sys_which},
    {"trace", "trace [on|off|status]", "Toggle shell execution trace output.", 0u, dihos_cmd_sys_trace},
    {"pwd", "pwd", "Print the current directory.", 0u, dihos_cmd_fs_pwd},
    {"cd", "cd [path]", "Change the current directory.", 0u, dihos_cmd_fs_cd},
    {"ls", "ls [-l] [path]", "List directory entries.", 0u, dihos_cmd_fs_list},
    {"dir", "dir [-l] [path]", "List directory entries.", 0u, dihos_cmd_fs_list},
    {"cat", "cat [path]", "Read a text file and print it.", 0u, dihos_cmd_fs_read},
    {"stat", "stat [path]", "Show file or directory details.", 0u, dihos_cmd_fs_stat},
    {"mkdir", "mkdir [-f] [-p] [path]", "Create a directory.", 0u, dihos_cmd_fs_make},
    {"touch", "touch [-f] [path]", "Create a file if needed.", 0u, dihos_cmd_fs_touch},
    {"write", "write [-f] [path] [text...]", "Write stdin or text into a file.", 1u, dihos_cmd_fs_write},
    {"append", "append [-f] [path] [text...]", "Append stdin or text to a file.", 1u, dihos_cmd_fs_append},
    {"cp", "cp [-f] [src] [dst]", "Copy one file to another path.", 0u, dihos_cmd_fs_copy},
    {"copy", "copy [-f] [src] [dst]", "Copy one file to another path.", 0u, dihos_cmd_fs_copy},
    {"mv", "mv [-f] [src] [dst]", "Move or rename a path.", 0u, dihos_cmd_fs_move},
    {"move", "move [-f] [src] [dst]", "Move or rename a path.", 0u, dihos_cmd_fs_move},
    {"rm", "rm [-f] [-r] [path]", "Remove a file or directory.", 0u, dihos_cmd_fs_remove},
    {"del", "del [-f] [-r] [path]", "Remove a file or directory.", 0u, dihos_cmd_fs_remove},
    {"find", "find [path] [name=TEXT] [max=N]", "Find files and directories.", 0u, dihos_cmd_fs_find},
    {"tree", "tree [path] [max=N]", "Print a directory tree.", 0u, dihos_cmd_fs_tree},
    {"head", "head [-n N] [lines=N]", "Keep the first N lines from stdin.", 1u, dihos_cmd_text_head},
    {"tail", "tail [-n N] [lines=N]", "Keep the last N lines from stdin.", 1u, dihos_cmd_text_tail},
    {"grep", "grep [-i] [-v] needle", "Filter stdin to matching lines.", 1u, dihos_cmd_text_match},
    {"wc", "wc [-l|-w|-c]", "Count lines, words, or chars from stdin.", 1u, dihos_cmd_text_wc},
    {"sort", "sort", "Sort stdin lines.", 1u, dihos_cmd_text_sort},
    {"uniq", "uniq", "Collapse adjacent duplicate stdin lines.", 1u, dihos_cmd_text_uniq},
    {"tee", "tee [-f] [path]", "Write stdin to a file and pass it onward.", 1u, dihos_cmd_text_tee},
    {"yes", "yes [text] [count=N]", "Print repeated text.", 0u, dihos_cmd_text_yes},
    {"cut", "cut field=N [delim=,]", "Select a delimited field from stdin.", 1u, dihos_cmd_text_cut},
    {"hexdump", "hexdump [path] [max=N]", "Print bytes as hex from a file or stdin.", 1u, dihos_cmd_file_hexdump},
    {"strings", "strings [path] [min=N]", "Print printable strings from a file or stdin.", 1u, dihos_cmd_file_strings},
    {"assert", "assert exists|isfile|isdir PATH", "Fail if a simple condition is false.", 0u, dihos_cmd_test_assert},
    {"assert_eq", "assert_eq [lhs] [rhs]", "Fail if two values differ.", 0u, dihos_cmd_test_assert_eq},
    {"fail", "fail [message...]", "Return failure for tests.", 0u, dihos_cmd_test_fail},
    {"wifi", "wifi scan|current|connect|supplicant|get|rx ...", "WiFi command group.", 0u, dihos_cmd_wifi_group},
    {"hw", "hw acpi|touchpad|gpio ...", "Hardware command group.", 0u, dihos_cmd_hw_group},
};

static const dihos_shell_command G_fallback_command = {
    "<shell-script>", "<namespace>:<command> [args...]", "Run /OS/System/Programs/Shell/<namespace>/<command>.sac.", 1u, dihos_cmd_shell_fallback};

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

static int dihos_starts_with(const char *text, const char *prefix)
{
    uint32_t i = 0u;

    if (!text || !prefix)
        return 0;

    while (prefix[i])
    {
        if (text[i] != prefix[i])
            return 0;
        ++i;
    }

    return 1;
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

static int dihos_stage_add_named(dihos_shell_stage *stage, const char *key, const char *value)
{
    uint32_t i = 0u;

    if (!stage || !key || !key[0])
        return -1;

    for (i = 0u; i < stage->named_count; ++i)
    {
        if (strcmp(stage->named[i].key, key) == 0)
        {
            stage->named[i].value = value ? value : "";
            return 0;
        }
    }

    if (stage->named_count >= DIHOS_SHELL_ARG_MAX)
        return -1;

    stage->named[stage->named_count].key = key;
    stage->named[stage->named_count].value = value ? value : "";
    stage->named_count++;
    return 0;
}

static int dihos_stage_add_positional(dihos_shell_stage *stage, const char *value)
{
    if (!stage)
        return -1;
    if (stage->positional_count >= DIHOS_SHELL_ARG_MAX)
        return -1;
    stage->positional[stage->positional_count++] = value ? value : "";
    return 0;
}

static int dihos_stage_enable_force(dihos_shell_stage *stage)
{
    return dihos_stage_add_named(stage, "unsafe", "yes") == 0 &&
           dihos_stage_add_named(stage, "force", "yes") == 0 &&
           dihos_stage_add_named(stage, "replace", "yes") == 0
               ? 0
               : -1;
}

static int dihos_stage_apply_short_flag(dihos_shell_stage *stage, char flag)
{
    switch (flag)
    {
    case 'f':
        return dihos_stage_enable_force(stage);
    case 'r':
    case 'R':
        return dihos_stage_add_named(stage, "recursive", "yes");
    case 'l':
        return dihos_stage_add_named(stage, "long", "yes") == 0 &&
               dihos_stage_add_named(stage, "view", "long") == 0
                   ? 0
                   : -1;
    case 'p':
        return dihos_stage_add_named(stage, "parents", "yes");
    case 'i':
        return dihos_stage_add_named(stage, "ignore_case", "yes");
    case 'v':
        return dihos_stage_add_named(stage, "invert", "yes");
    case 'n':
        return dihos_stage_add_named(stage, "number", "yes");
    case 'a':
        return dihos_stage_add_named(stage, "append", "yes") == 0 &&
               dihos_stage_add_named(stage, "all", "yes") == 0
                   ? 0
                   : -1;
    case 'w':
        return dihos_stage_add_named(stage, "mode", "words");
    case 'c':
        return dihos_stage_add_named(stage, "mode", "chars");
    default:
        return -1;
    }
}

static int dihos_parse_shell_flag(dihos_shell_stage *stage, char *word)
{
    char *eq = 0;
    const char *value = "yes";

    if (!word || word[0] != '-' || word[1] == 0)
        return 0;
    if (word[1] >= '0' && word[1] <= '9')
        return 0;

    if (word[1] == '-')
    {
        const char *name = word + 2u;
        if (!name[0])
            return -1;

        eq = strchr(word, '=');
        if (eq)
        {
            *eq = 0;
            value = eq + 1u;
        }

        if (strcmp(name, "force") == 0)
            return dihos_stage_enable_force(stage) == 0 ? 1 : -1;
        if (strcmp(name, "recursive") == 0)
            return dihos_stage_add_named(stage, "recursive", value) == 0 ? 1 : -1;
        if (strcmp(name, "long") == 0)
            return dihos_stage_add_named(stage, "long", value) == 0 &&
                           dihos_stage_add_named(stage, "view", "long") == 0
                       ? 1
                       : -1;
        if (strcmp(name, "parents") == 0)
            return dihos_stage_add_named(stage, "parents", value) == 0 ? 1 : -1;
        if (strcmp(name, "ignore-case") == 0)
            return dihos_stage_add_named(stage, "ignore_case", value) == 0 ? 1 : -1;
        if (strcmp(name, "invert-match") == 0)
            return dihos_stage_add_named(stage, "invert", value) == 0 ? 1 : -1;
        if (strcmp(name, "lines") == 0)
            return dihos_stage_add_named(stage, "lines", value) == 0 ? 1 : -1;
        if (strcmp(name, "words") == 0)
            return dihos_stage_add_named(stage, "mode", "words") == 0 ? 1 : -1;
        if (strcmp(name, "chars") == 0 || strcmp(name, "bytes") == 0)
            return dihos_stage_add_named(stage, "mode", "chars") == 0 ? 1 : -1;
        if (strcmp(name, "append") == 0)
            return dihos_stage_add_named(stage, "append", value) == 0 ? 1 : -1;
        if (strcmp(name, "all") == 0)
            return dihos_stage_add_named(stage, "all", value) == 0 ? 1 : -1;
        if (strcmp(name, "number") == 0)
            return dihos_stage_add_named(stage, "number", value) == 0 ? 1 : -1;
        if (strcmp(name, "max") == 0)
            return dihos_stage_add_named(stage, "max", value) == 0 ? 1 : -1;
        if (strcmp(name, "min") == 0)
            return dihos_stage_add_named(stage, "min", value) == 0 ? 1 : -1;

        terminal_error("unknown flag");
        return -1;
    }

    for (uint32_t i = 1u; word[i]; ++i)
    {
        if (dihos_stage_apply_short_flag(stage, word[i]) != 0)
        {
            terminal_error("unknown flag");
            return -1;
        }
    }

    return 1;
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

static int dihos_path_kind_raw(const char *raw)
{
    KDir dir;
    KFile file;

    if (!raw || !raw[0])
        return 0;

    if (kdir_open(&dir, raw) == 0)
    {
        kdir_close(&dir);
        return 2;
    }

    if (kfile_open(&file, raw, KFILE_READ) == 0)
    {
        kfile_close(&file);
        return 1;
    }

    return 0;
}

static int dihos_path_kind_friendly(const char *path)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];

    if (dihos_resolve_raw_path(path, friendly, raw) != 0)
        return 0;
    return dihos_path_kind_raw(raw);
}

static const char *dihos_path_basename(const char *friendly)
{
    const char *base = friendly;

    if (!friendly)
        return "";

    for (uint32_t i = 0u; friendly[i]; ++i)
    {
        if (friendly[i] == '/' && friendly[i + 1u])
            base = friendly + i + 1u;
    }

    return base ? base : "";
}

static int dihos_shell_name_segment_ok(const char *text)
{
    if (!text || !text[0])
        return 0;

    for (uint32_t i = 0u; text[i]; ++i)
    {
        char ch = text[i];
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '.' || dihos_is_space(ch))
            return 0;
    }

    return 1;
}

static int dihos_shell_build_fallback_path(const char *name, char *friendly, uint32_t friendly_cap,
                                           char *raw, uint32_t raw_cap)
{
    char ns[64];
    char cmd[64];
    uint32_t ns_len = 0u;
    uint32_t cmd_len = 0u;
    const char *colon = 0;
    ksb b;

    if (!name || !friendly || friendly_cap == 0u || !raw || raw_cap == 0u)
        return -1;

    colon = strchr(name, ':');
    if (!colon || colon == name || !colon[1])
        return -1;

    for (const char *p = name; p < colon && ns_len + 1u < sizeof(ns); ++p)
        ns[ns_len++] = *p;
    ns[ns_len] = 0;

    for (const char *p = colon + 1u; *p && cmd_len + 1u < sizeof(cmd); ++p)
        cmd[cmd_len++] = *p;
    cmd[cmd_len] = 0;

    if (!dihos_shell_name_segment_ok(ns) || !dihos_shell_name_segment_ok(cmd))
        return -1;

    ksb_init(&b, friendly, friendly_cap);
    ksb_puts(&b, "/OS/System/Programs/Shell/");
    ksb_puts(&b, ns);
    ksb_putc(&b, '/');
    ksb_puts(&b, cmd);
    ksb_puts(&b, ".sac");
    if (b.overflow)
        return -1;

    return dihos_path_friendly_to_raw(friendly, raw, raw_cap);
}

static int dihos_shell_fallback_available(const char *name, char *friendly, char *raw)
{
    char local_friendly[DIHOS_SHELL_PATH_CAP];
    char local_raw[DIHOS_SHELL_PATH_CAP];

    if (G_shell_fallback_depth >= 4u)
        return 0;

    if (dihos_shell_build_fallback_path(name,
                                        friendly ? friendly : local_friendly, DIHOS_SHELL_PATH_CAP,
                                        raw ? raw : local_raw, DIHOS_SHELL_PATH_CAP) != 0)
        return 0;

    return dihos_path_kind_raw(raw ? raw : local_raw) == 1;
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
            if (in_quote && *read == 'n')
            {
                *write++ = '\n';
                ++read;
                continue;
            }
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
        int flag_rc = 0;

        if (tokens[i].type != DIHOS_TOK_WORD || !word)
            return -1;

        flag_rc = dihos_parse_shell_flag(&stage, word);
        if (flag_rc < 0)
            return -1;
        if (flag_rc > 0)
            continue;

        eq = strchr(word, '=');
        if (eq && eq != word)
        {
            if (stage.named_count >= DIHOS_SHELL_ARG_MAX)
                return -1;
            *eq = 0;
            if (dihos_stage_add_named(&stage, word, eq + 1u) != 0)
                return -1;
        }
        else
        {
            if (dihos_stage_add_positional(&stage, word) != 0)
                return -1;
        }
    }

    command = dihos_find_command(stage.name);
    if (!command)
    {
        if (dihos_shell_fallback_available(stage.name, 0, 0))
            command = &G_fallback_command;
        else
        {
            dihos_error3("unknown DIHOS command ", stage.name, 0);
            return -1;
        }
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
        if (G_shell_trace && !capture_only)
        {
            terminal_print_inline("trace: ");
            terminal_print_inline(stage.name);
            terminal_print_inline(command == &G_fallback_command ? " -> shell script\n" : " -> builtin\n");
        }
        rc = command->handler(&stage);
        if (G_shell_trace && !capture_only)
        {
            terminal_print_inline("trace: status ");
            if (rc < 0)
                terminal_print_inline("-");
            dihos_print_dec_value(rc < 0 ? (uint64_t)(-rc) : (uint64_t)rc);
            terminal_print_inline("\n");
        }
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

static int dihos_is_console_runtime_token(const char *text)
{
    return text && strcmp(text, "%console%") == 0;
}

static const char *dihos_current_console_text(void)
{
    if (dihos_shell_session_console_text(dihos_current_shell(), G_console_scratch,
                                         sizeof(G_console_scratch)) != 0)
        G_console_scratch[0] = 0;
    return G_console_scratch;
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
        if (dihos_is_console_runtime_token(piece))
            piece = dihos_current_console_text();

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
            char friendly[DIHOS_SHELL_PATH_CAP];
            char raw[DIHOS_SHELL_PATH_CAP];

            if (dihos_shell_fallback_available(stage->positional[0], friendly, raw))
            {
                dihos_print_label_value("name: ", stage->positional[0]);
                dihos_print_label_value("usage: ", G_fallback_command.usage);
                dihos_print_label_value("about: ", G_fallback_command.summary);
                dihos_print_label_value("script: ", friendly);
                terminal_print("stdin: yes");
                return 0;
            }

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
    terminal_print("flags: -f/--force authorizes unsafe fs writes, -r recursive, -l long, -i ignore-case");
    terminal_print("paths: / is root, ./ is the current directory, ../ walks upward, 0:/ is raw disk root");
    terminal_print("fallback: unknown ns:cmd runs /OS/System/Programs/Shell/ns/cmd.sac when present");
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

static int dihos_cmd_sys_flush(dihos_shell_stage *stage)
{
    (void)stage;

    terminal_success("log flushed");
    terminal_flush_log();
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
    terminal_print("paths: / root, ./ current directory, ../ parent directory, 0:/ raw disk root");
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
    terminal_print_inline("xhci_count: ");
    dihos_print_dec_value(k_bootinfo_ptr->xhci_mmio_count);
    terminal_print_inline("\n");
    {
        uint32_t count = k_bootinfo_ptr->xhci_mmio_count;
        if (count > BOOTINFO_XHCI_MMIO_MAX)
            count = BOOTINFO_XHCI_MMIO_MAX;
        for (uint32_t i = 0; i < count; ++i)
        {
            dihos_print_label_hex64("xhci_base: ", k_bootinfo_ptr->xhci_mmio_bases[i]);
            terminal_print_inline("xhci_source: ");
            terminal_print_inline(xhci_source_name(k_bootinfo_ptr->xhci_mmio_sources[i]));
            terminal_print_inline("\n");
        }
    }
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

static int dihos_cmd_sys_which(dihos_shell_stage *stage)
{
    const dihos_shell_command *cmd = 0;
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];

    if (stage->positional_count == 0u)
    {
        terminal_error("which needs a command name");
        return -1;
    }

    cmd = dihos_find_command(stage->positional[0]);
    if (cmd)
    {
        dihos_print_label_value("command: ", cmd->name);
        terminal_print("type: builtin");
        dihos_print_label_value("usage: ", cmd->usage);
        return 0;
    }

    if (dihos_shell_fallback_available(stage->positional[0], friendly, raw))
    {
        dihos_print_label_value("command: ", stage->positional[0]);
        terminal_print("type: shell script fallback");
        dihos_print_label_value("script: ", friendly);
        dihos_print_label_value("raw: ", raw);
        return 0;
    }

    terminal_error("command not found");
    return -1;
}

static int dihos_cmd_sys_trace(dihos_shell_stage *stage)
{
    const char *mode = stage->positional_count > 0u ? stage->positional[0] : "status";

    if (strcmp(mode, "on") == 0 || strcmp(mode, "yes") == 0 || strcmp(mode, "1") == 0)
    {
        G_shell_trace = 1u;
        terminal_success("trace enabled");
        return 0;
    }

    if (strcmp(mode, "off") == 0 || strcmp(mode, "no") == 0 || strcmp(mode, "0") == 0)
    {
        G_shell_trace = 0u;
        terminal_success("trace disabled");
        return 0;
    }

    if (strcmp(mode, "status") != 0)
    {
        terminal_error("trace needs on, off, or status");
        return -1;
    }

    terminal_print_inline("trace: ");
    terminal_print_inline(G_shell_trace ? "on\n" : "off\n");
    return 0;
}

static int dihos_cmd_wifi_networks(dihos_shell_stage *stage)
{
    const char *refresh_text = dihos_stage_named(stage, "refresh");
    uint32_t count = 0u;
    int refresh = dihos_is_yes(refresh_text);
    int auto_refresh = 0;

    terminal_print_inline("\n");

    if (!refresh)
    {
        (void)kwifi_network_poll(32u);
        count = kwifi_network_count();
        if (count == 0u && !kwifi_network_scan_running())
        {
            refresh = 1;
            auto_refresh = 1;
        }
        else if (count == 0u)
        {
            terminal_warn("wifi scan already running; waiting for results");
            terminal_print_inline("\n");
            (void)kwifi_network_poll(KWIFI_NETWORK_SCAN_POLL_ROUNDS);
        }
    }

    if (refresh)
    {
        if (kwifi_network_refresh())
        {
            terminal_success(auto_refresh ? "wifi scan auto-refresh queued" : "wifi scan refresh queued");
            terminal_print_inline("\n");
            terminal_print_inline("wifi scan listening for results...\n");
            (void)kwifi_network_poll(KWIFI_NETWORK_SCAN_POLL_ROUNDS);
        }
        else
        {
            terminal_warn(kwifi_network_scan_running() ? "wifi scan refresh skipped; previous scan still running" : "wifi scan refresh skipped");
            terminal_print_inline("\n");
            (void)kwifi_network_poll(kwifi_network_scan_running() ? KWIFI_NETWORK_SCAN_POLL_ROUNDS : 32u);
        }
    }

    count = kwifi_network_count();

    terminal_print("wifi networks: ");
    dihos_print_dec_value(count);
    terminal_print_inline("\n");

    if (count == 0u)
    {
        terminal_warn("no WiFi networks discovered yet");
        return 0;
    }

    for (uint32_t i = 0u; i < count; ++i)
    {
        const char *name = kwifi_network_name(i);

        terminal_print_inline("  #");
        dihos_print_dec_value(i + 1u);
        terminal_print_inline(": ");
        if (kwifi_network_hidden(i) || !name || name[0] == 0)
            terminal_print_inline("<hidden>");
        else
            terminal_print_inline(name);
        terminal_print_inline("\n");
    }

    return 0;
}

static int dihos_cmd_wifi_connect(dihos_shell_stage *stage)
{
    const char *ssid = dihos_stage_named(stage, "ssid");
    const char *username = dihos_stage_named(stage, "username");
    const char *password = dihos_stage_named(stage, "password");
    const char *bssid = dihos_stage_named(stage, "bssid");
    const char *channel = dihos_stage_named(stage, "channel");
    const char *automate_text = dihos_stage_named(stage, "automate");
    uint32_t automate = 1u;

    if (!ssid && stage->positional_count > 0u)
        ssid = stage->positional[0];

    if (automate_text && automate_text[0])
        automate = dihos_is_yes(automate_text) ? 1u : 0u;

    if (!ssid || !ssid[0])
    {
        terminal_error("wifi:connect needs ssid=NAME");
        return -1;
    }

    if (!password || !password[0])
    {
        terminal_error("wifi:connect needs password=PASS");
        return -1;
    }

    if (!kwifi_connect_request(ssid, username, password, bssid, channel))
    {
        terminal_error("wifi connect request failed");
        return -1;
    }

    if (username && username[0] && automate)
    {
        for (uint32_t i = 0u; i < 4u; ++i)
            (void)kwifi_poll_connection(8u);

        if (kwifi_current_connected())
            terminal_success("enterprise auth completed; link reports connected");
        else
            terminal_warn("enterprise EAPOL engine started; PEAP/TLS/MSCHAPv2 still required");
    }

    terminal_success("wifi connect request queued");
    return 0;
}

static int dihos_cmd_wifi_current(dihos_shell_stage *stage)
{
    const char *ssid = kwifi_current_ssid();
    const char *username = kwifi_current_username();
    const char *status = kwifi_current_status();
    const char *auth_mode = kwifi_current_auth_mode();
    const char *eap_phase = kwifi_current_eap_phase();
    const char *peap_phase = kwifi_current_peap_phase();
    (void)stage;

    terminal_print("wifi current:");
    terminal_print_inline("  connected: ");
    terminal_print_inline(kwifi_current_connected() ? "yes\n" : "no\n");
    terminal_print_inline("  ssid: ");
    terminal_print_inline((ssid && ssid[0]) ? ssid : "<none>");
    terminal_print_inline("\n");
    terminal_print_inline("  username: ");
    terminal_print_inline((username && username[0]) ? username : "<none>");
    terminal_print_inline("\n");
    terminal_print_inline("  auth_mode: ");
    terminal_print_inline((auth_mode && auth_mode[0]) ? auth_mode : "none");
    terminal_print_inline("\n");
    terminal_print_inline("  eap_phase: ");
    terminal_print_inline((eap_phase && eap_phase[0]) ? eap_phase : "none");
    terminal_print_inline("\n");
    terminal_print_inline("  peap_phase: ");
    terminal_print_inline((peap_phase && peap_phase[0]) ? peap_phase : "none");
    terminal_print_inline("\n");
    terminal_print_inline("  status: ");
    terminal_print_inline((status && status[0]) ? status : "idle");
    terminal_print_inline("\n");
    terminal_print_inline("  rx_queue: ");
    dihos_print_dec_value(kwifi_rx_queue_count());
    terminal_print_inline(" (dropped ");
    dihos_print_dec_value(kwifi_rx_queue_dropped());
    terminal_print_inline(")\n");
    return 0;
}

static int dihos_cmd_wifi_supplicant(dihos_shell_stage *stage)
{
    const char *ready_text = dihos_stage_named(stage, "ready");
    uint32_t ready = 0u;

    if (!ready_text || !ready_text[0])
    {
        terminal_error("wifi:supplicant needs ready=yes|no");
        return -1;
    }

    ready = dihos_is_yes(ready_text) ? 1u : 0u;
    if (!kwifi_set_supplicant_ready(ready))
    {
        terminal_error("wifi:supplicant update rejected (enterprise connect not active)");
        return -1;
    }

    terminal_success(ready ? "enterprise supplicant readiness hint set to ready" : "enterprise supplicant readiness hint set to not-ready");
    return 0;
}

static int dihos_cmd_wifi_get(dihos_shell_stage *stage)
{
    const char *url = dihos_stage_named(stage, "url");
    const char *max_text = dihos_stage_named(stage, "max");
    uint32_t max_bytes = 8192u;

    if (!url && stage->positional_count > 0u)
        url = stage->positional[0];

    if (!url || !url[0])
    {
        terminal_error("wifi:get needs a URL");
        return -1;
    }

    if (max_text && max_text[0])
    {
        if (dihos_parse_u32(max_text, &max_bytes) != 0)
        {
            terminal_error("wifi:get max must be an integer");
            return -1;
        }

        if (max_bytes < 128u)
            max_bytes = 128u;
        if (max_bytes > 65536u)
            max_bytes = 65536u;
    }

    if (!kwifi_current_connected())
    {
        terminal_error("wifi:get requires an active WiFi connection");
        return -1;
    }

    if (dihos_starts_with(url, "file://"))
    {
        const char *path = url + 7u;
        char friendly[DIHOS_SHELL_PATH_CAP];
        char raw[DIHOS_SHELL_PATH_CAP];
        const char *open_path = 0;
        KFile file;
        char buf[512];
        uint32_t total = 0u;
        uint8_t saw_output = 0u;
        uint8_t ended_with_newline = 1u;

        if (!path || !path[0])
        {
            terminal_error("wifi:get file:// URL is missing a path");
            return -1;
        }

        if (path[0] && path[1] && path[2] && path[1] == ':' && path[2] == '/')
            open_path = path;
        else
        {
            if (dihos_resolve_raw_path(path, friendly, raw) != 0)
            {
                terminal_error("wifi:get file:// path is invalid");
                return -1;
            }
            open_path = raw;
        }

        if (kfile_open(&file, open_path, KFILE_READ) != 0)
        {
            terminal_error("wifi:get cannot open file URL target");
            return -1;
        }

        for (;;)
        {
            uint32_t got = 0u;
            uint32_t want = (uint32_t)sizeof(buf) - 1u;
            uint32_t remaining = (total < max_bytes) ? (max_bytes - total) : 0u;
            if (remaining < want)
                want = remaining;
            if (want == 0u)
                break;

            if (kfile_read(&file, buf, want, &got) != 0 || got == 0u)
                break;

            buf[got] = 0;
            terminal_print_inline(buf);
            ended_with_newline = (buf[got - 1u] == '\n') ? 1u : 0u;
            saw_output = 1u;
            total += got;
        }

        kfile_close(&file);

        if (saw_output && !ended_with_newline)
            terminal_print_inline("\n");

        if (total >= max_bytes)
            terminal_warn("wifi:get output truncated by max= limit");

        return 0;
    }

    if (dihos_starts_with(url, "http://") || dihos_starts_with(url, "https://"))
    {
        terminal_error("wifi:get http(s) transport not ready");
        terminal_print("note: WiFi auth is up, but DHCP/DNS/TCP/HTTP data path is not integrated yet");
        return -1;
    }

    terminal_error("wifi:get supports file://, http://, or https:// URLs");
    return -1;
}

static int dihos_cmd_wifi_rx(dihos_shell_stage *stage)
{
    const char *drain_text = dihos_stage_named(stage, "drain");
    const char *max_text = dihos_stage_named(stage, "max");
    uint32_t drain = 0u;
    uint32_t max_frames = 4u;
    uint32_t queued = kwifi_rx_queue_count();
    uint32_t dropped = kwifi_rx_queue_dropped();

    if (drain_text && drain_text[0])
        drain = dihos_is_yes(drain_text) ? 1u : 0u;
    if (max_text && max_text[0])
    {
        if (dihos_parse_u32(max_text, &max_frames) != 0)
        {
            terminal_error("wifi:rx max must be an integer");
            return -1;
        }
        if (max_frames == 0u)
            max_frames = 1u;
        if (max_frames > 32u)
            max_frames = 32u;
    }

    terminal_print("wifi rx:");
    terminal_print_inline("  queued: ");
    dihos_print_dec_value(queued);
    terminal_print_inline("\n");
    terminal_print_inline("  dropped: ");
    dihos_print_dec_value(dropped);
    terminal_print_inline("\n");

    if (!drain)
        return 0;

    for (uint32_t i = 0u; i < max_frames; ++i)
    {
        uint8_t frame[2048];
        uint32_t frame_len = 0u;
        uint32_t frame_kind = 0u;
        uint32_t preview = 0u;

        if (!kwifi_rx_frame_pop(frame, sizeof(frame), &frame_len, &frame_kind))
            break;

        terminal_print("  frame ");
        dihos_print_dec_value(i + 1u);
        terminal_print(" kind=");
        terminal_print_inline_hex64(frame_kind);
        uint8_t is_dp_payload = 0u;
        uint8_t is_htt_msg = 0u;
        uint8_t is_mgmt_frame = 0u;
        terminal_print(" class=");
        if (frame_kind & 0x80000000u)
        {
            is_dp_payload = 1u;
            terminal_print_inline("dp-payload");
        }
        else if (frame_kind & 0x40000000u)
            terminal_print_inline("dp-unmatched-desc");
        else if ((frame_kind & 0xFF000000u) == 0x20000000u || frame_kind <= 0xFFu)
        {
            is_htt_msg = 1u;
            terminal_print_inline("htt-msg");
        }
        else if ((frame_kind & 0xFF000000u) == 0x10000000u)
        {
            is_mgmt_frame = 1u;
            terminal_print_inline("wmi-mgmt-rx");
        }
        else
            terminal_print_inline("unknown");
        terminal_print(" len=");
        dihos_print_dec_value(frame_len);
        terminal_print_inline("\n");

        if (is_dp_payload && frame_len >= 14u)
        {
            uint16_t ethertype = (uint16_t)((uint16_t)frame[12] << 8) | (uint16_t)frame[13];
            terminal_print_inline("    ethertype: ");
            terminal_print_inline_hex8((uint8_t)(ethertype >> 8));
            terminal_print_inline_hex8((uint8_t)(ethertype & 0xFFu));
            if (ethertype == 0x0800u)
                terminal_print_inline(" (IPv4)");
            else if (ethertype == 0x0806u)
                terminal_print_inline(" (ARP)");
            else if (ethertype == 0x86DDu)
                terminal_print_inline(" (IPv6)");
            terminal_print_inline("\n");
        }
        else if (is_htt_msg)
        {
            uint8_t msg_id = (uint8_t)(frame_kind & 0xFFu);
            terminal_print_inline("    htt_msg_id: ");
            terminal_print_inline_hex8(msg_id);
            terminal_print_inline("\n");
        }
        else if (is_mgmt_frame && frame_len >= 2u)
        {
            uint16_t fc = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
            uint8_t subtype = (uint8_t)((fc >> 4) & 0xFu);
            terminal_print_inline("    mgmt_subtype: ");
            terminal_print_inline_hex8(subtype);
            if (subtype == 0u)
                terminal_print_inline(" (assoc-req)");
            else if (subtype == 1u)
                terminal_print_inline(" (assoc-resp)");
            else if (subtype == 2u)
                terminal_print_inline(" (reassoc-req)");
            else if (subtype == 3u)
                terminal_print_inline(" (reassoc-resp)");
            else if (subtype == 10u)
                terminal_print_inline(" (disassoc)");
            else if (subtype == 11u)
                terminal_print_inline(" (auth)");
            else if (subtype == 12u)
                terminal_print_inline(" (deauth)");
            else if (subtype == 13u)
                terminal_print_inline(" (action)");
            terminal_print_inline("\n");
        }

        preview = frame_len;
        if (preview > 24u)
            preview = 24u;
        terminal_print_inline("    bytes:");
        for (uint32_t b = 0u; b < preview; ++b)
        {
            terminal_print_inline(" ");
            terminal_print_inline_hex8(frame[b]);
        }
        if (frame_len > preview)
            terminal_print_inline(" ...");
        terminal_print_inline("\n");
    }

    return 0;
}

static void dihos_stage_shift_positionals(dihos_shell_stage *dst, const dihos_shell_stage *src, uint32_t shift)
{
    if (!dst || !src)
        return;

    *dst = *src;
    dst->positional_count = 0u;
    for (uint32_t i = shift; i < src->positional_count && dst->positional_count < DIHOS_SHELL_ARG_MAX; ++i)
        dst->positional[dst->positional_count++] = src->positional[i];
}

static int dihos_cmd_wifi_group(dihos_shell_stage *stage)
{
    dihos_shell_stage sub;
    const char *mode = stage->positional_count > 0u ? stage->positional[0] : "scan";

    dihos_stage_shift_positionals(&sub, stage, 1u);

    if (strcmp(mode, "scan") == 0 || strcmp(mode, "networks") == 0 || strcmp(mode, "list") == 0)
    {
        if (strcmp(mode, "scan") == 0)
            (void)dihos_stage_add_named(&sub, "refresh", "yes");
        return dihos_cmd_wifi_networks(&sub);
    }
    if (strcmp(mode, "current") == 0 || strcmp(mode, "status") == 0)
        return dihos_cmd_wifi_current(&sub);
    if (strcmp(mode, "connect") == 0)
        return dihos_cmd_wifi_connect(&sub);
    if (strcmp(mode, "supplicant") == 0)
        return dihos_cmd_wifi_supplicant(&sub);
    if (strcmp(mode, "get") == 0)
        return dihos_cmd_wifi_get(&sub);
    if (strcmp(mode, "rx") == 0)
        return dihos_cmd_wifi_rx(&sub);

    terminal_error("wifi needs scan, current, connect, supplicant, get, or rx");
    return -1;
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

static int dihos_cmd_shell_fallback(dihos_shell_stage *stage)
{
    dihos_shell_stage run_stage;
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    char named_args[DIHOS_SHELL_ARG_MAX][DIHOS_SCRIPT_VAR_VALUE_CAP];
    uint32_t i = 0u;
    int rc = 0;

    if (!stage || !stage->name)
        return -1;

    if (!dihos_shell_fallback_available(stage->name, friendly, raw))
    {
        dihos_error3("unknown DIHOS command ", stage->name, 0);
        return -1;
    }

    if (G_shell_fallback_depth >= 4u)
    {
        terminal_error("shell script fallback recursion limit reached");
        return -1;
    }

    memset(&run_stage, 0, sizeof(run_stage));
    run_stage.session = stage->session;
    run_stage.name = "sys:run";
    run_stage.stdin_text = stage->stdin_text;
    run_stage.stdin_len = stage->stdin_len;
    run_stage.capture_only = stage->capture_only;
    run_stage.has_stdin = stage->has_stdin;
    run_stage.positional[run_stage.positional_count++] = friendly;

    for (i = 0u; i < stage->positional_count && run_stage.positional_count < DIHOS_SHELL_ARG_MAX; ++i)
        run_stage.positional[run_stage.positional_count++] = stage->positional[i];

    for (i = 0u; i < stage->named_count && run_stage.positional_count < DIHOS_SHELL_ARG_MAX; ++i)
    {
        ksb b;
        ksb_init(&b, named_args[i], sizeof(named_args[i]));
        ksb_puts(&b, stage->named[i].key);
        ksb_putc(&b, '=');
        ksb_puts(&b, stage->named[i].value);
        if (!b.overflow)
            run_stage.positional[run_stage.positional_count++] = named_args[i];
    }

    if (G_shell_trace && !stage->capture_only)
    {
        dihos_print_label_value("trace: fallback script: ", friendly);
        dihos_print_label_value("trace: fallback raw: ", raw);
    }

    ++G_shell_fallback_depth;
    rc = dihos_cmd_sys_run(&run_stage);
    --G_shell_fallback_depth;
    return rc;
}

static int dihos_mkdir_parents_friendly(const char *friendly)
{
    char partial[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    uint32_t len = 0u;

    if (!friendly || friendly[0] != '/')
        return -1;
    if (strcmp(friendly, "/") == 0)
        return 0;

    partial[0] = '/';
    partial[1] = 0;
    len = 1u;

    for (uint32_t i = 1u;; ++i)
    {
        char ch = friendly[i];
        uint8_t at_end = ch == 0;

        if (ch != '/' && ch != 0)
        {
            if (len + 1u >= sizeof(partial))
                return -1;
            partial[len++] = ch;
            partial[len] = 0;
            continue;
        }

        if (len > 1u)
        {
            int kind = 0;
            if (dihos_path_friendly_to_raw(partial, raw, sizeof(raw)) != 0)
                return -1;
            kind = dihos_path_kind_raw(raw);
            if (kind == 1)
                return -1;
            if (kind == 0 && kfile_mkdir(raw) != 0)
                return -1;
        }

        if (at_end)
            break;

        if (len > 1u && partial[len - 1u] != '/')
        {
            if (len + 1u >= sizeof(partial))
                return -1;
            partial[len++] = '/';
            partial[len] = 0;
        }
    }

    return 0;
}

static int dihos_resolve_destination_path(const char *src_friendly, const char *dst_input,
                                          char *dst_friendly, char *dst_raw)
{
    char base_friendly[DIHOS_SHELL_PATH_CAP];
    char base_raw[DIHOS_SHELL_PATH_CAP];

    if (!src_friendly || !dst_input || !dst_friendly || !dst_raw)
        return -1;

    if (dihos_resolve_raw_path(dst_input, base_friendly, base_raw) != 0)
        return -1;

    if (dihos_path_kind_raw(base_raw) == 2)
    {
        const char *base = dihos_path_basename(src_friendly);
        if (!base || !base[0] || strcmp(base, "/") == 0)
            return -1;
        if (dihos_path_join_friendly(base_friendly, base, dst_friendly, DIHOS_SHELL_PATH_CAP) != 0)
            return -1;
        return dihos_path_friendly_to_raw(dst_friendly, dst_raw, DIHOS_SHELL_PATH_CAP);
    }

    dihos_copy_trunc(dst_friendly, DIHOS_SHELL_PATH_CAP, base_friendly);
    dihos_copy_trunc(dst_raw, DIHOS_SHELL_PATH_CAP, base_raw);
    return 0;
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
    uint8_t parents = dihos_is_yes(dihos_stage_named(stage, "parents"));

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

    if (parents)
    {
        if (dihos_mkdir_parents_friendly(friendly) != 0)
        {
            terminal_error("failed to create parent directories");
            return -1;
        }
        terminal_success("directory created");
        return 0;
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
    else if (stage->positional_count == 2u && dihos_is_console_runtime_token(stage->positional[1]))
    {
        text = dihos_current_console_text();
    }
    else if (dihos_stage_named(stage, "text"))
    {
        text = dihos_stage_named(stage, "text");
        if (dihos_is_console_runtime_token(text))
            text = dihos_current_console_text();
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

static int dihos_cmd_fs_touch(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KFile file;

    if (stage->positional_count == 0u)
    {
        terminal_error("touch needs a path");
        return -1;
    }

    if (dihos_require_unsafe(stage, stage->name) != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (dihos_path_kind_raw(raw) == 2)
    {
        terminal_error("touch target is a directory");
        return -1;
    }

    if (kfile_open(&file, raw, KFILE_WRITE | KFILE_CREATE) != 0)
    {
        terminal_error("touch failed");
        return -1;
    }

    kfile_close(&file);
    terminal_success("file touched");
    return 0;
}

static int dihos_cmd_fs_append(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    char joined[1024];
    const char *text = 0;
    KFile file;
    uint32_t wrote = 0;

    if (stage->positional_count == 0u)
    {
        terminal_error("append needs a path");
        return -1;
    }

    if (dihos_require_unsafe(stage, stage->name) != 0)
        return -1;

    if (stage->has_stdin)
        text = stage->stdin_text;
    else if (dihos_stage_named(stage, "text"))
        text = dihos_stage_named(stage, "text");
    else
    {
        if (dihos_join_positionals(stage, 1u, joined, sizeof(joined)) != 0)
        {
            terminal_error("append text is too long");
            return -1;
        }
        text = joined;
    }

    if (dihos_is_console_runtime_token(text))
        text = dihos_current_console_text();

    if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (kfile_open(&file, raw, KFILE_WRITE | KFILE_CREATE | KFILE_APPEND) != 0)
    {
        terminal_error("cannot open file for append");
        return -1;
    }

    if (text && text[0] && kfile_write(&file, text, (uint32_t)strlen(text), &wrote) != 0)
    {
        kfile_close(&file);
        terminal_error("append failed");
        return -1;
    }

    kfile_close(&file);
    terminal_success("file appended");
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
    int src_kind = 0;
    int dst_kind = 0;
    uint8_t replace = dihos_is_yes(dihos_stage_named(stage, "replace")) ||
                      dihos_is_yes(dihos_stage_named(stage, "force"));

    if (stage->positional_count < 2u)
    {
        terminal_error("fs:copy needs src and dst");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:copy") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], src_friendly, src_raw) != 0 ||
        dihos_resolve_destination_path(src_friendly, stage->positional[1], dst_friendly, dst_raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    src_kind = dihos_path_kind_raw(src_raw);
    if (src_kind == 0)
    {
        terminal_error("copy source not found");
        return -1;
    }
    if (src_kind == 2)
    {
        terminal_error("copy currently supports files, not directories");
        return -1;
    }

    dst_kind = dihos_path_kind_raw(dst_raw);
    if (dst_kind == 2)
    {
        terminal_error("copy destination is a directory");
        return -1;
    }
    if (dst_kind == 1 && !replace)
    {
        terminal_error("copy destination exists; use -f or replace=yes");
        return -1;
    }
    if (dst_kind == 1 && replace && kfile_unlink(dst_raw) != 0)
    {
        terminal_error("copy could not replace destination");
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
    int src_kind = 0;
    int dst_kind = 0;
    uint8_t replace = dihos_is_yes(dihos_stage_named(stage, "replace")) ||
                      dihos_is_yes(dihos_stage_named(stage, "force"));

    if (stage->positional_count < 2u)
    {
        terminal_error("fs:move needs src and dst");
        return -1;
    }

    if (dihos_require_unsafe(stage, "fs:move") != 0)
        return -1;

    if (dihos_resolve_raw_path(stage->positional[0], src_friendly, src_raw) != 0 ||
        dihos_resolve_destination_path(src_friendly, stage->positional[1], dst_friendly, dst_raw) != 0)
    {
        terminal_error("invalid path");
        return -1;
    }

    if (strcmp(src_friendly, "/") == 0)
    {
        terminal_error("refusing to move shell root");
        return -1;
    }

    if (strcmp(src_raw, dst_raw) == 0)
    {
        terminal_success("move complete");
        return 0;
    }

    src_kind = dihos_path_kind_raw(src_raw);
    if (src_kind == 0)
    {
        terminal_error("move source not found");
        return -1;
    }

    dst_kind = dihos_path_kind_raw(dst_raw);
    if (dst_kind == 2)
    {
        terminal_error("move destination is a directory");
        return -1;
    }
    if (dst_kind == 1 && !replace)
    {
        terminal_error("move destination exists; use -f or replace=yes");
        return -1;
    }
    if (dst_kind == 1 && replace && kfile_unlink(dst_raw) != 0)
    {
        terminal_error("move could not replace destination");
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
    uint8_t force = dihos_is_yes(dihos_stage_named(stage, "force"));

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

    if (force && dihos_path_kind_raw(raw) == 0)
        return 0;

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

static void dihos_find_walk(const char *raw, const char *friendly, const char *needle,
                            uint32_t depth, uint32_t max_depth, uint32_t *printed)
{
    KDir dir;
    kdirent ent;

    if (!raw || !friendly || depth > max_depth)
        return;

    if (!needle || !needle[0] || strstr(dihos_path_basename(friendly), needle))
    {
        terminal_print(friendly);
        if (printed)
            (*printed)++;
    }

    if (depth == max_depth || kdir_open(&dir, raw) != 0)
        return;

    while (kdir_next(&dir, &ent) > 0)
    {
        char child_raw[DIHOS_SHELL_PATH_CAP];
        char child_friendly[DIHOS_SHELL_PATH_CAP];

        if (dihos_join_raw_path(raw, ent.name, child_raw, sizeof(child_raw)) != 0 ||
            dihos_path_join_friendly(friendly, ent.name, child_friendly, sizeof(child_friendly)) != 0)
            continue;

        dihos_find_walk(child_raw, child_friendly, needle, depth + 1u, max_depth, printed);
    }

    kdir_close(&dir);
}

static int dihos_cmd_fs_find(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    const char *needle = dihos_stage_named(stage, "name");
    const char *max_text = dihos_stage_named(stage, "max");
    uint32_t max_depth = 6u;
    uint32_t printed = 0u;

    if (!needle && stage->positional_count > 1u)
        needle = stage->positional[1];
    if (max_text && dihos_parse_u32(max_text, &max_depth) != 0)
    {
        terminal_error("find max must be an integer");
        return -1;
    }
    if (max_depth > 16u)
        max_depth = 16u;

    if (dihos_resolve_path_or_cwd(stage->positional_count > 0u ? stage->positional[0] : G_shell.cwd,
                                  friendly, raw) != 0)
        return -1;

    if (dihos_path_kind_raw(raw) == 0)
    {
        terminal_error("find path not found");
        return -1;
    }

    dihos_find_walk(raw, friendly, needle, 0u, max_depth, &printed);
    if (printed == 0u)
        return 1;
    return 0;
}

static void dihos_tree_walk(const char *raw, const char *friendly, uint32_t depth, uint32_t max_depth)
{
    KDir dir;
    kdirent ent;

    for (uint32_t i = 0u; i < depth; ++i)
        terminal_print_inline("  ");
    terminal_print_inline(dihos_path_basename(friendly));
    if (dihos_path_kind_raw(raw) == 2)
        terminal_print_inline("/");
    terminal_print_inline("\n");

    if (depth >= max_depth || kdir_open(&dir, raw) != 0)
        return;

    while (kdir_next(&dir, &ent) > 0)
    {
        char child_raw[DIHOS_SHELL_PATH_CAP];
        char child_friendly[DIHOS_SHELL_PATH_CAP];

        if (dihos_join_raw_path(raw, ent.name, child_raw, sizeof(child_raw)) != 0 ||
            dihos_path_join_friendly(friendly, ent.name, child_friendly, sizeof(child_friendly)) != 0)
            continue;

        dihos_tree_walk(child_raw, child_friendly, depth + 1u, max_depth);
    }

    kdir_close(&dir);
}

static int dihos_cmd_fs_tree(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    const char *max_text = dihos_stage_named(stage, "max");
    uint32_t max_depth = 4u;

    if (max_text && dihos_parse_u32(max_text, &max_depth) != 0)
    {
        terminal_error("tree max must be an integer");
        return -1;
    }
    if (max_depth > 12u)
        max_depth = 12u;

    if (dihos_resolve_path_or_cwd(stage->positional_count > 0u ? stage->positional[0] : G_shell.cwd,
                                  friendly, raw) != 0)
        return -1;

    if (dihos_path_kind_raw(raw) == 0)
    {
        terminal_error("tree path not found");
        return -1;
    }

    dihos_tree_walk(raw, friendly, 0u, max_depth);
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

static int dihos_cmd_hw_group(dihos_shell_stage *stage)
{
    dihos_shell_stage sub;
    const char *mode = stage->positional_count > 0u ? stage->positional[0] : "";

    if (!mode[0])
    {
        terminal_error("hw needs acpi, touchpad, or gpio");
        return -1;
    }

    dihos_stage_shift_positionals(&sub, stage, 1u);
    if (strcmp(mode, "acpi") == 0)
        return dihos_cmd_hw_acpi(&sub);
    if (strcmp(mode, "touchpad") == 0)
        return dihos_cmd_hw_touchpad(&sub);
    if (strcmp(mode, "gpio") == 0)
        return dihos_cmd_hw_gpio(&sub);

    terminal_error("hw needs acpi, touchpad, or gpio");
    return -1;
}

static int dihos_char_equal_fold(char a, char b)
{
    return dihos_lower_ascii(a) == dihos_lower_ascii(b);
}

static int dihos_strstr_ci(const char *haystack, const char *needle)
{
    uint32_t needle_len = 0u;

    if (!haystack || !needle)
        return 0;
    if (!needle[0])
        return 1;

    needle_len = (uint32_t)strlen(needle);
    for (uint32_t i = 0u; haystack[i]; ++i)
    {
        uint32_t j = 0u;
        while (j < needle_len && haystack[i + j] &&
               dihos_char_equal_fold(haystack[i + j], needle[j]))
            ++j;
        if (j == needle_len)
            return 1;
    }

    return 0;
}

static int dihos_text_resolve_lines(const dihos_shell_stage *stage, uint32_t *out_lines)
{
    const char *lines_text = dihos_stage_named(stage, "lines");
    uint32_t lines = 10u;

    if (!lines_text && stage->positional_count > 0u)
    {
        const char *pos = stage->positional[0];
        if (pos && pos[0] == '-' && pos[1] >= '0' && pos[1] <= '9')
            lines_text = pos + 1u;
        else
            lines_text = pos;
    }

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
    uint32_t line_no = 1u;
    uint8_t ignore_case = dihos_is_yes(dihos_stage_named(stage, "ignore_case"));
    uint8_t invert = dihos_is_yes(dihos_stage_named(stage, "invert"));
    uint8_t number = dihos_is_yes(dihos_stage_named(stage, "number"));

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

            {
                int matched = ignore_case ? dihos_strstr_ci(line, needle) : (strstr(line, needle) != 0);
                if (invert)
                    matched = !matched;
                if (matched)
                {
                    if (number)
                    {
                        dihos_print_dec_value(line_no);
                        terminal_print_inline(":");
                    }
                    terminal_print(line);
                }
            }
        }

        start = i + 1u;
        ++line_no;
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

static void dihos_count_text_units(const char *text, uint32_t len, uint64_t *lines, uint64_t *words, uint64_t *chars)
{
    uint8_t in_word = 0u;

    if (lines)
        *lines = 0u;
    if (words)
        *words = 0u;
    if (chars)
        *chars = len;

    for (uint32_t i = 0u; i < len; ++i)
    {
        if (text[i] == '\n' && lines)
            (*lines)++;

        if (dihos_is_space(text[i]))
            in_word = 0u;
        else if (!in_word)
        {
            in_word = 1u;
            if (words)
                (*words)++;
        }
    }

    if (lines && len > 0u && text[len - 1u] != '\n')
        (*lines)++;
}

static int dihos_cmd_text_wc(dihos_shell_stage *stage)
{
    const char *mode = dihos_stage_named(stage, "mode");
    uint64_t lines = 0u;
    uint64_t words = 0u;
    uint64_t chars = 0u;

    if (dihos_require_stdin(stage, "wc needs piped stdin") != 0)
        return -1;

    dihos_count_text_units(stage->stdin_text, stage->stdin_len, &lines, &words, &chars);

    if (!mode && dihos_is_yes(dihos_stage_named(stage, "long")))
        mode = "lines";
    if (!mode && dihos_stage_named(stage, "lines"))
        mode = "lines";

    if (!mode)
    {
        dihos_print_dec_value(lines);
        terminal_print_inline(" ");
        dihos_print_dec_value(words);
        terminal_print_inline(" ");
        dihos_print_dec_value(chars);
        terminal_print_inline("\n");
        return 0;
    }

    if (strcmp(mode, "lines") == 0)
        dihos_print_dec_value(lines);
    else if (strcmp(mode, "words") == 0)
        dihos_print_dec_value(words);
    else if (strcmp(mode, "chars") == 0)
        dihos_print_dec_value(chars);
    else
    {
        terminal_error("wc mode must be lines, words, or chars");
        return -1;
    }
    terminal_print_inline("\n");
    return 0;
}

static int dihos_line_compare(const char *a, const char *b)
{
    return strcmp(a ? a : "", b ? b : "");
}

static int dihos_cmd_text_sort(dihos_shell_stage *stage)
{
    char lines[96][256];
    uint32_t line_count = 0u;
    uint32_t start = 0u;

    if (dihos_require_stdin(stage, "sort needs piped stdin") != 0)
        return -1;

    for (uint32_t i = 0u; i <= stage->stdin_len && line_count < 96u; ++i)
    {
        if (i != stage->stdin_len && stage->stdin_text[i] != '\n')
            continue;

        {
            uint32_t len = i - start;
            if (len >= sizeof(lines[0]))
                len = (uint32_t)sizeof(lines[0]) - 1u;
            memcpy(lines[line_count], stage->stdin_text + start, len);
            lines[line_count][len] = 0;
            ++line_count;
            start = i + 1u;
        }
    }

    for (uint32_t i = 1u; i < line_count; ++i)
    {
        char tmp[256];
        uint32_t j = i;
        dihos_copy_trunc(tmp, sizeof(tmp), lines[i]);
        while (j > 0u && dihos_line_compare(lines[j - 1u], tmp) > 0)
        {
            dihos_copy_trunc(lines[j], sizeof(lines[j]), lines[j - 1u]);
            --j;
        }
        dihos_copy_trunc(lines[j], sizeof(lines[j]), tmp);
    }

    for (uint32_t i = 0u; i < line_count; ++i)
        terminal_print(lines[i]);

    if (line_count == 96u)
        terminal_warn("sort output truncated at 96 lines");
    return 0;
}

static int dihos_cmd_text_uniq(dihos_shell_stage *stage)
{
    char prev[512];
    uint8_t have_prev = 0u;
    uint32_t start = 0u;

    if (dihos_require_stdin(stage, "uniq needs piped stdin") != 0)
        return -1;

    prev[0] = 0;
    for (uint32_t i = 0u; i <= stage->stdin_len; ++i)
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

            if (!have_prev || strcmp(prev, line) != 0)
            {
                terminal_print(line);
                dihos_copy_trunc(prev, sizeof(prev), line);
                have_prev = 1u;
            }
        }
        start = i + 1u;
    }

    return 0;
}

static int dihos_cmd_text_tee(dihos_shell_stage *stage)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KFile file;
    uint32_t wrote = 0u;
    uint32_t flags = KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC;

    if (dihos_require_stdin(stage, "tee needs piped stdin") != 0)
        return -1;

    if (stage->positional_count > 0u)
    {
        if (dihos_require_unsafe(stage, "tee") != 0)
            return -1;

        if (dihos_is_yes(dihos_stage_named(stage, "append")))
            flags = KFILE_WRITE | KFILE_CREATE | KFILE_APPEND;

        if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
        {
            terminal_error("invalid path");
            return -1;
        }

        if (kfile_open(&file, raw, flags) != 0)
        {
            terminal_error("tee cannot open output file");
            return -1;
        }

        if (stage->stdin_len > 0u &&
            kfile_write(&file, stage->stdin_text, stage->stdin_len, &wrote) != 0)
        {
            kfile_close(&file);
            terminal_error("tee write failed");
            return -1;
        }
        kfile_close(&file);
    }

    terminal_print_inline(stage->stdin_text);
    if (stage->stdin_len > 0u && stage->stdin_text[stage->stdin_len - 1u] != '\n')
        terminal_print_inline("\n");
    return 0;
}

static int dihos_cmd_text_yes(dihos_shell_stage *stage)
{
    const char *count_text = dihos_stage_named(stage, "count");
    char text[256];
    uint32_t count = 10u;

    if (count_text && dihos_parse_u32(count_text, &count) != 0)
    {
        terminal_error("yes count must be an integer");
        return -1;
    }
    if (count > 256u)
        count = 256u;

    if (stage->positional_count > 0u)
    {
        if (dihos_join_positionals(stage, 0u, text, sizeof(text)) != 0)
        {
            terminal_error("yes text is too long");
            return -1;
        }
    }
    else
    {
        dihos_copy_trunc(text, sizeof(text), "y");
    }

    for (uint32_t i = 0u; i < count; ++i)
        terminal_print(text);
    return 0;
}

static int dihos_cmd_text_cut(dihos_shell_stage *stage)
{
    const char *field_text = dihos_stage_named(stage, "field");
    const char *delim_text = dihos_stage_named(stage, "delim");
    char delim = ',';
    uint32_t field = 1u;
    uint32_t start = 0u;

    if (dihos_require_stdin(stage, "cut needs piped stdin") != 0)
        return -1;

    if (!field_text && stage->positional_count > 0u)
        field_text = stage->positional[0];
    if (!field_text || dihos_parse_u32(field_text, &field) != 0 || field == 0u)
    {
        terminal_error("cut needs field=N");
        return -1;
    }
    if (delim_text && delim_text[0])
        delim = delim_text[0];

    for (uint32_t i = 0u; i <= stage->stdin_len; ++i)
    {
        if (i != stage->stdin_len && stage->stdin_text[i] != '\n')
            continue;

        uint32_t current = 1u;
        uint32_t field_start = start;
        uint32_t field_end = i;

        for (uint32_t j = start; j < i; ++j)
        {
            if (stage->stdin_text[j] == delim)
            {
                if (current == field)
                {
                    field_end = j;
                    break;
                }
                ++current;
                field_start = j + 1u;
            }
        }

        if (current == field && field_start <= field_end)
            dihos_emit_text_span(stage->stdin_text + field_start, field_end - field_start, 1u);

        start = i + 1u;
    }

    return 0;
}

static void dihos_emit_hexdump_bytes(const uint8_t *data, uint32_t len, uint32_t base_offset)
{
    uint32_t offset = 0u;

    while (offset < len)
    {
        uint32_t take = len - offset;
        if (take > 16u)
            take = 16u;

        terminal_print_inline_hex32(base_offset + offset);
        terminal_print_inline(":");
        for (uint32_t i = 0u; i < take; ++i)
        {
            terminal_print_inline(" ");
            terminal_print_inline_hex8(data[offset + i]);
        }
        terminal_print_inline("\n");
        offset += take;
    }
}

static int dihos_cmd_file_hexdump(dihos_shell_stage *stage)
{
    const char *max_text = dihos_stage_named(stage, "max");
    uint32_t max_bytes = 512u;

    if (max_text && dihos_parse_u32(max_text, &max_bytes) != 0)
    {
        terminal_error("hexdump max must be an integer");
        return -1;
    }
    if (max_bytes > 4096u)
        max_bytes = 4096u;

    if (stage->has_stdin)
    {
        uint32_t len = stage->stdin_len < max_bytes ? stage->stdin_len : max_bytes;
        dihos_emit_hexdump_bytes((const uint8_t *)stage->stdin_text, len, 0u);
        return 0;
    }

    if (stage->positional_count > 0u)
    {
        char friendly[DIHOS_SHELL_PATH_CAP];
        char raw[DIHOS_SHELL_PATH_CAP];
        KFile file;
        uint8_t buf[256];
        uint32_t offset = 0u;

        if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
        {
            terminal_error("invalid path");
            return -1;
        }
        if (kfile_open(&file, raw, KFILE_READ) != 0)
        {
            terminal_error("hexdump cannot open file");
            return -1;
        }
        while (offset < max_bytes)
        {
            uint32_t want = max_bytes - offset;
            uint32_t got = 0u;
            if (want > sizeof(buf))
                want = (uint32_t)sizeof(buf);
            if (kfile_read(&file, buf, want, &got) != 0 || got == 0u)
                break;
            dihos_emit_hexdump_bytes(buf, got, offset);
            offset += got;
        }
        kfile_close(&file);
        return 0;
    }

    terminal_error("hexdump needs a path or piped stdin");
    return -1;
}

static int dihos_is_printable_ascii(char ch)
{
    return ch >= 32 && ch <= 126;
}

static void dihos_strings_feed_char(char ch, char *buf, uint32_t *len, uint32_t min)
{
    if (dihos_is_printable_ascii(ch))
    {
        if (*len + 1u < 160u)
        {
            buf[*len] = ch;
            (*len)++;
            buf[*len] = 0;
        }
        return;
    }

    if (*len >= min)
        terminal_print(buf);
    *len = 0u;
    buf[0] = 0;
}

static int dihos_cmd_file_strings(dihos_shell_stage *stage)
{
    const char *min_text = dihos_stage_named(stage, "min");
    uint32_t min_len = 4u;
    char seq[160];
    uint32_t seq_len = 0u;

    if (min_text && dihos_parse_u32(min_text, &min_len) != 0)
    {
        terminal_error("strings min must be an integer");
        return -1;
    }
    if (min_len == 0u)
        min_len = 1u;
    if (min_len > 64u)
        min_len = 64u;

    seq[0] = 0;
    if (stage->has_stdin)
    {
        for (uint32_t i = 0u; i < stage->stdin_len; ++i)
            dihos_strings_feed_char(stage->stdin_text[i], seq, &seq_len, min_len);
        dihos_strings_feed_char(0, seq, &seq_len, min_len);
        return 0;
    }

    if (stage->positional_count > 0u)
    {
        char friendly[DIHOS_SHELL_PATH_CAP];
        char raw[DIHOS_SHELL_PATH_CAP];
        KFile file;
        char buf[256];

        if (dihos_resolve_raw_path(stage->positional[0], friendly, raw) != 0)
        {
            terminal_error("invalid path");
            return -1;
        }
        if (kfile_open(&file, raw, KFILE_READ) != 0)
        {
            terminal_error("strings cannot open file");
            return -1;
        }
        for (;;)
        {
            uint32_t got = 0u;
            if (kfile_read(&file, buf, (uint32_t)sizeof(buf), &got) != 0 || got == 0u)
                break;
            for (uint32_t i = 0u; i < got; ++i)
                dihos_strings_feed_char(buf[i], seq, &seq_len, min_len);
        }
        kfile_close(&file);
        dihos_strings_feed_char(0, seq, &seq_len, min_len);
        return 0;
    }

    terminal_error("strings needs a path or piped stdin");
    return -1;
}

static int dihos_cmd_test_assert(dihos_shell_stage *stage)
{
    const char *mode = stage->positional_count > 0u ? stage->positional[0] : "";
    const char *path = stage->positional_count > 1u ? stage->positional[1] : "";
    int kind = 0;
    int ok = 0;

    if (!mode[0] || !path[0])
    {
        terminal_error("assert needs exists|isfile|isdir PATH");
        return -1;
    }

    kind = dihos_path_kind_friendly(path);
    if (strcmp(mode, "exists") == 0)
        ok = kind != 0;
    else if (strcmp(mode, "isfile") == 0)
        ok = kind == 1;
    else if (strcmp(mode, "isdir") == 0)
        ok = kind == 2;
    else
    {
        terminal_error("assert condition must be exists, isfile, or isdir");
        return -1;
    }

    if (!ok)
    {
        terminal_error("assert failed");
        return -1;
    }

    terminal_success("assert passed");
    return 0;
}

static int dihos_cmd_test_assert_eq(dihos_shell_stage *stage)
{
    if (stage->positional_count < 2u)
    {
        terminal_error("assert_eq needs lhs and rhs");
        return -1;
    }

    if (strcmp(stage->positional[0], stage->positional[1]) != 0)
    {
        terminal_error("assert_eq failed");
        return -1;
    }

    terminal_success("assert_eq passed");
    return 0;
}

static int dihos_cmd_test_fail(dihos_shell_stage *stage)
{
    char msg[256];

    if (stage->positional_count > 0u && dihos_join_positionals(stage, 0u, msg, sizeof(msg)) == 0)
        terminal_error(msg);
    else
        terminal_error("fail");
    return -1;
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

int dihos_shell_session_console_text(dihos_shell_session *session, char *out, uint32_t out_cap)
{
    if (!out || out_cap == 0u)
        return -1;

    out[0] = 0;
    if (!session)
        session = &G_default_shell;

    if (session->io.console_text)
        return session->io.console_text(out, out_cap, session->io.user);

    return 0;
}

void dihos_shell_session_set_title(dihos_shell_session *session, const char *title)
{
    if (!session)
        session = &G_default_shell;

    if (session->io.set_title)
        session->io.set_title(title ? title : "", session->io.user);
}
