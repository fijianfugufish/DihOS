#include "acpi/aml_tiny.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---------- small local helpers ---------- */

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

/*
  Very small AML NameString parser.

  Supports:
  - Root prefix '\'
  - Parent prefix '^' repeated
  - SimpleNameSeg
  - DualNamePrefix 0x2E
  - MultiNamePrefix 0x2F

  Does not support every AML namespace feature.
*/
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
        /* relative to current scope */
        if (ctx->method.scope_prefix && ctx->method.scope_prefix[0])
        {
            append_str(out, out_sz, &at, ctx->method.scope_prefix);
        }
    }

    while (!aml_eof(ctx, 1) && *ctx->p == '^')
    {
        ctx->p++;
        copy_char(out, out_sz, &at, '^');
    }

    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    b = *ctx->p++;

    if (b == 0x2E) /* DualNamePrefix */
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
    else if (b == 0x2F) /* MultiNamePrefix */
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
        ctx->p--; /* unread */
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

    if (v->type == 0) /* integer */
    {
        *out = v->ivalue;
        return AML_TINY_OK;
    }

    if (v->type == 1) /* name ref */
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

    if (v->type == 2) /* local ref */
    {
        if (v->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;

        *out = ctx->locals[(uint32_t)v->ivalue];
        return AML_TINY_OK;
    }

    if (v->type == 3) /* arg ref */
    {
        if (v->ivalue >= 7u)
            return AML_TINY_ERR_INTERNAL;

        *out = (ctx->method.arg_count > v->ivalue) ? ctx->method.args[(uint32_t)v->ivalue] : 0u;
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

    if (dst->type == 2) /* local ref */
    {
        if (dst->ivalue >= 8u)
            return AML_TINY_ERR_INTERNAL;

        ctx->locals[(uint32_t)dst->ivalue] = v;
        return AML_TINY_OK;
    }

    if (dst->type == 3) /* arg ref */
    {
        if (dst->ivalue >= 7u)
            return AML_TINY_ERR_INTERNAL;

        if (ctx->method.arg_count > dst->ivalue)
            ctx->method.args[(uint32_t)dst->ivalue] = v;
        return AML_TINY_OK;
    }

    if (dst->type != 1) /* must be name ref from here on */
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

/* ---------- forward decls ---------- */

static int aml_eval_termarg(aml_tiny_ctx *ctx, aml_tiny_value *out);
static int aml_exec_one_term(aml_tiny_ctx *ctx);
static int aml_exec_term_list(aml_tiny_ctx *ctx, const uint8_t *end_limit);

/* ---------- evaluator ---------- */

static int aml_eval_namestring_as_ref(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    out->type = 1;
    out->ivalue = 0;
    out->name[0] = 0;
    return aml_parse_namestring(ctx, out->name, sizeof(out->name));
}

static int aml_eval_termarg(aml_tiny_ctx *ctx, aml_tiny_value *out)
{
    uint8_t op;

    if (!ctx || !out)
        return AML_TINY_ERR_BAD_ARG;
    if (aml_eof(ctx, 1))
        return AML_TINY_ERR_EOF;

    op = *ctx->p;

    /* Integer constants */
    if (op == 0x00) /* ZeroOp */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = 0;
        out->name[0] = 0;
        return AML_TINY_OK;
    }
    if (op == 0x01) /* OneOp */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = 1;
        out->name[0] = 0;
        return AML_TINY_OK;
    }
    if (op == 0xFF) /* OnesOp */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = ~0ull;
        out->name[0] = 0;
        return AML_TINY_OK;
    }
    if (op == 0x0A) /* ByteConst */
    {
        uint8_t v;
        ctx->p++;
        if (aml_take_u8(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        out->name[0] = 0;
        return AML_TINY_OK;
    }
    if (op == 0x0B) /* WordConst */
    {
        uint16_t v;
        ctx->p++;
        if (aml_take_u16(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        out->name[0] = 0;
        return AML_TINY_OK;
    }
    if (op == 0x0C) /* DWordConst */
    {
        uint32_t v;
        ctx->p++;
        if (aml_take_u32(ctx, &v) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;
        out->type = 0;
        out->ivalue = v;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* Method arguments */
    if (op == 0x68) /* Arg0Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 0u) ? ctx->method.args[0] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x69) /* Arg1Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 1u) ? ctx->method.args[1] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x6A) /* Arg2Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 2u) ? ctx->method.args[2] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x6B) /* Arg3Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 3u) ? ctx->method.args[3] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x6C) /* Arg4Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 4u) ? ctx->method.args[4] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x6D) /* Arg5Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 5u) ? ctx->method.args[5] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x6E) /* Arg6Op */
    {
        ctx->p++;
        out->type = 0;
        out->ivalue = (ctx->method.arg_count > 6u) ? ctx->method.args[6] : 0u;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* Method locals */
    if (op >= 0x60 && op <= 0x67) /* Local0Op .. Local7Op */
    {
        ctx->p++;
        out->type = 2;
        out->ivalue = (uint64_t)(op - 0x60u);
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* Logical / integer ops */
    if (op == 0x93) /* LEqualOp */
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
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x92) /* LNotOp */
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
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x7B) /* AndOp */
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
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    if (op == 0x7D) /* OrOp */
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
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* BufferOp */
    if (op == 0x11)
    {
        uint32_t body_len;
        ctx->p++;

        /*
          We do not model Buffer objects fully yet.
          Skip the package body and treat the buffer value as integer 0.
        */
        if (aml_skip_pkg_body(ctx, &body_len) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;

        out->type = 0;
        out->ivalue = 0;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* PackageOp */
    if (op == 0x12)
    {
        uint32_t body_len;
        ctx->p++;

        if (aml_skip_pkg_body(ctx, &body_len) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;

        out->type = 0;
        out->ivalue = 0;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* VarPackageOp */
    if (op == 0x13)
    {
        uint32_t body_len;
        ctx->p++;

        if (aml_skip_pkg_body(ctx, &body_len) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;

        out->type = 0;
        out->ivalue = 0;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /* MethodOp encountered in a context we do not execute directly */
    if (op == 0x14)
    {
        uint32_t body_len;
        ctx->p++;

        if (aml_skip_pkg_body(ctx, &body_len) != AML_TINY_OK)
            return AML_TINY_ERR_EOF;

        out->type = 0;
        out->ivalue = 0;
        out->name[0] = 0;
        return AML_TINY_OK;
    }

    /*
      Otherwise treat as NameString reference.
      This is a simplification, but useful for tiny control methods.
    */
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

    /* current byte should be 0xA0 */
    ctx->p++;

    rc = aml_pkg_length(ctx, &pkg_len, &pkg_bytes);
    if (rc != AML_TINY_OK)
        return rc;

    /*
      AML PkgLength includes the bytes of the PkgLength encoding itself.
      Since ctx->p is already positioned *after* those bytes, the remaining
      package body is (pkg_len - pkg_bytes) bytes long.
    */
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

        if (!aml_eof(ctx, 1) && *ctx->p == 0xA1) /* ElseOp */
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

        if (!aml_eof(ctx, 1) && *ctx->p == 0xA1) /* ElseOp */
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

    /* current byte should be 0x70 */
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

    /* current byte should be 0xA4 */
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

    /* current byte should be 0x86 */
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

    /* Tiny interpreter: consume Notify and log it, but do not require backend support yet */
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
    int rc;

    /* current byte should be 0xA2 */
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

    /*
      Bootstrapping-friendly behaviour:
      do not actually iterate, just skip the while body.
    */
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
    case 0x70: /* StoreOp */
        return aml_exec_store(ctx);

    case 0x86: /* NotifyOp */
        return aml_exec_notify(ctx);

    case 0xA0: /* IfOp */
        return aml_exec_if_else(ctx);

    case 0xA2: /* WhileOp */
        return aml_exec_while(ctx);

    case 0xA4: /* ReturnOp */
        return aml_exec_return(ctx);

    default:
    {
        aml_tiny_value dummy;
        int rc = aml_eval_termarg(ctx, &dummy);

        if (rc != AML_TINY_OK)
        {
            /*
              Bootstrapping-friendly resync:
              if we cannot parse this byte as a term, skip one byte and keep
              going instead of aborting the whole AML method.
            */
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

/* ---------- public API ---------- */

int aml_tiny_exec(
    const aml_tiny_method *method,
    const aml_tiny_host *host,
    uint64_t *out_return_value)
{
    aml_tiny_ctx ctx;
    int rc;

    if (!method || !method->aml || !host)
        return AML_TINY_ERR_BAD_ARG;

    ctx.host = *host;
    ctx.method = *method;
    ctx.p = method->aml;
    ctx.end = method->aml + method->aml_len;
    ctx.returned = 0;
    ctx.return_value = 0;
    ctx.last_error = AML_TINY_OK;

    for (uint32_t i = 0; i < 8u; ++i)
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

    if (!method || !method->aml || !host)
        return AML_TINY_ERR_BAD_ARG;

    ctx.host = *host;
    ctx.method = *method;
    ctx.p = method->aml;
    ctx.end = method->aml + method->aml_len;
    ctx.returned = 0;
    ctx.return_value = 0;
    ctx.last_error = AML_TINY_OK;

    for (uint32_t i = 0; i < 8u; ++i)
        ctx.locals[i] = 0u;

    while (ctx.p < ctx.end && !ctx.returned)
    {
        rc = aml_exec_one_term(&ctx);
        if (rc != AML_TINY_OK)
            return rc;
    }

    return AML_TINY_OK;
}