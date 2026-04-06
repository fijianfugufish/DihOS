#include "acpi/aml_tiny.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define AML_TINY_RC_BREAK 0x7001

static uint8_t rd8(const uint8_t *p) { return p[0]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

static void aml_log(aml_tiny_ctx *ctx, const char *msg)
{
    if (ctx && ctx->host.log)
        ctx->host.log(ctx->host.user, msg);
}

static void aml_log_hex32(aml_tiny_ctx *ctx, const char *prefix, uint32_t v)
{
    char msg[64];
    const char hex[] = "0123456789ABCDEF";
    uint32_t i = 0;

    if (!ctx || !prefix)
        return;

    while (prefix[i] && i + 1u < sizeof(msg))
    {
        msg[i] = prefix[i];
        ++i;
    }

    if (i + 10u >= sizeof(msg))
        return;

    msg[i++] = '0';
    msg[i++] = 'x';
    msg[i++] = hex[(v >> 28) & 0xF];
    msg[i++] = hex[(v >> 24) & 0xF];
    msg[i++] = hex[(v >> 20) & 0xF];
    msg[i++] = hex[(v >> 16) & 0xF];
    msg[i++] = hex[(v >> 12) & 0xF];
    msg[i++] = hex[(v >> 8) & 0xF];
    msg[i++] = hex[(v >> 4) & 0xF];
    msg[i++] = hex[v & 0xF];
    msg[i] = 0;

    aml_log(ctx, msg);
}

static void aml_log_if_pred(aml_tiny_ctx *ctx, uint64_t pv, uint32_t off)
{
    aml_log_hex32(ctx, "IF off=", off);
    aml_log_hex32(ctx, "IF pred=", (uint32_t)pv);
}

static void aml_log_badop(aml_tiny_ctx *ctx, uint8_t op)
{
    char msg[32];
    const char hex[] = "0123456789ABCDEF";

    msg[0] = 'b';
    msg[1] = 'a';
    msg[2] = 'd';
    msg[3] = ' ';
    msg[4] = 'o';
    msg[5] = 'p';
    msg[6] = ' ';
    msg[7] = '0';
    msg[8] = 'x';
    msg[9] = hex[(op >> 4) & 0xF];
    msg[10] = hex[op & 0xF];
    msg[11] = 0;

    aml_log(ctx, msg);
}

static int aml_eof(aml_tiny_ctx *ctx, uint32_t n)
{
    if (!ctx || !ctx->p || !ctx->end)
        return 1;
    return (ctx->p + n > ctx->end);
}

static int aml_take_u8(aml_tiny_ctx *ctx, uint8_t *out)
{
    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;
    *out = *ctx->p++;
    return AML_TINY_OK;
}

static int aml_take_u16(aml_tiny_ctx *ctx, uint16_t *out)
{
    if (aml_eof(ctx, 2))
        return AML_TINY_ERR_EOF;
    *out = rd16(ctx->p);
    ctx->p += 2;
    return AML_TINY_OK;
}

static int aml_take_u32(aml_tiny_ctx *ctx, uint32_t *out)
{
    if (aml_eof(ctx, 4))
        return AML_TINY_ERR_EOF;
    *out = rd32(ctx->p);
    ctx->p += 4;
    return AML_TINY_OK;
}

static int aml_pkg_length(aml_tiny_ctx *ctx, uint32_t *out_len, uint32_t *out_pkg_bytes)
{
    uint8_t lead;
    uint8_t follow_count;
    uint32_t len;
    uint32_t i;

    if (!out_len || !out_pkg_bytes)
        return AML_TINY_ERR_BAD_ARG;

    if (aml_take_u8(ctx, &lead) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;

    follow_count = (uint8_t)((lead >> 6) & 0x03u);
    len = (uint32_t)(lead & 0x0Fu);

    for (i = 0; i < follow_count; ++i)
    {
        uint8_t b;
        if (aml_take_u8(ctx, &b) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        len |= ((uint32_t)b << (4u + 8u * i));
    }

    *out_len = len;
    *out_pkg_bytes = 1u + (uint32_t)follow_count;
    return AML_TINY_OK;
}

static int aml_skip_pkg_body(aml_tiny_ctx *ctx, uint32_t *out_body_len)
{
    uint32_t pkg_len;
    uint32_t pkg_bytes;
    uint32_t body_len;

    if (!ctx)
        return AML_TINY_ERR_BAD_ARG;

    if (aml_pkg_length(ctx, &pkg_len, &pkg_bytes) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;

    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    body_len = pkg_len - pkg_bytes;

    if (ctx->p + body_len > ctx->end)
        body_len = (uint32_t)(ctx->end - ctx->p);

    if (out_body_len)
        *out_body_len = body_len;

    ctx->p += body_len;
    return AML_TINY_OK;
}

static int is_namechar(uint8_t c)
{
    return ((c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_');
}

static void copy_char(char *dst, uint32_t dst_sz, uint32_t *at, char c)
{
    if (*at + 1u < dst_sz)
    {
        dst[*at] = c;
        (*at)++;
        dst[*at] = 0;
    }
}

static void append_str(char *dst, uint32_t dst_sz, uint32_t *at, const char *src)
{
    while (*src)
    {
        copy_char(dst, dst_sz, at, *src);
        src++;
    }
}

static void aml_make_effective_name(aml_tiny_ctx *ctx, const char *in, char *out, uint32_t out_sz)
{
    uint32_t at = 0;
    uint32_t i = 0;

    if (!out || out_sz == 0)
        return;

    out[0] = 0;

    if (!in || !in[0])
        return;

    /* Already absolute or upward-relative: keep as-is */
    if (in[0] == '\\' || in[0] == '^')
    {
        while (in[i] && at + 1u < out_sz)
        {
            out[at++] = in[i++];
        }
        out[at] = 0;
        return;
    }

    /* Otherwise bind it to the current method scope, if any */
    if (ctx && ctx->method.scope_prefix && ctx->method.scope_prefix[0])
    {
        const char *sp = ctx->method.scope_prefix;

        while (*sp && at + 1u < out_sz)
            out[at++] = *sp++;

        if (at && out[at - 1] != '.' && out[at - 1] != '\\' && at + 1u < out_sz)
            out[at++] = '.';
    }

    i = 0;
    while (in[i] && at + 1u < out_sz)
        out[at++] = in[i++];

    out[at] = 0;
}

static void aml_value_zero(aml_tiny_value *v)
{
    uint32_t i;

    if (!v)
        return;

    v->type = 0;
    v->ivalue = 0;
    v->name[0] = 0;
    v->buf_len = 0;
    v->pkg_count = 0;

    for (i = 0; i < AML_TINY_MAX_BUFFER_BYTES; ++i)
        v->buf[i] = 0;

    for (i = 0; i < AML_TINY_MAX_PACKAGE_ELEMS; ++i)
        v->pkg_elems[i] = 0;
}

static void aml_value_copy(aml_tiny_value *dst, const aml_tiny_value *src)
{
    uint32_t i;

    if (!dst || !src)
        return;

    dst->type = src->type;
    dst->ivalue = src->ivalue;

    for (i = 0; i < AML_TINY_MAX_NAMESTRING; ++i)
        dst->name[i] = src->name[i];

    dst->buf_len = src->buf_len;
    for (i = 0; i < AML_TINY_MAX_BUFFER_BYTES; ++i)
        dst->buf[i] = src->buf[i];

    dst->pkg_count = src->pkg_count;
    for (i = 0; i < AML_TINY_MAX_PACKAGE_ELEMS; ++i)
        dst->pkg_elems[i] = src->pkg_elems[i];
}

static int aml_str_eq(const char *a, const char *b)
{
    uint32_t i = 0;

    if (!a || !b)
        return 0;

    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        ++i;
    }

    return a[i] == 0 && b[i] == 0;
}

static aml_tiny_named_obj *aml_find_named_obj(aml_tiny_ctx *ctx, const char *name)
{
    uint32_t i;

    if (!ctx || !name)
        return NULL;

    for (i = 0; i < 8u; ++i)
    {
        if (ctx->named_objs[i].used && aml_str_eq(ctx->named_objs[i].name, name))
            return &ctx->named_objs[i];
    }

    return NULL;
}

static aml_tiny_named_obj *aml_get_or_create_named_obj(aml_tiny_ctx *ctx, const char *name)
{
    uint32_t i;
    aml_tiny_named_obj *slot;

    slot = aml_find_named_obj(ctx, name);
    if (slot)
        return slot;

    if (!ctx || !name)
        return NULL;

    for (i = 0; i < 8u; ++i)
    {
        if (!ctx->named_objs[i].used)
        {
            uint32_t j = 0;
            ctx->named_objs[i].used = 1;

            while (name[j] && j + 1u < AML_TINY_MAX_NAMESTRING)
            {
                ctx->named_objs[i].name[j] = name[j];
                ++j;
            }
            ctx->named_objs[i].name[j] = 0;

            aml_value_zero(&ctx->named_objs[i].value);
            return &ctx->named_objs[i];
        }
    }

    return NULL;
}

static int aml_parse_nameseg(aml_tiny_ctx *ctx, char out4[5])
{
    uint32_t i;
    if (aml_eof(ctx, 4))
        return AML_TINY_ERR_EOF;

    for (i = 0; i < 4; ++i)
    {
        uint8_t c = ctx->p[i];
        if (!is_namechar(c))
            return AML_TINY_ERR_PARSE;
        out4[i] = (char)c;
    }
    out4[4] = 0;
    ctx->p += 4;
    return AML_TINY_OK;
}

static int aml_parse_namestring(aml_tiny_ctx *ctx, char *out, uint32_t out_sz)
{
    uint32_t at = 0;
    uint8_t b;

    if (!ctx || !out || out_sz < 2)
        return AML_TINY_ERR_BAD_ARG;

    out[0] = 0;

    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    b = *ctx->p;

    if (b == '\\')
    {
        ctx->p++;
        copy_char(out, out_sz, &at, '\\');
    }
    else
    {
        if (ctx->method.scope_prefix && ctx->method.scope_prefix[0])
            append_str(out, out_sz, &at, ctx->method.scope_prefix);
    }

    while (!aml_eof(ctx, 1) && *ctx->p == '^')
    {
        ctx->p++;
        copy_char(out, out_sz, &at, '^');
    }

    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    b = *ctx->p++;

    if (b == 0x2E)
    {
        char seg[5];
        if (at && out[at - 1] != '\\' && out[at - 1] != '^')
            copy_char(out, out_sz, &at, '.');
        if (aml_parse_nameseg(ctx, seg) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        append_str(out, out_sz, &at, seg);

        copy_char(out, out_sz, &at, '.');

        if (aml_parse_nameseg(ctx, seg) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        append_str(out, out_sz, &at, seg);
        return AML_TINY_OK;
    }
    else if (b == 0x2F)
    {
        uint8_t count;
        uint32_t i;
        char seg[5];

        if (aml_take_u8(ctx, &count) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;

        for (i = 0; i < count; ++i)
        {
            if (i != 0 || (at && out[at - 1] != '\\' && out[at - 1] != '^'))
                copy_char(out, out_sz, &at, '.');

            if (aml_parse_nameseg(ctx, seg) != AML_TINY_OK)
                return AML_TINY_ERR_PARSE;
            append_str(out, out_sz, &at, seg);
        }
        return AML_TINY_OK;
    }
    else
    {
        char seg[5];
        ctx->p--;
        if (at && out[at - 1] != '\\' && out[at - 1] != '^')
            copy_char(out, out_sz, &at, '.');
        if (aml_parse_nameseg(ctx, seg) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        append_str(out, out_sz, &at, seg);
        return AML_TINY_OK;
    }
}

static int aml_value_as_int(aml_tiny_ctx *ctx, const aml_tiny_value *v, uint64_t *out)
{
    if (!ctx || !v || !out)
        return AML_TINY_ERR_BAD_ARG;

    if (v->type == 0)
    {
        *out = v->ivalue;
        return AML_TINY_OK;
    }

    if (v->type == 1)
    {
        char full_name[AML_TINY_MAX_NAMESTRING];
        aml_tiny_named_obj *obj;

        aml_make_effective_name(ctx, v->name, full_name, sizeof(full_name));

        obj = aml_find_named_obj(ctx, full_name);
        if (!obj)
            obj = aml_find_named_obj(ctx, v->name);

        if (obj)
            return aml_value_as_int(ctx, &obj->value, out);

        if (!ctx->host.read_named_int)
            return AML_TINY_ERR_NAMESPACE;

        if (ctx->host.read_named_int(ctx->host.user, full_name, out) == 0)
            return AML_TINY_OK;

        if (ctx->host.read_named_int(ctx->host.user, v->name, out) == 0)
            return AML_TINY_OK;

        aml_log(ctx, "namespace miss");
        aml_log(ctx, full_name);
        *out = 0;
        return AML_TINY_OK;
    }

    if (v->type == 2)
    {
        const aml_tiny_value *lv;

        if (v->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;

        lv = &ctx->locals[(uint32_t)v->ivalue];

        if (lv->type == 0)
        {
            *out = lv->ivalue;
            return AML_TINY_OK;
        }

        if (lv->type == 4)
        {
            uint64_t r = 0;
            uint32_t i, n = (lv->buf_len > 8u) ? 8u : lv->buf_len;
            for (i = 0; i < n; ++i)
                r |= ((uint64_t)lv->buf[i] << (8u * i));
            *out = r;
            return AML_TINY_OK;
        }

        if (lv->type == 5)
        {
            *out = (lv->pkg_count > 0u) ? lv->pkg_elems[0] : 0u;
            return AML_TINY_OK;
        }

        if (lv->type == 1)
        {
            if (!ctx->host.read_named_int)
                return AML_TINY_ERR_NAMESPACE;

            if (ctx->host.read_named_int(ctx->host.user, lv->name, out) != 0)
            {
                *out = 0;
                return AML_TINY_OK;
            }

            return AML_TINY_OK;
        }

        *out = 0;
        return AML_TINY_OK;
    }

    if (v->type == 3)
    {
        const aml_tiny_value *av;

        if (v->ivalue >= 7u)
            return AML_TINY_ERR_INTERNAL;

        if (ctx->method.arg_count <= v->ivalue)
        {
            *out = 0;
            return AML_TINY_OK;
        }

        if (!ctx->method.use_typed_args)
        {
            *out = ctx->method.args[(uint32_t)v->ivalue];
            return AML_TINY_OK;
        }

        av = &ctx->method.typed_args[(uint32_t)v->ivalue];

        if (av->type == 0)
        {
            *out = av->ivalue;
            return AML_TINY_OK;
        }

        if (av->type == 4)
        {
            uint64_t r = 0;
            uint32_t i, n = (av->buf_len > 8u) ? 8u : av->buf_len;
            for (i = 0; i < n; ++i)
                r |= ((uint64_t)av->buf[i] << (8u * i));
            *out = r;
            return AML_TINY_OK;
        }

        if (av->type == 5)
        {
            *out = (av->pkg_count > 0u) ? av->pkg_elems[0] : 0u;
            return AML_TINY_OK;
        }

        if (av->type == 1)
        {
            if (!ctx->host.read_named_int)
                return AML_TINY_ERR_NAMESPACE;

            if (ctx->host.read_named_int(ctx->host.user, av->name, out) != 0)
            {
                *out = 0;
                return AML_TINY_OK;
            }
            return AML_TINY_OK;
        }

        *out = 0;
        return AML_TINY_OK;
    }

    if (v->type == 4)
    {
        /* for now expose first dword-ish value when coerced to int */
        uint64_t r = 0;
        uint32_t i, n = (v->buf_len > 8u) ? 8u : v->buf_len;
        for (i = 0; i < n; ++i)
            r |= ((uint64_t)v->buf[i] << (8u * i));
        *out = r;
        return AML_TINY_OK;
    }

    if (v->type == 5)
    {
        *out = (v->pkg_count > 0u) ? v->pkg_elems[0] : 0u;
        return AML_TINY_OK;
    }

    return AML_TINY_ERR_INTERNAL;
}

static int aml_eval_termarg(aml_tiny_ctx *ctx, aml_tiny_value *out);
static int aml_exec_one_term(aml_tiny_ctx *ctx);
static int aml_exec_term_list(aml_tiny_ctx *ctx, const uint8_t *end_limit);

static int aml_materialize_value(aml_tiny_ctx *ctx, const aml_tiny_value *src, aml_tiny_value *out)
{
    aml_tiny_named_obj *obj;
    uint64_t iv = 0;

    if (!ctx || !src || !out)
        return AML_TINY_ERR_BAD_ARG;

    aml_value_zero(out);

    /* Already concrete */
    if (src->type == 0 || src->type == 4 || src->type == 5)
    {
        aml_value_copy(out, src);
        return AML_TINY_OK;
    }

    /* LocalX */
    if (src->type == 2)
    {
        if (src->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;

        return aml_materialize_value(ctx, &ctx->locals[(uint32_t)src->ivalue], out);
    }

    /* ArgX */
    if (src->type == 3)
    {
        if (src->ivalue >= 7u)
            return AML_TINY_ERR_INTERNAL;

        if (ctx->method.arg_count <= src->ivalue)
        {
            aml_value_zero(out);
            out->type = 0;
            out->ivalue = 0;
            return AML_TINY_OK;
        }

        if (ctx->method.use_typed_args)
            return aml_materialize_value(ctx, &ctx->method.typed_args[(uint32_t)src->ivalue], out);

        aml_value_zero(out);
        out->type = 0;
        out->ivalue = ctx->method.args[(uint32_t)src->ivalue];
        return AML_TINY_OK;
    }

    /* Named ref */
    if (src->type == 1)
    {
        char full_name[AML_TINY_MAX_NAMESTRING];

        aml_make_effective_name(ctx, src->name, full_name, sizeof(full_name));

        obj = aml_find_named_obj(ctx, full_name);
        if (!obj)
            obj = aml_find_named_obj(ctx, src->name);

        if (obj)
            return aml_materialize_value(ctx, &obj->value, out);

        if (aml_value_as_int(ctx, src, &iv) == AML_TINY_OK)
        {
            aml_value_zero(out);
            out->type = 0;
            out->ivalue = iv;
            return AML_TINY_OK;
        }

        return AML_TINY_ERR_NAMESPACE;
    }

    return AML_TINY_ERR_UNSUPPORTED;
}

static int aml_value_equal(aml_tiny_ctx *ctx, const aml_tiny_value *a, const aml_tiny_value *b)
{
    aml_tiny_value av;
    aml_tiny_value bv;
    uint32_t i;
    uint64_t ai = 0;
    uint64_t bi = 0;

    if (!ctx || !a || !b)
        return 0;

    aml_log(ctx, "EQ enter");

    if (aml_materialize_value(ctx, a, &av) != AML_TINY_OK)
        aml_value_copy(&av, a);

    if (aml_materialize_value(ctx, b, &bv) != AML_TINY_OK)
        aml_value_copy(&bv, b);

    if (av.type == 4 && bv.type == 4)
    {
        if (av.buf_len != bv.buf_len)
            return 0;

        for (i = 0; i < av.buf_len; ++i)
        {
            if (av.buf[i] != bv.buf[i])
                return 0;
        }

        return 1;
    }

    if (av.type == 5 && bv.type == 5)
    {
        if (av.pkg_count != bv.pkg_count)
            return 0;

        for (i = 0; i < av.pkg_count; ++i)
        {
            if (av.pkg_elems[i] != bv.pkg_elems[i])
                return 0;
        }

        return 1;
    }

    if (aml_value_as_int(ctx, &av, &ai) != AML_TINY_OK)
        return 0;
    if (aml_value_as_int(ctx, &bv, &bi) != AML_TINY_OK)
        return 0;

    aml_log_hex32(ctx, "EQ intA=", (uint32_t)ai);
    aml_log_hex32(ctx, "EQ intB=", (uint32_t)bi);

    if (ai == bi)
    {
        aml_log(ctx, "EQ int match");
        return 1;
    }

    aml_log(ctx, "EQ int mismatch");
    return 0;
}

static int aml_parse_target_or_null(aml_tiny_ctx *ctx, aml_tiny_value *out, int *is_null)
{
    if (!ctx || !out || !is_null)
        return AML_TINY_ERR_BAD_ARG;

    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    if (*ctx->p == 0x00)
    {
        ctx->p++;
        aml_value_zero(out);
        *is_null = 1;
        return AML_TINY_OK;
    }

    *is_null = 0;
    return aml_eval_termarg(ctx, out);
}

static int aml_store_to_target(aml_tiny_ctx *ctx, const aml_tiny_value *src, const aml_tiny_value *dst)
{
    uint64_t v;
    aml_tiny_named_obj *obj;
    aml_tiny_value resolved;

    if (!ctx || !src || !dst)
        return AML_TINY_ERR_BAD_ARG;

    if (aml_materialize_value(ctx, src, &resolved) != AML_TINY_OK)
        return AML_TINY_ERR_NAMESPACE;

    if (dst->type == 2)
    {
        if (dst->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;

        aml_value_copy(&ctx->locals[(uint32_t)dst->ivalue], &resolved);
        return AML_TINY_OK;
    }

    if (dst->type == 3)
    {
        if (dst->ivalue >= 7u)
            return AML_TINY_ERR_INTERNAL;

        if (ctx->method.arg_count > dst->ivalue)
        {
            if (ctx->method.use_typed_args)
            {
                aml_tiny_value *av = &ctx->method.typed_args[(uint32_t)dst->ivalue];
                aml_value_copy(av, &resolved);
            }
            else
            {
                if (aml_value_as_int(ctx, &resolved, &v) != AML_TINY_OK)
                    return AML_TINY_ERR_NAMESPACE;
                ctx->method.args[(uint32_t)dst->ivalue] = v;
            }
        }
        return AML_TINY_OK;
    }

    if (dst->type != 1)
        return AML_TINY_ERR_UNSUPPORTED;

    {
        char full_name[AML_TINY_MAX_NAMESTRING];

        aml_make_effective_name(ctx, dst->name, full_name, sizeof(full_name));

        obj = aml_get_or_create_named_obj(ctx, full_name);
        if (!obj)
            obj = aml_get_or_create_named_obj(ctx, dst->name);

        if (!obj)
            return AML_TINY_ERR_NAMESPACE;

        aml_value_copy(&obj->value, &resolved);

        /* Buffers/packages stay in the tiny namespace only */
        if (resolved.type != 0)
            return AML_TINY_OK;

        if (aml_value_as_int(ctx, &resolved, &v) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;

        if (ctx->host.write_named_int)
        {
            if (ctx->host.write_named_int(ctx->host.user, full_name, v) == 0)
                return AML_TINY_OK;

            if (ctx->host.write_named_int(ctx->host.user, dst->name, v) == 0)
                return AML_TINY_OK;
        }

        return AML_TINY_OK;
    }
}

static int aml_eval_namestring_as_ref(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    out->type = 1;
    out->ivalue = 0;
    out->name[0] = 0;
    out->buf_len = 0;
    out->pkg_count = 0;
    return aml_parse_namestring(ctx, out->name, sizeof(out->name));
}

static int aml_parse_buffer_object(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    uint32_t pkg_len, pkg_bytes;
    aml_tiny_value lenv;
    uint64_t declared_len = 0;
    const uint8_t *pkg_end;
    uint32_t remain, i;

    if (aml_pkg_length(ctx, &pkg_len, &pkg_bytes) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    pkg_end = ctx->p + (pkg_len - pkg_bytes);
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    if (aml_eval_termarg(ctx, &lenv) != AML_TINY_OK)
        return AML_TINY_ERR_PARSE;
    if (aml_value_as_int(ctx, &lenv, &declared_len) != AML_TINY_OK)
        return AML_TINY_ERR_PARSE;

    aml_value_zero(out);
    out->type = 4;

    remain = (uint32_t)(pkg_end - ctx->p);

    /*
     * Copy only the actual initializer bytes left inside this BufferOp package.
     * Do NOT copy past pkg_end.
     */
    out->buf_len = (uint32_t)declared_len;
    if (out->buf_len > AML_TINY_MAX_BUFFER_BYTES)
        out->buf_len = AML_TINY_MAX_BUFFER_BYTES;

    for (i = 0; i < out->buf_len; ++i)
        out->buf[i] = 0;

    for (i = 0; i < remain && i < out->buf_len; ++i)
        out->buf[i] = *ctx->p++;

    /* skip any trailing bytes still inside this package */
    ctx->p = pkg_end;

    return AML_TINY_OK;
}

static int aml_parse_package_object(aml_tiny_ctx *ctx, aml_tiny_value *out, int is_varpkg)
{
    uint32_t pkg_len, pkg_bytes;
    uint8_t count8 = 0;
    aml_tiny_value countv;
    uint64_t count64 = 0;
    const uint8_t *pkg_end;
    uint32_t i;

    if (aml_pkg_length(ctx, &pkg_len, &pkg_bytes) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    pkg_end = ctx->p + (pkg_len - pkg_bytes);
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    if (is_varpkg)
    {
        if (aml_eval_termarg(ctx, &countv) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_value_as_int(ctx, &countv, &count64) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
    }
    else
    {
        if (aml_take_u8(ctx, &count8) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        count64 = count8;
    }

    aml_value_zero(out);
    out->type = 5;

    for (i = 0; i < (uint32_t)count64 && ctx->p < pkg_end && out->pkg_count < AML_TINY_MAX_PACKAGE_ELEMS; ++i)
    {
        aml_tiny_value elem;
        uint64_t iv = 0;

        if (aml_eval_termarg(ctx, &elem) != AML_TINY_OK)
            break;

        if (aml_value_as_int(ctx, &elem, &iv) != AML_TINY_OK)
            iv = 0;

        out->pkg_elems[out->pkg_count++] = iv;
    }

    ctx->p = pkg_end;
    return AML_TINY_OK;
}

static int aml_exec_nameop(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    aml_tiny_value name_ref;
    aml_tiny_value init_val;
    aml_tiny_named_obj *obj;
    int rc;

    ctx->p++; /* consume 0x08 */

    rc = aml_eval_namestring_as_ref(ctx, &name_ref);
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_eval_termarg(ctx, &init_val);
    if (rc != AML_TINY_OK)
        return rc;

    obj = aml_get_or_create_named_obj(ctx, name_ref.name);
    if (!obj)
        return AML_TINY_ERR_INTERNAL;

    aml_value_copy(&obj->value, &init_val);

    aml_value_copy(out, &name_ref);
    return AML_TINY_OK;
}

static int aml_eval_termarg(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    uint8_t op;

    if (!ctx || !out)
        return AML_TINY_ERR_BAD_ARG;
    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    out->type = 0;
    out->ivalue = 0;
    out->name[0] = 0;
    out->buf_len = 0;
    out->pkg_count = 0;

    op = *ctx->p;

    if (op == 0x00)
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = 0;
        return AML_TINY_OK;
    }
    if (op == 0x01)
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = 1;
        return AML_TINY_OK;
    }
    if (op == 0xFF)
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = ~0ull;
        return AML_TINY_OK;
    }
    if (op == 0x0A)
    {
        uint8_t v;
        ctx->p++;
        if (aml_take_u8(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        return AML_TINY_OK;
    }
    if (op == 0x0B)
    {
        uint16_t v;
        ctx->p++;
        if (aml_take_u16(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        return AML_TINY_OK;
    }
    if (op == 0x0C)
    {
        uint32_t v;
        ctx->p++;
        if (aml_take_u32(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        return AML_TINY_OK;
    }

    if (op == 0x68)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 0;
        return AML_TINY_OK;
    }
    if (op == 0x69)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 1;
        return AML_TINY_OK;
    }
    if (op == 0x6A)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 2;
        return AML_TINY_OK;
    }
    if (op == 0x6B)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 3;
        return AML_TINY_OK;
    }
    if (op == 0x6C)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 4;
        return AML_TINY_OK;
    }
    if (op == 0x6D)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 5;
        return AML_TINY_OK;
    }
    if (op == 0x6E)
    {
        ctx->p++;
        out->type = 3;
        out->ivalue = 6;
        return AML_TINY_OK;
    }

    if (op >= 0x60 && op <= 0x67)
    {
        ctx->p++;
        out->type = 2;
        out->ivalue = (uint64_t)(op - 0x60u);
        return AML_TINY_OK;
    }

    if (op == 0x93)
    {
        aml_tiny_value a, b;
        ctx->p++;
        if (aml_eval_termarg(ctx, &a) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_eval_termarg(ctx, &b) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        out->type = 0;
        out->ivalue = aml_value_equal(ctx, &a, &b) ? 1ull : 0ull;
        return AML_TINY_OK;
    }

    if (op == 0x92)
    {
        aml_tiny_value a;
        uint64_t av;
        ctx->p++;
        if (aml_eval_termarg(ctx, &a) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_value_as_int(ctx, &a, &av) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;
        out->type = 0;
        out->ivalue = av ? 0ull : 1ull;
        return AML_TINY_OK;
    }

    if (op == 0x7B)
    {
        aml_tiny_value a, b;
        uint64_t av, bv;
        ctx->p++;
        if (aml_eval_termarg(ctx, &a) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_eval_termarg(ctx, &b) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_value_as_int(ctx, &a, &av) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;
        if (aml_value_as_int(ctx, &b, &bv) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;
        out->type = 0;
        out->ivalue = av & bv;
        return AML_TINY_OK;
    }

    if (op == 0x7D)
    {
        aml_tiny_value a, b;
        uint64_t av, bv;
        ctx->p++;
        if (aml_eval_termarg(ctx, &a) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_eval_termarg(ctx, &b) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;
        if (aml_value_as_int(ctx, &a, &av) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;
        if (aml_value_as_int(ctx, &b, &bv) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;
        out->type = 0;
        out->ivalue = av | bv;
        return AML_TINY_OK;
    }

    if (op == 0x08)
    {
        return aml_exec_nameop(ctx, out);
    }

    if (op == 0x11)
    {
        ctx->p++;
        return aml_parse_buffer_object(ctx, out);
    }

    if (op == 0x12)
    {
        ctx->p++;
        return aml_parse_package_object(ctx, out, 0);
    }

    if (op == 0x13)
    {
        ctx->p++;
        return aml_parse_package_object(ctx, out, 1);
    }

    if (op == 0x14)
    {
        uint32_t body_len;
        ctx->p++;
        if (aml_skip_pkg_body(ctx, &body_len) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = 0;
        return AML_TINY_OK;
    }

    if (op == 0x96) /* ToBuffer */
    {
        aml_tiny_value src, target, resolved;
        uint64_t iv = 0;
        int is_null = 0;
        uint32_t i;

        ctx->p++;

        if (aml_eval_termarg(ctx, &src) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (aml_parse_target_or_null(ctx, &target, &is_null) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (aml_materialize_value(ctx, &src, &resolved) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;

        aml_value_zero(out);
        out->type = 4;

        if (resolved.type == 4)
        {
            aml_value_copy(out, &resolved);
        }
        else if (resolved.type == 0)
        {
            iv = resolved.ivalue;
            out->buf_len = 8u;
            for (i = 0; i < out->buf_len; ++i)
                out->buf[i] = (uint8_t)((iv >> (8u * i)) & 0xFFu);
        }
        else if (aml_value_as_int(ctx, &resolved, &iv) == AML_TINY_OK)
        {
            out->buf_len = 8u;
            for (i = 0; i < out->buf_len; ++i)
                out->buf[i] = (uint8_t)((iv >> (8u * i)) & 0xFFu);
        }
        else
        {
            return AML_TINY_ERR_NAMESPACE;
        }

        if (!is_null)
        {
            if (aml_store_to_target(ctx, out, &target) != AML_TINY_OK)
                return AML_TINY_ERR_NAMESPACE;
        }

        return AML_TINY_OK;
    }

    if (op == 0x99) /* ToInteger */
    {
        aml_tiny_value src, target;
        uint64_t iv = 0;
        int is_null = 0;

        ctx->p++;

        if (aml_eval_termarg(ctx, &src) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (aml_parse_target_or_null(ctx, &target, &is_null) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (aml_value_as_int(ctx, &src, &iv) != AML_TINY_OK)
            return AML_TINY_ERR_NAMESPACE;

        aml_value_zero(out);
        out->type = 0;
        out->ivalue = iv;

        if (!is_null)
        {
            if (aml_store_to_target(ctx, out, &target) != AML_TINY_OK)
                return AML_TINY_ERR_NAMESPACE;
        }

        return AML_TINY_OK;
    }

    if (op == 0x9D) /* CopyObject */
    {
        aml_tiny_value src, target;
        int is_null = 0;

        ctx->p++;

        if (aml_eval_termarg(ctx, &src) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (aml_parse_target_or_null(ctx, &target, &is_null) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        aml_value_copy(out, &src);

        if (!is_null)
        {
            if (aml_store_to_target(ctx, out, &target) != AML_TINY_OK)
                return AML_TINY_ERR_NAMESPACE;
        }

        return AML_TINY_OK;
    }

    if (op == 0x0D) /* StringPrefix */
    {
        uint32_t i = 0;

        ctx->p++; /* skip prefix */

        aml_value_zero(out);
        out->type = 4; /* keep it simple: represent strings as byte buffers */

        while (!aml_eof(ctx, 1) && *ctx->p != 0)
        {
            if (out->buf_len < AML_TINY_MAX_BUFFER_BYTES)
                out->buf[out->buf_len++] = *ctx->p;
            ctx->p++;
            ++i;
        }

        if (aml_eof(ctx, 1))
            return AML_TINY_ERR_EOF;

        ctx->p++; /* consume trailing NUL */
        return AML_TINY_OK;
    }

    return aml_eval_namestring_as_ref(ctx, out);
}

static const uint8_t *aml_skip_one_object(const uint8_t *p, const uint8_t *end)
{
    aml_tiny_ctx tmp;
    aml_tiny_value v;
    uint32_t pkg_len, pkg_bytes;
    int rc;

    if (!p || !end || p >= end)
        return p;

    tmp.method.aml = NULL;
    tmp.method.aml_len = 0;
    tmp.method.scope_prefix = NULL;
    tmp.method.arg_count = 0;
    tmp.method.use_typed_args = 0;
    tmp.host.log = NULL;
    tmp.host.read_named_int = NULL;
    tmp.host.write_named_int = NULL;
    tmp.host.user = NULL;
    tmp.p = p;
    tmp.end = end;
    tmp.returned = 0;
    tmp.return_value = 0;
    tmp.last_error = AML_TINY_OK;

    for (uint32_t i = 0; i < 8u; ++i)
    {
        tmp.locals[i].type = 0;
        tmp.locals[i].ivalue = 0;
        tmp.named_objs[i].used = 0;
        tmp.named_objs[i].name[0] = 0;
        tmp.named_objs[i].value.type = 0;
        tmp.named_objs[i].value.ivalue = 0;
    }

    switch (*tmp.p)
    {
    case 0xA0: /* IfOp */
    {
        const uint8_t *q;

        tmp.p++; /* consume IfOp */
        rc = aml_pkg_length(&tmp, &pkg_len, &pkg_bytes);
        if (rc != AML_TINY_OK || pkg_len < pkg_bytes)
            return p + 1;

        q = tmp.p + (pkg_len - pkg_bytes);
        if (q > end)
            q = end;

        /* IMPORTANT: carry a directly attached Else with this If */
        if (q < end && *q == 0xA1)
        {
            aml_tiny_ctx e = tmp;
            uint32_t else_len, else_pkg_bytes;

            e.p = q + 1; /* skip ElseOp */
            rc = aml_pkg_length(&e, &else_len, &else_pkg_bytes);
            if (rc == AML_TINY_OK && else_len >= else_pkg_bytes)
            {
                q = e.p + (else_len - else_pkg_bytes);
                if (q > end)
                    q = end;
            }
        }

        return q;
    }

    case 0xA1: /* ElseOp */
    case 0xA2: /* WhileOp */
    case 0x14: /* MethodOp */
    case 0x11: /* BufferOp */
    case 0x12: /* PackageOp */
    case 0x13: /* VarPackageOp */
    {
        tmp.p++; /* consume opcode */
        rc = aml_pkg_length(&tmp, &pkg_len, &pkg_bytes);
        if (rc == AML_TINY_OK && pkg_len >= pkg_bytes)
        {
            const uint8_t *q = tmp.p + (pkg_len - pkg_bytes);
            if (q > end)
                q = end;
            return q;
        }
        return p + 1;
    }

    case 0x70: /* StoreOp */
    {
        tmp.p++;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        return tmp.p;
    }

    case 0x86: /* NotifyOp */
    {
        tmp.p++;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        return tmp.p;
    }

    case 0xA4: /* ReturnOp */
    {
        tmp.p++;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        return tmp.p;
    }

    case 0xA5: /* BreakOp */
        return p + 1;

    case 0x08: /* NameOp */
    {
        tmp.p++;
        if (aml_parse_namestring(&tmp, v.name, sizeof(v.name)) != AML_TINY_OK)
            return p + 1;
        if (aml_eval_termarg(&tmp, &v) != AML_TINY_OK)
            return p + 1;
        return tmp.p;
    }

    default:
    {
        uint8_t op = *tmp.p;

        if (op == 0xA0 || op == 0xA1 || op == 0xA2 || op == 0xA4 || op == 0xA5)
            return p + 1;

        if (aml_eval_termarg(&tmp, &v) == AML_TINY_OK && tmp.p > p)
            return tmp.p;

        return p + 1;
    }
    }
}

static int aml_exec_if_else(aml_tiny_ctx *ctx)
{
    uint32_t pkg_len;
    uint32_t pkg_bytes;
    const uint8_t *pkg_start;
    const uint8_t *pkg_end;
    const uint8_t *then_start;
    const uint8_t *then_end;
    aml_tiny_value pred;
    uint64_t pv;
    int rc;

    ctx->p++; /* skip IfOp */

#ifdef AML_TINY_DEBUG_IF
    aml_log(ctx, "EXEC: IF");
#endif

    rc = aml_pkg_length(ctx, &pkg_len, &pkg_bytes);
    if (rc != AML_TINY_OK)
        return rc;

    pkg_start = ctx->p;

    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    pkg_end = pkg_start + (pkg_len - pkg_bytes);
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    rc = aml_eval_termarg(ctx, &pred);
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_value_as_int(ctx, &pred, &pv);
    if (rc != AML_TINY_OK)
        return rc;

    aml_log_if_pred(ctx, pv, (uint32_t)(ctx->p - ctx->method.aml));

    then_start = ctx->p;

    /* Walk AML objects, not raw bytes, so we only stop at a top-level ElseOp. */
    then_end = then_start;
    while (then_end < pkg_end)
    {
        const uint8_t *prev = then_end;

        if (*then_end == 0xA1)
            break;

        then_end = aml_skip_one_object(then_end, pkg_end);

        if (!then_end || then_end <= prev)
            break;
    }

    if (pv)
    {
        ctx->p = then_start;
        rc = aml_exec_term_list(ctx, then_end);
        if (rc == AML_TINY_ERR_EOF)
            rc = AML_TINY_OK;
        if (rc != AML_TINY_OK)
            return rc;

        ctx->p = then_end;

        if (ctx->p < pkg_end && *ctx->p == 0xA1)
        {
            uint32_t else_len;
            uint32_t else_pkg_bytes;

            ctx->p++; /* consume ElseOp */
            rc = aml_pkg_length(ctx, &else_len, &else_pkg_bytes);
            if (rc != AML_TINY_OK)
                return rc;
            if (else_len < else_pkg_bytes)
                return AML_TINY_ERR_PARSE;

            if (ctx->p + (else_len - else_pkg_bytes) > ctx->end)
                ctx->p = ctx->end;
            else
                ctx->p += (else_len - else_pkg_bytes);
        }

        return AML_TINY_OK;
    }

    ctx->p = then_end;

    if (ctx->p < pkg_end && *ctx->p == 0xA1)
    {
        uint32_t else_len;
        uint32_t else_pkg_bytes;
        const uint8_t *else_end;

        ctx->p++; /* consume ElseOp */
        rc = aml_pkg_length(ctx, &else_len, &else_pkg_bytes);
        if (rc != AML_TINY_OK)
            return rc;
        if (else_len < else_pkg_bytes)
            return AML_TINY_ERR_PARSE;

        else_end = ctx->p + (else_len - else_pkg_bytes);
        if (else_end > ctx->end)
            else_end = ctx->end;

        rc = aml_exec_term_list(ctx, else_end);
        if (rc == AML_TINY_ERR_EOF)
            rc = AML_TINY_OK;
        if (rc != AML_TINY_OK)
            return rc;

        ctx->p = else_end;
        return AML_TINY_OK;
    }

    ctx->p = pkg_end;
    return AML_TINY_OK;
}

static int aml_exec_store(aml_tiny_ctx *ctx)
{
    aml_tiny_value src, dst;
    int rc;

    ctx->p++;

    rc = aml_eval_termarg(ctx, &src);
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_eval_termarg(ctx, &dst);
    if (rc != AML_TINY_OK)
        return rc;

    return aml_store_to_target(ctx, &src, &dst);
}

static int aml_exec_return(aml_tiny_ctx *ctx)
{
    aml_tiny_value v;
    uint64_t iv;
    int rc;

    ctx->p++;

    rc = aml_eval_termarg(ctx, &v);
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_value_as_int(ctx, &v, &iv);
    if (rc != AML_TINY_OK)
        return rc;

    ctx->returned = 1;
    ctx->return_value = iv;
    return AML_TINY_OK;
}

static int aml_exec_notify(aml_tiny_ctx *ctx)
{
    aml_tiny_value target, value;
    uint64_t iv;
    int rc;

    ctx->p++;

    rc = aml_eval_termarg(ctx, &target);
    if (rc == AML_TINY_ERR_EOF)
    {
        aml_log(ctx, "notify target truncated");
        return AML_TINY_OK;
    }
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_eval_termarg(ctx, &value);
    if (rc == AML_TINY_ERR_EOF)
    {
        aml_log(ctx, "notify value truncated");
        return AML_TINY_OK;
    }
    if (rc != AML_TINY_OK)
        return rc;

    rc = aml_value_as_int(ctx, &value, &iv);
    if (rc != AML_TINY_OK)
        return rc;

    aml_log(ctx, "notify");
    aml_log(ctx, target.name[0] ? target.name : "(non-name target)");
    return AML_TINY_OK;
}

static int aml_exec_while(aml_tiny_ctx *ctx)
{
    uint32_t pkg_len, pkg_bytes;
    const uint8_t *pkg_start;
    const uint8_t *pkg_end;
    const uint8_t *pred_start;
    const uint8_t *body_start;
    aml_tiny_value pred;
    uint64_t pv;
    uint32_t iter;
    int rc;

    ctx->p++; /* skip WhileOp */

    rc = aml_pkg_length(ctx, &pkg_len, &pkg_bytes);
    if (rc != AML_TINY_OK)
        return rc;

    pkg_start = ctx->p;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    pkg_end = pkg_start + (pkg_len - pkg_bytes);
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    pred_start = ctx->p;
    rc = aml_eval_termarg(ctx, &pred);
    if (rc != AML_TINY_OK)
        return rc;
    body_start = ctx->p;

    aml_log_hex32(ctx, "WHILE pred_off=", (uint32_t)(pred_start - ctx->method.aml));
    aml_log_hex32(ctx, "WHILE body_off=", (uint32_t)(body_start - ctx->method.aml));
    aml_log_hex32(ctx, "WHILE end_off=",  (uint32_t)(pkg_end - ctx->method.aml));

    for (iter = 0; iter < AML_TINY_MAX_WHILE_ITERS; ++iter)
    {
        const uint8_t *body_entry;

        if (ctx->returned)
        {
            ctx->p = pkg_end;
            return AML_TINY_OK;
        }

        ctx->p = pred_start;

        rc = aml_eval_termarg(ctx, &pred);
        if (rc != AML_TINY_OK)
            return rc;

        rc = aml_value_as_int(ctx, &pred, &pv);
        if (rc != AML_TINY_OK)
            return rc;

        body_start = ctx->p;
        body_entry = body_start;

        if (iter == 0u)
        {
            aml_log_hex32(ctx, "WHILE iter=", iter);
            aml_log_hex32(ctx, "WHILE pred=", (uint32_t)pv);
            aml_log_hex32(ctx, "WHILE p=", (uint32_t)(ctx->p - ctx->method.aml));
        }

        if (!pv)
        {
            aml_log(ctx, "WHILE exit pred=0");
            ctx->p = pkg_end;
            return AML_TINY_OK;
        }

        ctx->p = body_start;

        rc = aml_exec_term_list(ctx, pkg_end);

        if (rc == AML_TINY_RC_BREAK)
        {
            aml_log(ctx, "WHILE break");
            ctx->p = pkg_end;
            return AML_TINY_OK;
        }

        if (rc == AML_TINY_ERR_EOF)
            rc = AML_TINY_OK;

        if (rc != AML_TINY_OK)
            return rc;

        if (ctx->returned)
        {
            aml_log(ctx, "WHILE return");
            ctx->p = pkg_end;
            return AML_TINY_OK;
        }

        /*
          If the body made literally no forward progress and predicate stays true,
          this is almost certainly a stubbed-environment loop.
        */
        if (ctx->p == body_entry)
        {
            aml_log(ctx, "WHILE no body progress");
        }
    }

    aml_log(ctx, "while iteration cap");
    ctx->p = pkg_end;
    return AML_TINY_OK;
}

static int aml_exec_one_term(aml_tiny_ctx *ctx)
{
    uint8_t op;

    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    op = *ctx->p;

    if (op == 0xA5) /* BreakOp */
    {
        ctx->p++;
        return AML_TINY_RC_BREAK;
    }

    if (op == 0xA1) /* ElseOp */
    {
        uint32_t else_len, else_pkg_bytes;

        ctx->p++; /* consume ElseOp */

        if (aml_pkg_length(ctx, &else_len, &else_pkg_bytes) != AML_TINY_OK)
            return AML_TINY_ERR_PARSE;

        if (else_len < else_pkg_bytes)
            return AML_TINY_ERR_PARSE;

        if (ctx->p + (else_len - else_pkg_bytes) > ctx->end)
            ctx->p = ctx->end;
        else
            ctx->p += (else_len - else_pkg_bytes);

        return AML_TINY_OK;
    }

    switch (op)
    {
    case 0x70:
        return aml_exec_store(ctx);

    case 0x86:
        return aml_exec_notify(ctx);

    case 0xA0:
        return aml_exec_if_else(ctx);

    case 0xA2:
        return aml_exec_while(ctx);

    case 0xA4:
        return aml_exec_return(ctx);

    default:
    {
        aml_tiny_value dummy;
        int rc = aml_eval_termarg(ctx, &dummy);

        if (rc != AML_TINY_OK)
        {
            if (*ctx->p == 0x00)
            {
                ctx->p++;
                return AML_TINY_OK;
            }

            aml_log(ctx, "term parse fail");
            aml_log_badop(ctx, *ctx->p);
            ctx->p++;
            return AML_TINY_OK;
        }

        return AML_TINY_OK;
    }
    }
}

static int aml_exec_term_list(aml_tiny_ctx *ctx, const uint8_t *end_limit)
{
    int rc;

    while (ctx->p < end_limit && !ctx->returned)
    {
        rc = aml_exec_one_term(ctx);

        if (rc == AML_TINY_RC_BREAK)
            return rc;

        if (rc != AML_TINY_OK)
            return rc;
    }

    if (ctx->p > end_limit)
        ctx->p = end_limit;

    return AML_TINY_OK;
}

int aml_tiny_exec(
    const aml_tiny_method *method,
    const aml_tiny_host *host,
    uint64_t *out_return_value)
{
    aml_tiny_ctx ctx;
    int rc;
    uint32_t i;

    if (!method || !method->aml || !host)
        return AML_TINY_ERR_BAD_ARG;

    ctx.host = *host;
    ctx.method = *method;

    if (!ctx.method.use_typed_args)
    {
        uint32_t i;
        for (i = 0; i < 7u; ++i)
        {
            ctx.method.typed_args[i].type = 0;
            ctx.method.typed_args[i].ivalue = ctx.method.args[i];
            ctx.method.typed_args[i].name[0] = 0;
            ctx.method.typed_args[i].buf_len = 0;
            ctx.method.typed_args[i].pkg_count = 0;
        }
    }

    ctx.p = method->aml;
    ctx.end = method->aml + method->aml_len;
    ctx.returned = 0;
    ctx.return_value = 0;
    ctx.last_error = AML_TINY_OK;

    for (i = 0; i < 8u; ++i)
        aml_value_zero(&ctx.locals[i]);

    for (i = 0; i < 8u; ++i)
    {
        ctx.named_objs[i].used = 0;
        ctx.named_objs[i].name[0] = 0;
        aml_value_zero(&ctx.named_objs[i].value);
    }

    rc = aml_exec_term_list(&ctx, ctx.end);
    if (rc != AML_TINY_OK)
    {
        aml_log(&ctx, "exec fail");
        return rc;
    }

    if (out_return_value)
        *out_return_value = ctx.return_value;

    return AML_TINY_OK;
}

int aml_tiny_trace_names(
    const aml_tiny_method *method,
    const aml_tiny_host *host)
{
    aml_tiny_ctx ctx;
    int rc;
    uint32_t i;

    if (!method || !method->aml || !host)
        return AML_TINY_ERR_BAD_ARG;

    ctx.host = *host;
    ctx.method = *method;
    ctx.p = method->aml;
    ctx.end = method->aml + method->aml_len;
    ctx.returned = 0;
    ctx.return_value = 0;
    ctx.last_error = AML_TINY_OK;

    for (i = 0; i < 8u; ++i)
        aml_value_zero(&ctx.locals[i]);

    for (i = 0; i < 8u; ++i)
    {
        ctx.named_objs[i].used = 0;
        ctx.named_objs[i].name[0] = 0;
        aml_value_zero(&ctx.named_objs[i].value);
    }

    while (ctx.p < ctx.end && !ctx.returned)
    {
        rc = aml_exec_one_term(&ctx);
        if (rc != AML_TINY_OK)
            return rc;
    }

    return AML_TINY_OK;
}
