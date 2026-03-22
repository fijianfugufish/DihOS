#include "acpi/aml_tiny.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

static uint8_t rd8(const uint8_t *p) { return p[0]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

static void aml_log(aml_tiny_ctx *ctx, const char *msg)
{
    if (ctx && ctx->host.log)
        ctx->host.log(ctx->host.user, msg);
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
        if (!ctx->host.read_named_int)
            return AML_TINY_ERR_NAMESPACE;

        if (ctx->host.read_named_int(ctx->host.user, v->name, out) != 0)
        {
            aml_log(ctx, "namespace miss");
            aml_log(ctx, v->name);
            *out = 0;
            return AML_TINY_OK;
        }

        return AML_TINY_OK;
    }

    if (v->type == 2)
    {
        if (v->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;
        *out = ctx->locals[(uint32_t)v->ivalue];
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

static int aml_store_to_target(aml_tiny_ctx *ctx, const aml_tiny_value *src, const aml_tiny_value *dst)
{
    uint64_t v;

    if (!ctx || !src || !dst)
        return AML_TINY_ERR_BAD_ARG;

    if (aml_value_as_int(ctx, src, &v) != AML_TINY_OK)
        return AML_TINY_ERR_NAMESPACE;

    if (dst->type == 2)
    {
        if (dst->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;
        ctx->locals[(uint32_t)dst->ivalue] = v;
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
                av->type = 0;
                av->ivalue = v;
                av->name[0] = 0;
                av->buf_len = 0;
                av->pkg_count = 0;
            }
            else
            {
                ctx->method.args[(uint32_t)dst->ivalue] = v;
            }
        }
        return AML_TINY_OK;
    }

    if (dst->type != 1)
        return AML_TINY_ERR_UNSUPPORTED;

    if (!ctx->host.write_named_int)
        return AML_TINY_ERR_NAMESPACE;

    if (ctx->host.write_named_int(ctx->host.user, dst->name, v) != 0)
    {
        aml_log(ctx, "store miss");
        aml_log(ctx, dst->name);
        return AML_TINY_ERR_NAMESPACE;
    }

    return AML_TINY_OK;
}

static int aml_eval_termarg(aml_tiny_ctx *ctx, aml_tiny_value *out);
static int aml_exec_one_term(aml_tiny_ctx *ctx);
static int aml_exec_term_list(aml_tiny_ctx *ctx, const uint8_t *end_limit);

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
    uint32_t pkg_len, pkg_bytes, remain, i;
    aml_tiny_value lenv;
    uint64_t declared_len;

    if (aml_pkg_length(ctx, &pkg_len, &pkg_bytes) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    remain = pkg_len - pkg_bytes;
    if (ctx->p + remain > ctx->end)
        remain = (uint32_t)(ctx->end - ctx->p);

    if (aml_eval_termarg(ctx, &lenv) != AML_TINY_OK)
        return AML_TINY_ERR_PARSE;
    if (aml_value_as_int(ctx, &lenv, &declared_len) != AML_TINY_OK)
        return AML_TINY_ERR_PARSE;

    out->type = 4;
    out->ivalue = 0;
    out->name[0] = 0;
    out->buf_len = 0;
    out->pkg_count = 0;

    for (i = 0; i < remain && out->buf_len < AML_TINY_MAX_BUFFER_BYTES; ++i)
        out->buf[out->buf_len++] = *ctx->p++;

    while (i < remain)
    {
        ctx->p++;
        ++i;
    }

    (void)declared_len;
    return AML_TINY_OK;
}

static int aml_parse_package_object(aml_tiny_ctx *ctx, aml_tiny_value *out, int is_varpkg)
{
    uint32_t pkg_len, pkg_bytes, remain;
    uint8_t count8 = 0;
    aml_tiny_value countv;
    uint64_t count64 = 0;
    const uint8_t *pkg_end;
    uint32_t i;

    if (aml_pkg_length(ctx, &pkg_len, &pkg_bytes) != AML_TINY_OK)
        return AML_TINY_ERR_EOF;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    remain = pkg_len - pkg_bytes;
    if (ctx->p + remain > ctx->end)
        remain = (uint32_t)(ctx->end - ctx->p);

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

    pkg_end = ctx->p + remain;
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    out->type = 5;
    out->ivalue = 0;
    out->name[0] = 0;
    out->buf_len = 0;
    out->pkg_count = 0;

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
        out->ivalue = (av == bv) ? 1ull : 0ull;
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

    return aml_eval_namestring_as_ref(ctx, out);
}

static int aml_exec_if_else(aml_tiny_ctx *ctx)
{
    uint32_t pkg_len;
    uint32_t pkg_bytes;
    const uint8_t *pkg_start;
    const uint8_t *pkg_end;
    aml_tiny_value pred;
    uint64_t pv;
    int rc;

    ctx->p++;

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

    if (pv)
    {
        rc = aml_exec_term_list(ctx, pkg_end);
        if (rc == AML_TINY_ERR_EOF)
            rc = AML_TINY_OK;
        if (rc != AML_TINY_OK)
            return rc;
        ctx->p = pkg_end;

        if (!aml_eof(ctx, 1) && *ctx->p == 0xA1)
        {
            uint32_t else_len;
            uint32_t else_pkg_bytes;

            ctx->p++;
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
    }
    else
    {
        ctx->p = pkg_end;

        if (!aml_eof(ctx, 1) && *ctx->p == 0xA1)
        {
            uint32_t else_len;
            uint32_t else_pkg_bytes;
            const uint8_t *else_end;

            ctx->p++;
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
        }
    }

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
    uint32_t pkg_len;
    uint32_t pkg_bytes;
    const uint8_t *pkg_start;
    const uint8_t *pkg_end;
    uint32_t iter = 0;
    int rc;

    ctx->p++;

    rc = aml_pkg_length(ctx, &pkg_len, &pkg_bytes);
    if (rc != AML_TINY_OK)
        return rc;
    if (pkg_len < pkg_bytes)
        return AML_TINY_ERR_PARSE;

    pkg_start = ctx->p;
    pkg_end = pkg_start + (pkg_len - pkg_bytes);
    if (pkg_end > ctx->end)
        pkg_end = ctx->end;

    while (iter++ < AML_TINY_MAX_WHILE_ITERS)
    {
        aml_tiny_value pred;
        uint64_t pv;

        ctx->p = pkg_start;
        rc = aml_eval_termarg(ctx, &pred);
        if (rc != AML_TINY_OK)
            return rc;
        rc = aml_value_as_int(ctx, &pred, &pv);
        if (rc != AML_TINY_OK)
            return rc;

        if (!pv)
        {
            ctx->p = pkg_end;
            return AML_TINY_OK;
        }

        rc = aml_exec_term_list(ctx, pkg_end);
        if (rc == AML_TINY_ERR_EOF)
            rc = AML_TINY_OK;
        if (rc != AML_TINY_OK)
            return rc;

        if (ctx->returned)
            return AML_TINY_OK;
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
            aml_log(ctx, "term parse fail");
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
        if (rc == AML_TINY_ERR_EOF)
            return AML_TINY_OK;
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
        ctx.locals[i] = 0u;

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
        ctx.locals[i] = 0u;

    while (ctx.p < ctx.end && !ctx.returned)
    {
        rc = aml_exec_one_term(&ctx);
        if (rc != AML_TINY_OK)
            return rc;
    }

    return AML_TINY_OK;
}