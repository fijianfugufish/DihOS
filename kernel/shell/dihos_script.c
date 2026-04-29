#include "shell/dihos_shell.h"

#include "filesystem/dihos_path.h"
#include "system/dihos_time.h"
#include "kwrappers/kfile.h"
#include "kwrappers/string.h"
#include <stdint.h>

enum
{
    SCRIPT_BLOCK_IF = 1,
    SCRIPT_BLOCK_ELSE_IF = 2,
    SCRIPT_BLOCK_ELSE = 3,
    SCRIPT_BLOCK_END = 4,
    SCRIPT_BLOCK_WHILE = 5,
    SCRIPT_BLOCK_FOR = 6,
    SCRIPT_BLOCK_FN = 7
};

enum
{
    SCRIPT_LOOP_WHILE = 1,
    SCRIPT_LOOP_FOR = 2
};

typedef struct script_block_entry
{
    uint8_t kind;
    uint8_t seen_else;
    uint32_t line_index;
    int function_index;
} script_block_entry;

static int script_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int script_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int script_is_name_start(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int script_is_name_char(char ch)
{
    return script_is_name_start(ch) || script_is_digit(ch);
}

static char script_lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (char)(ch - 'A' + 'a');
    return ch;
}

static void script_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;

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

static char *script_trim(char *text)
{
    uint32_t len = 0u;

    if (!text)
        return text;

    while (script_is_space(*text))
        ++text;

    len = (uint32_t)strlen(text);
    while (len > 0u && script_is_space(text[len - 1u]))
        text[--len] = 0;

    return text;
}

static int script_keyword(const char *line, const char *keyword)
{
    uint32_t len = (uint32_t)strlen(keyword);

    if (!line || strncmp(line, keyword, len) != 0)
        return 0;

    return line[len] == 0 || script_is_space(line[len]);
}

static void script_u32_to_dec(uint32_t value, char *out, uint32_t cap)
{
    char tmp[16];
    uint32_t len = 0u;
    uint32_t i = 0u;

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

static void script_u64_to_dec(uint64_t value, char *out, uint32_t cap)
{
    char tmp[32];
    uint32_t len = 0u;
    uint32_t i = 0u;

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

static void script_i32_to_dec(int value, char *out, uint32_t cap)
{
    uint32_t magnitude = 0u;

    if (!out || cap == 0u)
        return;

    if (value < 0)
    {
        out[0] = '-';
        if (cap <= 1u)
            return;
        magnitude = (uint32_t)(-value);
        script_u32_to_dec(magnitude, out + 1u, cap - 1u);
        return;
    }

    script_u32_to_dec((uint32_t)value, out, cap);
}

static int script_parse_i32(const char *text, int *out)
{
    int sign = 1;
    int value = 0;
    uint32_t i = 0u;

    if (!text || !text[0] || !out)
        return -1;

    if (text[0] == '-')
    {
        sign = -1;
        i = 1u;
        if (!text[i])
            return -1;
    }
    else if (text[0] == '+')
    {
        i = 1u;
        if (!text[i])
            return -1;
    }

    for (; text[i]; ++i)
    {
        if (!script_is_digit(text[i]))
            return -1;
        value = (value * 10) + (int)(text[i] - '0');
    }

    *out = value * sign;
    return 0;
}

static int script_parse_double(const char *text, double *out)
{
    uint32_t i = 0u;
    int sign = 1;
    int saw_digit = 0;
    double value = 0.0;
    double place = 0.1;

    if (!text || !text[0] || !out)
        return -1;

    if (text[i] == '-')
    {
        sign = -1;
        ++i;
    }
    else if (text[i] == '+')
    {
        ++i;
    }

    while (script_is_digit(text[i]))
    {
        saw_digit = 1;
        value = (value * 10.0) + (double)(text[i] - '0');
        ++i;
    }

    if (text[i] == '.')
    {
        ++i;
        while (script_is_digit(text[i]))
        {
            saw_digit = 1;
            value += (double)(text[i] - '0') * place;
            place *= 0.1;
            ++i;
        }
    }

    if (!saw_digit || text[i] != 0)
        return -1;

    *out = (double)sign * value;
    return 0;
}

static void script_double_to_text(double value, char *out, uint32_t cap)
{
    char int_buf[32];
    char frac_buf[8];
    uint64_t scaled = 0u;
    uint64_t int_part = 0u;
    uint32_t frac_part = 0u;
    uint32_t frac_len = 6u;
    uint32_t i = 0u;
    int negative = 0;
    ksb b;

    if (!out || cap == 0u)
        return;

    if (value < 0.0)
    {
        negative = 1;
        value = -value;
    }

    if (value > 1000000000000.0)
        value = 1000000000000.0;

    scaled = (uint64_t)(value * 1000000.0 + 0.5);
    int_part = scaled / 1000000u;
    frac_part = (uint32_t)(scaled % 1000000u);
    script_u64_to_dec(int_part, int_buf, sizeof(int_buf));

    ksb_init(&b, out, cap);
    if (negative && (int_part != 0u || frac_part != 0u))
        ksb_putc(&b, '-');
    ksb_puts(&b, int_buf);

    if (frac_part == 0u)
        return;

    frac_buf[6] = 0;
    for (i = 0u; i < 6u; ++i)
    {
        uint32_t place = 1u;
        uint32_t digit = 0u;
        uint32_t idx = 5u - i;

        while (idx > 0u)
        {
            place *= 10u;
            --idx;
        }
        digit = frac_part / place;
        frac_part %= place;
        frac_buf[i] = (char)('0' + digit);
    }

    while (frac_len > 0u && frac_buf[frac_len - 1u] == '0')
        --frac_len;

    ksb_putc(&b, '.');
    for (i = 0u; i < frac_len; ++i)
        ksb_putc(&b, frac_buf[i]);
}

static uint32_t script_seed_mix(uint32_t seed)
{
    uint32_t x = seed;

    if (x == 0u)
        x = 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0u)
        x = 0x6D2B79F5u;
    return x;
}

static void script_rng_seed(dihos_script_runner *runner, uint32_t seed)
{
    if (!runner)
        return;

    if (seed == 0u)
        seed = (uint32_t)dihos_time_ticks();
    if (seed == 0u)
        seed = 0xA1C3E5F7u;

    runner->rng_state = script_seed_mix(seed);
    runner->rng_seeded = 1u;
}

static uint32_t script_rng_next(dihos_script_runner *runner)
{
    uint32_t x = 0u;

    if (!runner)
        return 0u;
    if (!runner->rng_seeded || runner->rng_state == 0u)
        script_rng_seed(runner, (uint32_t)dihos_time_ticks());

    x = runner->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0u)
        x = 0x6D2B79F5u;

    runner->rng_state = x;
    return x;
}

static uint32_t script_rng_bounded(dihos_script_runner *runner, uint32_t bound)
{
    uint64_t limit = 0u;
    uint32_t value = 0u;

    if (!runner || bound == 0u)
        return 0u;

    limit = (0x100000000ULL / (uint64_t)bound) * (uint64_t)bound;
    do
    {
        value = script_rng_next(runner);
    } while ((uint64_t)value >= limit);

    return (uint32_t)((uint64_t)value % (uint64_t)bound);
}

static int script_name_valid(const char *name)
{
    uint32_t i = 0u;

    if (!name || !script_is_name_start(name[0]))
        return 0;

    for (i = 1u; name[i]; ++i)
        if (!script_is_name_char(name[i]))
            return 0;

    return 1;
}

static dihos_script_call_frame *script_current_frame(dihos_script_runner *runner)
{
    if (!runner || runner->call_depth == 0u)
        return 0;
    return &runner->call_frames[runner->call_depth - 1u];
}

static int script_find_var_in_list(const dihos_script_var *vars, uint32_t count, const char *name)
{
    uint32_t i = 0u;

    if (!vars || !name)
        return -1;

    for (i = 0u; i < count; ++i)
    {
        if (strcmp(vars[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

static int script_set_var_in_list(dihos_script_var *vars, uint32_t *count, uint32_t cap,
                                  const char *name, const char *value)
{
    int idx = -1;

    if (!vars || !count || !script_name_valid(name))
        return -1;

    idx = script_find_var_in_list(vars, *count, name);
    if (idx < 0)
    {
        if (*count >= cap)
            return -1;
        idx = (int)(*count);
        (*count)++;
        script_copy(vars[idx].name, sizeof(vars[idx].name), name);
    }

    script_copy(vars[idx].value, sizeof(vars[idx].value), value ? value : "");
    return 0;
}

static void script_unset_var_in_list(dihos_script_var *vars, uint32_t *count, const char *name)
{
    int idx = -1;

    if (!vars || !count || !name)
        return;

    idx = script_find_var_in_list(vars, *count, name);
    if (idx < 0)
        return;

    for (uint32_t i = (uint32_t)idx + 1u; i < *count; ++i)
        vars[i - 1u] = vars[i];
    (*count)--;
}

static const char *script_user_var_value(dihos_script_runner *runner, const char *name)
{
    dihos_script_call_frame *frame = 0;
    int idx = -1;

    if (!runner || !name)
        return "";

    frame = script_current_frame(runner);
    if (frame)
    {
        idx = script_find_var_in_list(frame->vars, frame->var_count, name);
        if (idx >= 0)
            return frame->vars[idx].value;
    }

    idx = script_find_var_in_list(runner->vars, runner->var_count, name);
    if (idx >= 0)
        return runner->vars[idx].value;

    return "";
}

static int script_set_global_var(dihos_script_runner *runner, const char *name, const char *value)
{
    if (!runner)
        return -1;

    return script_set_var_in_list(runner->vars, &runner->var_count, DIHOS_SCRIPT_MAX_VARS, name, value);
}

static int script_set_user_var(dihos_script_runner *runner, const char *name, const char *value)
{
    dihos_script_call_frame *frame = script_current_frame(runner);

    if (!runner)
        return -1;

    if (frame)
    {
        return script_set_var_in_list(frame->vars, &frame->var_count,
                                      DIHOS_SCRIPT_MAX_LOCAL_VARS, name, value);
    }

    return script_set_global_var(runner, name, value);
}

static void script_unset_user_var(dihos_script_runner *runner, const char *name)
{
    dihos_script_call_frame *frame = script_current_frame(runner);

    if (!runner || !script_name_valid(name))
        return;

    if (frame)
    {
        script_unset_var_in_list(frame->vars, &frame->var_count, name);
        return;
    }

    script_unset_var_in_list(runner->vars, &runner->var_count, name);
}

static void script_set_status(dihos_script_runner *runner, int status)
{
    char buf[16];

    if (!runner)
        return;

    runner->last_status = status;
    script_i32_to_dec(status, buf, sizeof(buf));
    (void)script_set_global_var(runner, "status", buf);
}

static void script_error(dihos_script_runner *runner, const char *message)
{
    char buf[128];
    char line_no[16];
    ksb b;

    if (!runner)
        return;

    runner->finished = 1u;
    runner->waiting_input = 0u;
    runner->input_var[0] = 0;
    runner->input_prompt[0] = 0;
    runner->exit_status = -1;
    runner->exit_text[0] = 0;
    script_set_status(runner, -1);

    ksb_init(&b, buf, sizeof(buf));
    ksb_puts(&b, "script error");
    if (runner->pc < runner->line_count)
    {
        script_u32_to_dec(runner->pc + 1u, line_no, sizeof(line_no));
        ksb_puts(&b, " line ");
        ksb_puts(&b, line_no);
    }
    ksb_puts(&b, ": ");
    ksb_puts(&b, message ? message : "unknown error");
    dihos_shell_session_error(runner->session, buf);
}

static char *script_next_token(char **cursor)
{
    char *start = 0;

    if (!cursor || !*cursor)
        return 0;

    while (script_is_space(**cursor))
        ++(*cursor);
    if (!**cursor)
        return 0;

    start = *cursor;
    while (**cursor && !script_is_space(**cursor))
        ++(*cursor);
    if (**cursor)
    {
        **cursor = 0;
        ++(*cursor);
    }

    return start;
}

static int script_find_label(const dihos_script_runner *runner, const char *name, uint32_t *line_index)
{
    for (uint32_t i = 0u; i < runner->label_count; ++i)
    {
        if (strcmp(runner->labels[i].name, name) == 0)
        {
            *line_index = runner->labels[i].line_index;
            return 0;
        }
    }

    return -1;
}

static int script_register_label(dihos_script_runner *runner, uint32_t line_index, const char *name)
{
    if (!script_name_valid(name) || runner->label_count >= DIHOS_SCRIPT_MAX_LABELS)
        return -1;
    if (script_find_label(runner, name, &line_index) == 0)
        return -1;

    script_copy(runner->labels[runner->label_count].name,
                sizeof(runner->labels[runner->label_count].name), name);
    runner->labels[runner->label_count].line_index = line_index;
    ++runner->label_count;
    return 0;
}

static int script_find_function_by_name(const dihos_script_runner *runner, const char *name)
{
    uint32_t i = 0u;

    if (!runner || !name)
        return -1;

    for (i = 0u; i < runner->function_count; ++i)
    {
        if (strcmp(runner->functions[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

static int script_find_function_by_line(const dihos_script_runner *runner, uint32_t line_index)
{
    uint32_t i = 0u;

    if (!runner)
        return -1;

    for (i = 0u; i < runner->function_count; ++i)
    {
        if (runner->functions[i].entry_line > 0u &&
            runner->functions[i].entry_line - 1u == line_index)
            return (int)i;
    }

    return -1;
}

static int script_runtime_value(dihos_script_runner *runner, const char *name, char *out, uint32_t out_cap)
{
    if (!runner || !name || !out || out_cap == 0u)
        return -1;

    if (strcmp(name, "script") == 0)
        script_copy(out, out_cap, runner->friendly_path);
    else if (strcmp(name, "script_raw") == 0)
        script_copy(out, out_cap, runner->raw_path);
    else if (strcmp(name, "script_dir") == 0)
        script_copy(out, out_cap, runner->script_dir);
    else if (strcmp(name, "status") == 0)
        script_i32_to_dec(runner->last_status, out, out_cap);
    else if (strcmp(name, "status_text") == 0)
        script_copy(out, out_cap, runner->session ? runner->session->run_status_text : "");
    else if (strcmp(name, "stdin") == 0)
        script_copy(out, out_cap, runner->stdin_text);
    else if (strcmp(name, "argc") == 0)
        script_u32_to_dec(runner->arg_count, out, out_cap);
    else if (strcmp(name, "cwd") == 0)
        script_copy(out, out_cap, (runner->session ? runner->session->cwd : "/"));
    else if (strcmp(name, "ticks") == 0)
        script_u64_to_dec((uint64_t)dihos_time_ticks(), out, out_cap);
    else if (strcmp(name, "seconds") == 0)
        script_u64_to_dec((uint64_t)dihos_time_seconds(), out, out_cap);
    else if (strcmp(name, "fattime") == 0)
        script_u32_to_dec((uint32_t)dihos_time_fattime(), out, out_cap);
    else
        return -1;

    return 0;
}

static int script_expand_vars(dihos_script_runner *runner, const char *input, char *out, uint32_t cap)
{
    ksb b;
    uint32_t i = 0u;
    char runtime_buf[DIHOS_SCRIPT_VAR_VALUE_CAP];

    if (!runner || !input || !out || cap == 0u)
        return -1;

    ksb_init(&b, out, cap);
    while (input[i])
    {
        if (input[i] == '$')
        {
            ++i;
            if (input[i] == '$')
            {
                ksb_putc(&b, '$');
                ++i;
                continue;
            }

            if (input[i] == '{')
            {
                char name[DIHOS_SCRIPT_VAR_NAME_CAP];
                uint32_t n = 0u;

                ++i;
                while (input[i] && input[i] != '}' && n + 1u < sizeof(name))
                    name[n++] = input[i++];
                if (input[i] != '}')
                    return -1;
                name[n] = 0;
                ++i;
                ksb_puts(&b, script_user_var_value(runner, name));
                continue;
            }

            if (script_is_name_start(input[i]))
            {
                char name[DIHOS_SCRIPT_VAR_NAME_CAP];
                uint32_t n = 0u;

                while (script_is_name_char(input[i]) && n + 1u < sizeof(name))
                    name[n++] = input[i++];
                name[n] = 0;
                ksb_puts(&b, script_user_var_value(runner, name));
                continue;
            }

            ksb_putc(&b, '$');
            continue;
        }

        if (input[i] == '%')
        {
            ++i;
            if (input[i] == '%')
            {
                ksb_putc(&b, '%');
                ++i;
                continue;
            }

            if (script_is_digit(input[i]))
            {
                uint32_t arg_index = 0u;
                while (script_is_digit(input[i]))
                {
                    arg_index = (arg_index * 10u) + (uint32_t)(input[i] - '0');
                    ++i;
                }

                if (arg_index == 0u)
                {
                    ksb_puts(&b, runner->friendly_path);
                }
                else if (arg_index <= runner->arg_count)
                {
                    ksb_puts(&b, runner->args[arg_index - 1u]);
                }

                continue;
            }

            if (script_is_name_start(input[i]))
            {
                char name[DIHOS_SCRIPT_VAR_NAME_CAP];
                uint32_t n = 0u;

                while (input[i] && input[i] != '%' && n + 1u < sizeof(name))
                    name[n++] = input[i++];
                if (input[i] != '%')
                    return -1;
                ++i;
                name[n] = 0;

                if (script_runtime_value(runner, name, runtime_buf, sizeof(runtime_buf)) != 0)
                    return -1;
                ksb_puts(&b, runtime_buf);
                continue;
            }

            return -1;
        }

        ksb_putc(&b, input[i++]);
    }

    return b.overflow ? -1 : 0;
}

static int script_path_kind(dihos_script_runner *runner, const char *path)
{
    char friendly[DIHOS_SHELL_PATH_CAP];
    char raw[DIHOS_SHELL_PATH_CAP];
    KDir dir;
    KFile file;

    if (!runner || !runner->session || !path)
        return 0;

    if (dihos_path_resolve_raw(runner->session->cwd, path, friendly, sizeof(friendly), raw, sizeof(raw)) != 0)
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

static char *script_strip_quotes(char *text)
{
    uint32_t len = 0u;

    if (!text)
        return text;

    text = script_trim(text);
    len = (uint32_t)strlen(text);
    if (len >= 2u && text[0] == '"' && text[len - 1u] == '"')
    {
        text[len - 1u] = 0;
        return text + 1u;
    }

    return text;
}

static int script_eval_condition(dihos_script_runner *runner, char *condition, int *out_true)
{
    static const char *ops[] = {"==", "!=", "<=", ">=", "<", ">"};
    char *expr = script_trim(condition);
    char *op_pos = 0;
    const char *op_text = 0;
    char lhs_buf[DIHOS_SCRIPT_LINE_CAP];
    char rhs_buf[DIHOS_SCRIPT_LINE_CAP];
    char *lhs = 0;
    char *rhs = 0;
    uint32_t i = 0u;
    double lhs_num = 0.0;
    double rhs_num = 0.0;

    if (!expr || !out_true || !expr[0])
        return -1;

    if (script_keyword(expr, "exists") || script_keyword(expr, "isfile") || script_keyword(expr, "isdir"))
    {
        int kind = 0;
        const char *keyword = script_keyword(expr, "exists") ? "exists" :
                              (script_keyword(expr, "isfile") ? "isfile" : "isdir");

        rhs = script_strip_quotes(script_trim(expr + strlen(keyword)));
        if (!rhs || !rhs[0])
            return -1;

        kind = script_path_kind(runner, rhs);
        if (strcmp(keyword, "exists") == 0)
            *out_true = kind != 0;
        else if (strcmp(keyword, "isfile") == 0)
            *out_true = kind == 1;
        else
            *out_true = kind == 2;
        return 0;
    }

    for (i = 0u; i < (uint32_t)(sizeof(ops) / sizeof(ops[0])); ++i)
    {
        op_pos = strstr(expr, ops[i]);
        if (op_pos)
        {
            op_text = ops[i];
            break;
        }
    }

    if (!op_pos || !op_text)
        return -1;

    memset(lhs_buf, 0, sizeof(lhs_buf));
    memset(rhs_buf, 0, sizeof(rhs_buf));
    if (op_pos > expr)
    {
        uint32_t lhs_len = (uint32_t)(op_pos - expr);
        if (lhs_len >= sizeof(lhs_buf))
            lhs_len = sizeof(lhs_buf) - 1u;
        memcpy(lhs_buf, expr, lhs_len);
        lhs_buf[lhs_len] = 0;
    }
    script_copy(rhs_buf, sizeof(rhs_buf), op_pos + strlen(op_text));

    lhs = script_strip_quotes(lhs_buf);
    rhs = script_strip_quotes(rhs_buf);

    if (strcmp(op_text, "==") == 0)
    {
        *out_true = strcmp(lhs, rhs) == 0;
        return 0;
    }
    if (strcmp(op_text, "!=") == 0)
    {
        *out_true = strcmp(lhs, rhs) != 0;
        return 0;
    }

    if (script_parse_double(lhs, &lhs_num) != 0 || script_parse_double(rhs, &rhs_num) != 0)
        return -1;

    if (strcmp(op_text, "<") == 0)
        *out_true = lhs_num < rhs_num;
    else if (strcmp(op_text, "<=") == 0)
        *out_true = lhs_num <= rhs_num;
    else if (strcmp(op_text, ">") == 0)
        *out_true = lhs_num > rhs_num;
    else if (strcmp(op_text, ">=") == 0)
        *out_true = lhs_num >= rhs_num;
    else
        return -1;

    return 0;
}

static int script_line_kind(const char *source)
{
    char work[DIHOS_SCRIPT_LINE_CAP];
    char *line = 0;

    script_copy(work, sizeof(work), source);
    line = script_trim(work);

    if (!line[0] || line[0] == '#')
        return 0;
    if (script_keyword(line, "else if"))
        return SCRIPT_BLOCK_ELSE_IF;
    if (script_keyword(line, "if"))
        return SCRIPT_BLOCK_IF;
    if (strcmp(line, "else") == 0)
        return SCRIPT_BLOCK_ELSE;
    if (strcmp(line, "end") == 0)
        return SCRIPT_BLOCK_END;
    if (script_keyword(line, "while"))
        return SCRIPT_BLOCK_WHILE;
    if (script_keyword(line, "for"))
        return SCRIPT_BLOCK_FOR;
    if (script_keyword(line, "fn"))
        return SCRIPT_BLOCK_FN;
    return 0;
}

static int script_is_block_start(int kind)
{
    return kind == SCRIPT_BLOCK_IF ||
           kind == SCRIPT_BLOCK_WHILE ||
           kind == SCRIPT_BLOCK_FOR ||
           kind == SCRIPT_BLOCK_FN;
}

static int script_find_matching_end(dihos_script_runner *runner, uint32_t start, uint32_t *target)
{
    uint32_t depth = 0u;

    for (uint32_t i = start; i < runner->line_count; ++i)
    {
        int kind = script_line_kind(runner->lines[i]);

        if (script_is_block_start(kind))
            ++depth;
        else if (kind == SCRIPT_BLOCK_END)
        {
            if (depth == 0u)
            {
                *target = i;
                return 0;
            }
            --depth;
        }
    }

    return -1;
}

static int script_find_if_branch_or_end(dihos_script_runner *runner, uint32_t start,
                                        uint32_t *target, uint8_t *target_kind)
{
    uint32_t depth = 0u;

    for (uint32_t i = start; i < runner->line_count; ++i)
    {
        int kind = script_line_kind(runner->lines[i]);

        if (script_is_block_start(kind))
        {
            ++depth;
            continue;
        }

        if (kind == SCRIPT_BLOCK_END)
        {
            if (depth == 0u)
            {
                *target = i;
                *target_kind = (uint8_t)SCRIPT_BLOCK_END;
                return 0;
            }
            --depth;
            continue;
        }

        if (depth == 0u && (kind == SCRIPT_BLOCK_ELSE_IF || kind == SCRIPT_BLOCK_ELSE))
        {
            *target = i;
            *target_kind = (uint8_t)kind;
            return 0;
        }
    }

    return -1;
}

static int script_parse_fn_header(char *line, dihos_script_function *out_fn)
{
    char *cursor = line + 2u;
    char *name = 0;
    char *arg = 0;

    if (!out_fn)
        return -1;

    memset(out_fn, 0, sizeof(*out_fn));

    name = script_next_token(&cursor);
    if (!name || !script_name_valid(name))
        return -1;
    script_copy(out_fn->name, sizeof(out_fn->name), name);

    while ((arg = script_next_token(&cursor)) != 0)
    {
        if (!script_name_valid(arg) || out_fn->arg_count >= DIHOS_SCRIPT_FUNC_MAX_ARGS)
            return -1;
        script_copy(out_fn->arg_names[out_fn->arg_count],
                    sizeof(out_fn->arg_names[out_fn->arg_count]), arg);
        ++out_fn->arg_count;
    }

    return 0;
}

static int script_validate_and_index(dihos_script_runner *runner)
{
    script_block_entry stack[DIHOS_SCRIPT_MAX_LINES];
    uint32_t depth = 0u;

    for (uint32_t i = 0u; i < runner->line_count; ++i)
    {
        char work[DIHOS_SCRIPT_LINE_CAP];
        char *line = 0;
        int kind = 0;

        script_copy(work, sizeof(work), runner->lines[i]);
        line = script_trim(work);
        kind = script_line_kind(line);

        if (!line[0] || line[0] == '#')
            continue;

        if (kind == SCRIPT_BLOCK_IF)
        {
            if (depth >= DIHOS_SCRIPT_MAX_LINES)
                return -1;
            stack[depth].kind = (uint8_t)kind;
            stack[depth].line_index = i;
            stack[depth].seen_else = 0u;
            stack[depth].function_index = -1;
            ++depth;
            continue;
        }

        if (kind == SCRIPT_BLOCK_ELSE_IF)
        {
            if (depth == 0u || stack[depth - 1u].kind != SCRIPT_BLOCK_IF ||
                stack[depth - 1u].seen_else)
                return -1;
            continue;
        }

        if (kind == SCRIPT_BLOCK_ELSE)
        {
            if (depth == 0u || stack[depth - 1u].kind != SCRIPT_BLOCK_IF ||
                stack[depth - 1u].seen_else)
                return -1;
            stack[depth - 1u].seen_else = 1u;
            continue;
        }

        if (kind == SCRIPT_BLOCK_WHILE || kind == SCRIPT_BLOCK_FOR)
        {
            if (depth >= DIHOS_SCRIPT_MAX_LINES)
                return -1;
            stack[depth].kind = (uint8_t)kind;
            stack[depth].line_index = i;
            stack[depth].seen_else = 0u;
            stack[depth].function_index = -1;
            ++depth;
            continue;
        }

        if (kind == SCRIPT_BLOCK_FN)
        {
            dihos_script_function fn;
            int existing = -1;

            if (depth != 0u)
                return -1;
            if (runner->function_count >= DIHOS_SCRIPT_MAX_FUNCS)
                return -1;
            if (script_parse_fn_header(line, &fn) != 0)
                return -1;

            existing = script_find_function_by_name(runner, fn.name);
            if (existing >= 0)
                return -1;

            runner->functions[runner->function_count] = fn;
            runner->functions[runner->function_count].entry_line = i + 1u;
            runner->functions[runner->function_count].end_line = 0u;

            stack[depth].kind = SCRIPT_BLOCK_FN;
            stack[depth].line_index = i;
            stack[depth].seen_else = 0u;
            stack[depth].function_index = (int)runner->function_count;
            ++depth;
            ++runner->function_count;
            continue;
        }

        if (kind == SCRIPT_BLOCK_END)
        {
            if (depth == 0u)
                return -1;

            --depth;
            if (stack[depth].kind == SCRIPT_BLOCK_FN)
            {
                int fn_index = stack[depth].function_index;
                if (fn_index < 0 || (uint32_t)fn_index >= runner->function_count)
                    return -1;
                runner->functions[fn_index].end_line = i;
            }
            continue;
        }

        if (script_keyword(line, "return"))
        {
            if (depth == 0u || stack[0].kind != SCRIPT_BLOCK_FN)
                return -1;
            continue;
        }

        if (strcmp(line, "break") == 0 || strcmp(line, "continue") == 0)
        {
            uint32_t j = depth;
            uint8_t in_loop = 0u;

            while (j > 0u)
            {
                --j;
                if (stack[j].kind == SCRIPT_BLOCK_WHILE || stack[j].kind == SCRIPT_BLOCK_FOR)
                {
                    in_loop = 1u;
                    break;
                }
            }
            if (!in_loop)
                return -1;
        }
    }

    return depth == 0u ? 0 : -1;
}

static void script_set_builtin_vars(dihos_script_runner *runner)
{
    char dir[DIHOS_SHELL_PATH_CAP];
    uint32_t slash = 0u;
    char argc_buf[16];

    (void)script_set_global_var(runner, "script", runner->friendly_path);
    (void)script_set_global_var(runner, "script_raw", runner->raw_path);

    script_copy(dir, sizeof(dir), runner->friendly_path);
    for (uint32_t i = 0u; dir[i]; ++i)
        if (dir[i] == '/')
            slash = i;

    if (slash == 0u)
        script_copy(dir, sizeof(dir), "/");
    else
        dir[slash] = 0;

    script_copy(runner->script_dir, sizeof(runner->script_dir), dir);
    (void)script_set_global_var(runner, "script_dir", runner->script_dir);
    (void)script_set_global_var(runner, "stdin", runner->stdin_text);
    script_u32_to_dec(runner->arg_count, argc_buf, sizeof(argc_buf));
    (void)script_set_global_var(runner, "argc", argc_buf);

    runner->input_var[0] = 0;
    runner->input_prompt[0] = 0;
    runner->status_text[0] = 0;
    runner->exit_text[0] = 0;
    script_set_status(runner, 0);
}

static int script_finalize_line(dihos_script_runner *runner, char *line_buf, uint32_t *line_len)
{
    if (runner->line_count >= DIHOS_SCRIPT_MAX_LINES)
        return -1;

    line_buf[*line_len] = 0;
    script_copy(runner->lines[runner->line_count], sizeof(runner->lines[runner->line_count]), line_buf);
    ++runner->line_count;
    *line_len = 0u;
    line_buf[0] = 0;
    return 0;
}

int dihos_script_load_file(dihos_script_runner *runner, dihos_shell_session *session,
                           const char *raw_path, const char *friendly_path)
{
    return dihos_script_load_file_with_args(runner, session, raw_path, friendly_path, 0, 0, 0u);
}

int dihos_script_load_file_with_stdin(dihos_script_runner *runner, dihos_shell_session *session,
                                      const char *raw_path, const char *friendly_path,
                                      const char *stdin_text)
{
    return dihos_script_load_file_with_args(runner, session, raw_path, friendly_path, stdin_text, 0, 0u);
}

int dihos_script_load_file_with_args(dihos_script_runner *runner, dihos_shell_session *session,
                                     const char *raw_path, const char *friendly_path,
                                     const char *stdin_text,
                                     const char *const *args, uint32_t arg_count)
{
    KFile file;
    char read_buf[512];
    char line_buf[DIHOS_SCRIPT_LINE_CAP];
    uint64_t file_size = 0u;
    uint64_t file_pos = 0u;
    uint32_t line_len = 0u;
    uint32_t got = 0u;

    if (!runner || !session || !raw_path || !raw_path[0])
        return -1;

    memset(runner, 0, sizeof(*runner));
    runner->session = session;
    script_copy(runner->raw_path, sizeof(runner->raw_path), raw_path);
    script_copy(runner->friendly_path, sizeof(runner->friendly_path), friendly_path && friendly_path[0] ? friendly_path : raw_path);
    script_copy(runner->stdin_text, sizeof(runner->stdin_text), stdin_text ? stdin_text : "");

    if (arg_count > DIHOS_SCRIPT_MAX_ARGS)
        arg_count = DIHOS_SCRIPT_MAX_ARGS;
    runner->arg_count = arg_count;
    for (uint32_t i = 0u; i < arg_count; ++i)
        script_copy(runner->args[i], sizeof(runner->args[i]), (args && args[i]) ? args[i] : "");

    if (kfile_open(&file, raw_path, KFILE_READ) != 0)
    {
        dihos_shell_session_error(session, "unable to open script");
        return -1;
    }

    file_size = kfile_size(&file);
    line_buf[0] = 0;
    while (file_pos < file_size)
    {
        uint32_t want = (uint32_t)sizeof(read_buf);
        uint64_t remaining = file_size - file_pos;

        if (remaining < (uint64_t)want)
            want = (uint32_t)remaining;

        if (want == 0u)
            break;

        if (kfile_read(&file, read_buf, want, &got) != 0 || got == 0u)
        {
            kfile_close(&file);
            dihos_shell_session_error(session, "script read failed");
            return -1;
        }

        file_pos += got;

        for (uint32_t i = 0u; i < got; ++i)
        {
            char ch = read_buf[i];

            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                if (script_finalize_line(runner, line_buf, &line_len) != 0)
                {
                    kfile_close(&file);
                    dihos_shell_session_error(session, "script has too many lines");
                    return -1;
                }
                continue;
            }
            if (line_len + 1u >= sizeof(line_buf))
            {
                kfile_close(&file);
                dihos_shell_session_error(session, "script line is too long");
                return -1;
            }
            line_buf[line_len++] = ch;
            line_buf[line_len] = 0;
        }
    }

    kfile_close(&file);

    if (line_len > 0u && script_finalize_line(runner, line_buf, &line_len) != 0)
    {
        dihos_shell_session_error(session, "script has too many lines");
        return -1;
    }

    for (uint32_t i = 0u; i < runner->line_count; ++i)
    {
        char work[DIHOS_SCRIPT_LINE_CAP];
        char *line = 0;

        script_copy(work, sizeof(work), runner->lines[i]);
        line = script_trim(work);
        if (line[0] == ':')
        {
            char *name = script_trim(line + 1u);
            if (script_register_label(runner, i, name) != 0)
            {
                dihos_shell_session_error(session, "invalid or duplicate script label");
                return -1;
            }
        }
    }

    if (script_validate_and_index(runner) != 0)
    {
        dihos_shell_session_error(session, "script has invalid control flow");
        return -1;
    }

    script_set_builtin_vars(runner);
    runner->loaded = 1u;
    return 0;
}

static int script_parse_for_header(char *line, char *name_out, uint32_t name_cap,
                                   double *from_out, double *to_out, double *step_out)
{
    char *cursor = line + 3u;
    char *name = 0;
    char *from_kw = 0;
    char *from_text = 0;
    char *to_kw = 0;
    char *to_text = 0;
    char *step_kw = 0;
    char *step_text = 0;

    if (!line || !name_out || !from_out || !to_out || !step_out)
        return -1;

    name = script_next_token(&cursor);
    from_kw = script_next_token(&cursor);
    from_text = script_next_token(&cursor);
    to_kw = script_next_token(&cursor);
    to_text = script_next_token(&cursor);
    step_kw = script_next_token(&cursor);
    step_text = script_next_token(&cursor);

    if (!name || !from_kw || !from_text || !to_kw || !to_text)
        return -1;
    if (!script_name_valid(name))
        return -1;
    if (strcmp(from_kw, "from") != 0 || strcmp(to_kw, "to") != 0)
        return -1;
    if (script_parse_double(from_text, from_out) != 0 || script_parse_double(to_text, to_out) != 0)
        return -1;

    *step_out = 1.0;
    if (step_kw)
    {
        if (strcmp(step_kw, "step") != 0 || !step_text || script_next_token(&cursor))
            return -1;
        if (script_parse_double(step_text, step_out) != 0)
            return -1;
    }

    script_copy(name_out, name_cap, name);
    return 0;
}

static int script_math_pow_int(double base, int exponent, double *out)
{
    double result = 1.0;
    uint32_t power = 0u;
    double factor = base;
    int neg = 0;

    if (!out)
        return -1;

    if (exponent < 0)
    {
        neg = 1;
        exponent = -exponent;
    }

    power = (uint32_t)exponent;
    while (power > 0u)
    {
        if (power & 1u)
            result *= factor;
        factor *= factor;
        power >>= 1u;
    }

    if (neg)
    {
        if (result == 0.0)
            return -1;
        result = 1.0 / result;
    }

    *out = result;
    return 0;
}

static int script_handle_math_op(dihos_script_runner *runner, char *line, const char *keyword, int op)
{
    char *cursor = line + strlen(keyword);
    char *name = 0;
    char *rhs_text = 0;
    const char *lhs_text = 0;
    double lhs = 0.0;
    double rhs = 0.0;
    double result = 0.0;
    char out_text[DIHOS_SCRIPT_VAR_VALUE_CAP];
    int exp_value = 0;

    name = script_next_token(&cursor);
    rhs_text = script_next_token(&cursor);

    if (!name || !rhs_text || script_next_token(&cursor) || !script_name_valid(name))
        return -1;

    lhs_text = script_user_var_value(runner, name);
    if (script_parse_double(lhs_text && lhs_text[0] ? lhs_text : "0", &lhs) != 0 ||
        script_parse_double(rhs_text, &rhs) != 0)
        return -1;

    if (op == 1)
        result = lhs + rhs;
    else if (op == 2)
        result = lhs - rhs;
    else if (op == 3)
        result = lhs * rhs;
    else if (op == 4)
    {
        if (rhs == 0.0)
            return -1;
        result = lhs / rhs;
    }
    else if (op == 5)
    {
        double rounded = rhs >= 0.0 ? rhs + 0.5 : rhs - 0.5;
        exp_value = (int)rounded;
        if ((double)exp_value != rhs)
            return -1;
        if (script_math_pow_int(lhs, exp_value, &result) != 0)
            return -1;
    }
    else
    {
        return -1;
    }

    script_double_to_text(result, out_text, sizeof(out_text));
    if (script_set_user_var(runner, name, out_text) != 0)
        return -1;

    script_set_status(runner, 0);
    ++runner->pc;
    return 0;
}

static int script_extract_run_out_target(char *line, char *out_name, uint32_t out_cap)
{
    char *cursor = line;
    char *token = 0;
    uint8_t saw_out = 0u;
    uint8_t saw_out_eq = 0u;

    if (!line || !out_name || out_cap == 0u)
        return 0;

    out_name[0] = 0;
    token = script_next_token(&cursor);
    if (!token || strcmp(token, "sys:run") != 0)
        return 0;

    while ((token = script_next_token(&cursor)) != 0)
    {
        char *eq = strchr(token, '=');

        if (strcmp(token, "|") == 0 || strcmp(token, "&&") == 0 ||
            strcmp(token, "||") == 0 || strcmp(token, ";") == 0)
            break;

        if (saw_out_eq)
        {
            if (script_name_valid(token))
            {
                script_copy(out_name, out_cap, token);
                return 1;
            }
            saw_out_eq = 0u;
            saw_out = 0u;
        }

        if (saw_out)
        {
            if (strcmp(token, "=") == 0)
            {
                saw_out_eq = 1u;
                continue;
            }

            if (token[0] == '=' && script_name_valid(token + 1u))
            {
                script_copy(out_name, out_cap, token + 1u);
                return 1;
            }

            saw_out = 0u;
        }

        if (strcmp(token, "out") == 0)
        {
            saw_out = 1u;
            continue;
        }

        if (eq && eq != token)
        {
            *eq = 0;
            if (strcmp(token, "out") == 0)
            {
                if (eq[1] == 0)
                {
                    saw_out_eq = 1u;
                    saw_out = 1u;
                    continue;
                }

                if (script_name_valid(eq + 1u))
                {
                    script_copy(out_name, out_cap, eq + 1u);
                    return 1;
                }
            }
            continue;
        }
    }

    return 0;
}

static int script_return_from_function(dihos_script_runner *runner, const char *value)
{
    dihos_script_call_frame frame;
    const char *ret_value = value ? value : "";

    if (!runner || runner->call_depth == 0u)
        return -1;

    frame = runner->call_frames[runner->call_depth - 1u];
    runner->call_depth--;
    runner->loop_depth = frame.loop_depth_base;

    if (frame.return_target[0])
    {
        if (script_set_user_var(runner, frame.return_target, ret_value) != 0)
            return -1;
    }

    script_set_status(runner, 0);
    runner->pc = frame.return_pc;
    return 0;
}

static int script_push_loop(dihos_script_runner *runner, uint8_t type,
                            uint32_t start_line, uint32_t end_line)
{
    if (!runner || runner->loop_depth >= DIHOS_SCRIPT_MAX_LOOPS)
        return -1;

    memset(&runner->loop_frames[runner->loop_depth], 0, sizeof(runner->loop_frames[runner->loop_depth]));
    runner->loop_frames[runner->loop_depth].type = type;
    runner->loop_frames[runner->loop_depth].start_line = start_line;
    runner->loop_frames[runner->loop_depth].end_line = end_line;
    ++runner->loop_depth;
    return 0;
}

static dihos_script_loop_frame *script_top_loop(dihos_script_runner *runner)
{
    if (!runner || runner->loop_depth == 0u)
        return 0;
    return &runner->loop_frames[runner->loop_depth - 1u];
}

static void script_pop_loop(dihos_script_runner *runner)
{
    if (runner && runner->loop_depth > 0u)
        runner->loop_depth--;
}

static int script_step_one(dihos_script_runner *runner)
{
    char expanded[DIHOS_SCRIPT_LINE_CAP];
    char work[DIHOS_SCRIPT_LINE_CAP];
    char cmd_scan[DIHOS_SCRIPT_LINE_CAP];
    char *cmd_cursor = 0;
    char *cmd_name = 0;
    char out_scan[DIHOS_SCRIPT_LINE_CAP];
    char out_name[DIHOS_SCRIPT_VAR_NAME_CAP];
    char *line = 0;
    int kind = 0;

    if (runner->pc >= runner->line_count)
    {
        runner->finished = 1u;
        runner->waiting_input = 0u;
        runner->input_var[0] = 0;
        runner->input_prompt[0] = 0;
        runner->exit_status = runner->last_status;
        runner->exit_text[0] = 0;
        return 1;
    }

    if (script_expand_vars(runner, runner->lines[runner->pc], expanded, sizeof(expanded)) != 0)
    {
        script_error(runner, "variable expansion failed");
        return -1;
    }

    script_copy(work, sizeof(work), expanded);
    line = script_trim(work);

    if (!line[0] || line[0] == '#')
    {
        ++runner->pc;
        return 0;
    }

    ++runner->instruction_count;
    if (runner->instruction_count > DIHOS_SCRIPT_INSTRUCTION_LIMIT)
    {
        script_error(runner, "instruction limit reached");
        return -1;
    }

    if (line[0] == ':')
    {
        ++runner->pc;
        return 0;
    }

    kind = script_line_kind(line);

    if (kind == SCRIPT_BLOCK_FN)
    {
        int fn_index = script_find_function_by_line(runner, runner->pc);
        if (fn_index < 0)
        {
            script_error(runner, "unknown function declaration");
            return -1;
        }
        runner->pc = runner->functions[fn_index].end_line + 1u;
        return 0;
    }

    if (kind == SCRIPT_BLOCK_IF)
    {
        uint32_t scan_from = runner->pc + 1u;
        int truthy = 0;

        if (script_eval_condition(runner, script_trim(line + 2u), &truthy) != 0)
        {
            script_error(runner, "invalid if condition");
            return -1;
        }

        if (truthy)
        {
            ++runner->pc;
            return 0;
        }

        for (;;)
        {
            uint32_t target = 0u;
            uint8_t target_kind = 0u;
            char branch_expanded[DIHOS_SCRIPT_LINE_CAP];
            char branch_work[DIHOS_SCRIPT_LINE_CAP];
            char *branch_line = 0;
            int branch_truthy = 0;

            if (script_find_if_branch_or_end(runner, scan_from, &target, &target_kind) != 0)
            {
                script_error(runner, "if is missing end");
                return -1;
            }

            if (target_kind == SCRIPT_BLOCK_END)
            {
                runner->pc = target + 1u;
                return 0;
            }

            if (target_kind == SCRIPT_BLOCK_ELSE)
            {
                runner->pc = target + 1u;
                return 0;
            }

            if (script_expand_vars(runner, runner->lines[target], branch_expanded, sizeof(branch_expanded)) != 0)
            {
                script_error(runner, "invalid else if condition");
                return -1;
            }

            script_copy(branch_work, sizeof(branch_work), branch_expanded);
            branch_line = script_trim(branch_work);
            if (script_eval_condition(runner, script_trim(branch_line + 7u), &branch_truthy) != 0)
            {
                script_error(runner, "invalid else if condition");
                return -1;
            }

            if (branch_truthy)
            {
                runner->pc = target + 1u;
                return 0;
            }

            scan_from = target + 1u;
        }
    }

    if (kind == SCRIPT_BLOCK_ELSE_IF || kind == SCRIPT_BLOCK_ELSE)
    {
        uint32_t target = 0u;

        if (script_find_matching_end(runner, runner->pc + 1u, &target) != 0)
        {
            script_error(runner, "else block is missing end");
            return -1;
        }

        runner->pc = target + 1u;
        return 0;
    }

    if (kind == SCRIPT_BLOCK_WHILE)
    {
        dihos_script_loop_frame *top = script_top_loop(runner);
        uint8_t has_top = top && top->type == SCRIPT_LOOP_WHILE &&
                          top->start_line == runner->pc;
        int truthy = 0;
        uint32_t end_line = 0u;

        if (script_eval_condition(runner, script_trim(line + 5u), &truthy) != 0)
        {
            script_error(runner, "invalid while condition");
            return -1;
        }

        if (truthy)
        {
            if (!has_top)
            {
                if (script_find_matching_end(runner, runner->pc + 1u, &end_line) != 0 ||
                    script_push_loop(runner, SCRIPT_LOOP_WHILE, runner->pc, end_line) != 0)
                {
                    script_error(runner, "while is missing end");
                    return -1;
                }
            }
            ++runner->pc;
            return 0;
        }

        if (has_top)
        {
            end_line = top->end_line;
            script_pop_loop(runner);
        }
        else if (script_find_matching_end(runner, runner->pc + 1u, &end_line) != 0)
        {
            script_error(runner, "while is missing end");
            return -1;
        }

        runner->pc = end_line + 1u;
        return 0;
    }

    if (kind == SCRIPT_BLOCK_FOR)
    {
        dihos_script_loop_frame *top = script_top_loop(runner);
        uint8_t has_top = top && top->type == SCRIPT_LOOP_FOR &&
                          top->start_line == runner->pc;

        if (has_top && top->for_initialized)
        {
            double next = top->for_current + top->for_step;
            int keep_running = (top->for_step > 0.0) ? (next <= top->for_end) : (next >= top->for_end);
            char value_text[DIHOS_SCRIPT_VAR_VALUE_CAP];

            if (!keep_running)
            {
                uint32_t after = top->end_line + 1u;
                script_pop_loop(runner);
                runner->pc = after;
                return 0;
            }

            top->for_current = next;
            script_double_to_text(next, value_text, sizeof(value_text));
            if (script_set_user_var(runner, top->for_var, value_text) != 0)
            {
                script_error(runner, "for loop variable update failed");
                return -1;
            }
            ++runner->pc;
            return 0;
        }
        else
        {
            char var_name[DIHOS_SCRIPT_VAR_NAME_CAP];
            double from_value = 0.0;
            double to_value = 0.0;
            double step_value = 1.0;
            uint32_t end_line = 0u;
            int keep_running = 0;
            char value_text[DIHOS_SCRIPT_VAR_VALUE_CAP];

            if (script_parse_for_header(line, var_name, sizeof(var_name),
                                        &from_value, &to_value, &step_value) != 0)
            {
                script_error(runner, "invalid for syntax");
                return -1;
            }
            if (step_value == 0.0)
            {
                script_error(runner, "for step cannot be zero");
                return -1;
            }

            keep_running = (step_value > 0.0) ? (from_value <= to_value) : (from_value >= to_value);
            if (script_find_matching_end(runner, runner->pc + 1u, &end_line) != 0)
            {
                script_error(runner, "for is missing end");
                return -1;
            }

            if (!keep_running)
            {
                runner->pc = end_line + 1u;
                return 0;
            }

            if (script_push_loop(runner, SCRIPT_LOOP_FOR, runner->pc, end_line) != 0)
            {
                script_error(runner, "too many nested loops");
                return -1;
            }

            top = script_top_loop(runner);
            script_copy(top->for_var, sizeof(top->for_var), var_name);
            top->for_current = from_value;
            top->for_end = to_value;
            top->for_step = step_value;
            top->for_initialized = 1u;

            script_double_to_text(from_value, value_text, sizeof(value_text));
            if (script_set_user_var(runner, var_name, value_text) != 0)
            {
                script_error(runner, "for loop variable set failed");
                return -1;
            }

            ++runner->pc;
            return 0;
        }
    }

    if (kind == SCRIPT_BLOCK_END)
    {
        dihos_script_loop_frame *top_loop = script_top_loop(runner);
        dihos_script_call_frame *frame = script_current_frame(runner);

        if (top_loop && top_loop->end_line == runner->pc)
        {
            runner->pc = top_loop->start_line;
            return 0;
        }

        if (frame)
        {
            int fn_index = (int)frame->function_index;
            if (fn_index >= 0 &&
                (uint32_t)fn_index < runner->function_count &&
                runner->functions[fn_index].end_line == runner->pc)
            {
                if (script_return_from_function(runner, "") != 0)
                {
                    script_error(runner, "function return failed");
                    return -1;
                }
                return 0;
            }
        }

        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "let"))
    {
        char *assignment = script_trim(line + 3u);
        char *eq = strchr(assignment, '=');

        if (!eq)
        {
            script_error(runner, "let needs name=value");
            return -1;
        }
        *eq = 0;
        if (script_set_user_var(runner, script_trim(assignment), script_trim(eq + 1u)) != 0)
        {
            script_error(runner, "invalid variable name or too many variables");
            return -1;
        }
        script_set_status(runner, 0);
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "add"))
    {
        if (script_handle_math_op(runner, line, "add", 1) != 0)
        {
            script_error(runner, "add needs name and numeric delta");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "sub"))
    {
        if (script_handle_math_op(runner, line, "sub", 2) != 0)
        {
            script_error(runner, "sub needs name and numeric delta");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "mul"))
    {
        if (script_handle_math_op(runner, line, "mul", 3) != 0)
        {
            script_error(runner, "mul needs name and numeric factor");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "div"))
    {
        if (script_handle_math_op(runner, line, "div", 4) != 0)
        {
            script_error(runner, "div needs name and non-zero numeric divisor");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "pow"))
    {
        if (script_handle_math_op(runner, line, "pow", 5) != 0)
        {
            script_error(runner, "pow needs name and integer exponent");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "unset"))
    {
        char *name = script_trim(line + 5u);

        if (!script_name_valid(name))
        {
            script_error(runner, "unset needs a variable name");
            return -1;
        }
        script_unset_user_var(runner, name);
        script_set_status(runner, 0);
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "seed"))
    {
        char *seed_text = script_trim(line + 4u);
        int seed_value = 0;

        if (seed_text && seed_text[0])
        {
            if (script_parse_i32(seed_text, &seed_value) != 0)
            {
                script_error(runner, "seed needs a numeric value");
                return -1;
            }
            script_rng_seed(runner, (uint32_t)seed_value);
        }
        else
        {
            script_rng_seed(runner, (uint32_t)dihos_time_ticks());
        }

        script_set_status(runner, 0);
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "rand"))
    {
        char *cursor = line + 4u;
        char *name = script_next_token(&cursor);
        char *a_text = script_next_token(&cursor);
        char *b_text = script_next_token(&cursor);
        char value_text[16];
        int value = 0;

        if (!name || !script_name_valid(name))
        {
            script_error(runner, "rand needs a variable name");
            return -1;
        }

        if (!a_text)
        {
            value = (int)(script_rng_next(runner) & 0x7FFFFFFFu);
        }
        else if (!b_text)
        {
            int max_value = 0;
            uint32_t span = 0u;

            if (script_parse_i32(a_text, &max_value) != 0 || max_value < 0)
            {
                script_error(runner, "rand max must be a non-negative integer");
                return -1;
            }

            span = (uint32_t)max_value + 1u;
            value = (int)script_rng_bounded(runner, span);
        }
        else
        {
            int min_value = 0;
            int max_value = 0;
            int64_t span64 = 0;
            uint32_t offset = 0u;

            if (script_parse_i32(a_text, &min_value) != 0 || script_parse_i32(b_text, &max_value) != 0)
            {
                script_error(runner, "rand range needs numeric min and max");
                return -1;
            }
            if (min_value > max_value)
            {
                script_error(runner, "rand range requires min <= max");
                return -1;
            }

            span64 = (int64_t)max_value - (int64_t)min_value + 1;
            if (span64 <= 0 || span64 > 0x100000000LL)
            {
                script_error(runner, "rand range is too large");
                return -1;
            }

            offset = script_rng_bounded(runner, (uint32_t)span64);
            value = min_value + (int)offset;
        }

        script_i32_to_dec(value, value_text, sizeof(value_text));
        if (script_set_user_var(runner, name, value_text) != 0)
        {
            script_error(runner, "invalid variable name or too many variables");
            return -1;
        }

        script_set_status(runner, 0);
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "pick"))
    {
        char *cursor = line + 4u;
        char *name = script_next_token(&cursor);
        char *option = 0;
        const char *choices[16];
        uint32_t choice_count = 0u;
        uint32_t picked = 0u;

        if (!name || !script_name_valid(name))
        {
            script_error(runner, "pick needs a variable name");
            return -1;
        }

        while ((option = script_next_token(&cursor)) != 0)
        {
            if (choice_count >= (uint32_t)(sizeof(choices) / sizeof(choices[0])))
            {
                script_error(runner, "pick supports up to 16 options");
                return -1;
            }
            choices[choice_count++] = option;
        }

        if (choice_count == 0u)
        {
            script_error(runner, "pick needs at least one option");
            return -1;
        }

        picked = script_rng_bounded(runner, choice_count);
        if (script_set_user_var(runner, name, choices[picked]) != 0)
        {
            script_error(runner, "invalid variable name or too many variables");
            return -1;
        }

        script_set_status(runner, 0);
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "goto"))
    {
        char *label = script_trim(line + 4u);
        uint32_t target = 0u;

        if (!script_name_valid(label) || script_find_label(runner, label, &target) != 0)
        {
            script_error(runner, "unknown goto label");
            return -1;
        }

        runner->loop_depth = 0u;
        runner->pc = target + 1u;
        script_set_status(runner, 0);
        return 0;
    }

    if (strcmp(line, "break") == 0)
    {
        dihos_script_loop_frame *top = script_top_loop(runner);
        uint32_t after = 0u;

        if (!top)
        {
            script_error(runner, "break is only valid inside loops");
            return -1;
        }

        after = top->end_line + 1u;
        script_pop_loop(runner);
        runner->pc = after;
        script_set_status(runner, 0);
        return 0;
    }

    if (strcmp(line, "continue") == 0)
    {
        dihos_script_loop_frame *top = script_top_loop(runner);

        if (!top)
        {
            script_error(runner, "continue is only valid inside loops");
            return -1;
        }

        runner->pc = top->end_line;
        script_set_status(runner, 0);
        return 0;
    }

    if (script_keyword(line, "input"))
    {
        char *cursor = line + 5u;
        char *name = script_next_token(&cursor);
        char *prompt = script_trim(cursor);

        if (!name || !script_name_valid(name))
        {
            script_error(runner, "input needs a variable name");
            return -1;
        }

        script_copy(runner->input_var, sizeof(runner->input_var), name);
        script_copy(runner->input_prompt, sizeof(runner->input_prompt),
                    (prompt && prompt[0]) ? prompt : "input> ");
        runner->waiting_input = 1u;
        ++runner->pc;
        return 0;
    }

    if (script_keyword(line, "call"))
    {
        char *cursor = line + 4u;
        char *fn_name = script_next_token(&cursor);
        char *token = 0;
        char *call_args[DIHOS_SCRIPT_FUNC_MAX_ARGS];
        uint32_t call_arg_count = 0u;
        char return_target[DIHOS_SCRIPT_VAR_NAME_CAP];
        int fn_index = -1;
        dihos_script_call_frame *frame = 0;
        uint32_t i = 0u;

        return_target[0] = 0;
        if (!fn_name || !script_name_valid(fn_name))
        {
            script_error(runner, "call needs a function name");
            return -1;
        }

        while ((token = script_next_token(&cursor)) != 0)
        {
            if (strcmp(token, "->") == 0)
            {
                char *target = script_next_token(&cursor);
                if (!target || script_next_token(&cursor) != 0 || !script_name_valid(target))
                {
                    script_error(runner, "call return target is invalid");
                    return -1;
                }
                script_copy(return_target, sizeof(return_target), target);
                break;
            }

            if (strncmp(token, "->", 2u) == 0 && token[2] != 0)
            {
                if (!script_name_valid(token + 2u))
                {
                    script_error(runner, "call return target is invalid");
                    return -1;
                }
                script_copy(return_target, sizeof(return_target), token + 2u);
                if (script_next_token(&cursor) != 0)
                {
                    script_error(runner, "call has extra tokens");
                    return -1;
                }
                break;
            }

            if (call_arg_count < DIHOS_SCRIPT_FUNC_MAX_ARGS)
                call_args[call_arg_count++] = token;
        }

        fn_index = script_find_function_by_name(runner, fn_name);
        if (fn_index < 0)
        {
            script_error(runner, "unknown function");
            return -1;
        }
        if (runner->call_depth >= DIHOS_SCRIPT_MAX_CALL_DEPTH)
        {
            script_error(runner, "function call depth limit reached");
            return -1;
        }

        frame = &runner->call_frames[runner->call_depth];
        memset(frame, 0, sizeof(*frame));
        frame->return_pc = runner->pc + 1u;
        frame->function_index = (uint32_t)fn_index;
        frame->loop_depth_base = runner->loop_depth;
        script_copy(frame->return_target, sizeof(frame->return_target), return_target);

        for (i = 0u; i < runner->functions[fn_index].arg_count; ++i)
        {
            const char *value = i < call_arg_count ? call_args[i] : "";
            if (script_set_var_in_list(frame->vars, &frame->var_count, DIHOS_SCRIPT_MAX_LOCAL_VARS,
                                       runner->functions[fn_index].arg_names[i], value) != 0)
            {
                script_error(runner, "unable to set function argument");
                return -1;
            }
        }

        ++runner->call_depth;
        runner->pc = runner->functions[fn_index].entry_line;
        script_set_status(runner, 0);
        return 0;
    }

    if (script_keyword(line, "return"))
    {
        char *value = script_trim(line + 6u);

        if (runner->call_depth == 0u)
        {
            script_error(runner, "return is only valid inside functions");
            return -1;
        }

        if (script_return_from_function(runner, value) != 0)
        {
            script_error(runner, "function return failed");
            return -1;
        }
        return 0;
    }

    if (script_keyword(line, "exit"))
    {
        char *status_text = script_trim(line + 4u);
        char *cursor = status_text;
        char *first = script_next_token(&cursor);
        int status = runner->last_status;
        char *message = 0;

        if (first && script_parse_i32(first, &status) == 0)
            message = script_trim(cursor);
        else
            message = status_text;

        runner->exit_status = status;
        script_copy(runner->exit_text, sizeof(runner->exit_text), message && message[0] ? message : "");
        runner->finished = 1u;
        runner->waiting_input = 0u;
        runner->input_var[0] = 0;
        runner->input_prompt[0] = 0;
        script_set_status(runner, status);
        return 1;
    }

    {
        int rc = dihos_shell_session_execute_line(runner->session, line);
        script_copy(cmd_scan, sizeof(cmd_scan), line);
        cmd_cursor = cmd_scan;
        cmd_name = script_next_token(&cmd_cursor);

        // Keep previous status across pure logging lines so `%status%` can still
        // refer to the prior command (notably after `sys:run` followed by `sys:echo`).
        if (!cmd_name || strcmp(cmd_name, "sys:echo") != 0)
            script_set_status(runner, rc);

        script_copy(out_scan, sizeof(out_scan), line);
        if (script_extract_run_out_target(out_scan, out_name, sizeof(out_name)))
        {
            (void)script_set_user_var(runner, out_name,
                                      (runner->session ? runner->session->run_status_text : ""));
        }

        ++runner->pc;
        return 0;
    }
}

int dihos_script_step(dihos_script_runner *runner, uint32_t budget)
{
    uint32_t steps = 0u;

    if (!runner || !runner->loaded)
        return -1;
    if (runner->finished)
        return 1;
    if (runner->waiting_input)
        return 0;

    if (budget == 0u)
        budget = DIHOS_SCRIPT_STEP_BUDGET;

    while (!runner->finished && steps < budget)
    {
        int rc = script_step_one(runner);
        if (rc < 0)
            return -1;
        ++steps;
        if (runner->waiting_input)
            break;
    }

    if (runner->pc >= runner->line_count && !runner->finished)
    {
        runner->finished = 1u;
        runner->waiting_input = 0u;
        runner->input_var[0] = 0;
        runner->input_prompt[0] = 0;
        runner->exit_status = runner->last_status;
        runner->exit_text[0] = 0;
    }

    return runner->finished ? 1 : 0;
}

int dihos_script_finished(const dihos_script_runner *runner)
{
    return runner && runner->finished;
}

int dihos_script_exit_status(const dihos_script_runner *runner)
{
    return runner ? runner->exit_status : -1;
}

const char *dihos_script_exit_text(const dihos_script_runner *runner)
{
    return runner ? runner->exit_text : "";
}

int dihos_script_waiting_input(const dihos_script_runner *runner)
{
    return runner && runner->waiting_input;
}

const char *dihos_script_input_prompt(const dihos_script_runner *runner)
{
    if (!runner || !runner->waiting_input)
        return "";
    return runner->input_prompt[0] ? runner->input_prompt : "input> ";
}

int dihos_script_submit_input(dihos_script_runner *runner, const char *text)
{
    const char *submitted = text ? text : "";

    if (!runner || !runner->loaded || !runner->waiting_input || !script_name_valid(runner->input_var))
        return -1;

    if (script_set_user_var(runner, runner->input_var, submitted) != 0)
        return -1;

    script_copy(runner->stdin_text, sizeof(runner->stdin_text), submitted);
    (void)script_set_global_var(runner, "stdin", runner->stdin_text);
    runner->input_var[0] = 0;
    runner->input_prompt[0] = 0;
    runner->waiting_input = 0u;
    script_set_status(runner, 0);
    return 0;
}
