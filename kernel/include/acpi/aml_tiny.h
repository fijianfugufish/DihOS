#ifndef AML_TINY_H
#define AML_TINY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
      Tiny AML executor.

      This is NOT a full AML interpreter.
      It is intended for very small ACPI control methods such as _PS0/_PS3,
      where the opcode set is simple and known.

      Supported object/value model:
      - Integer values only
      - Named integer objects via callbacks
      - No heap allocation
      - No package/buffer/string objects yet
    */

#define AML_TINY_OK 0
#define AML_TINY_ERR_BAD_ARG -1
#define AML_TINY_ERR_EOF -2
#define AML_TINY_ERR_UNSUPPORTED -3
#define AML_TINY_ERR_PARSE -4
#define AML_TINY_ERR_NAMESPACE -5
#define AML_TINY_ERR_INTERNAL -6

#define AML_TINY_MAX_NAMESEG 4
#define AML_TINY_MAX_NAMESTRING 64
#define AML_TINY_MAX_IF_DEPTH 8

    typedef struct aml_tiny_ctx aml_tiny_ctx;

    typedef int (*aml_tiny_read_named_int_fn)(
        void *user,
        const char *path,
        uint64_t *out_value);

    typedef int (*aml_tiny_write_named_int_fn)(
        void *user,
        const char *path,
        uint64_t value);

    typedef void (*aml_tiny_log_fn)(
        void *user,
        const char *msg);

    typedef struct
    {
        aml_tiny_read_named_int_fn read_named_int;
        aml_tiny_write_named_int_fn write_named_int;
        aml_tiny_log_fn log;
        void *user;
    } aml_tiny_host;

    typedef struct
    {
        const uint8_t *aml;
        uint32_t aml_len;
        const char *scope_prefix; /* example: "\\_SB.I2C1.TPD0" */

        uint32_t arg_count;
        uint64_t args[7];
    } aml_tiny_method;

    typedef struct
    {
        uint8_t type;    /* 0 = integer, 1 = name ref, 2 = local ref, 3 = arg ref */
        uint64_t ivalue; /* integer value OR local/arg index */
        char name[AML_TINY_MAX_NAMESTRING];
    } aml_tiny_value;

    struct aml_tiny_ctx
    {
        aml_tiny_host host;
        aml_tiny_method method;

        const uint8_t *p;
        const uint8_t *end;

        uint8_t returned;
        uint64_t return_value;

        int last_error;

        uint64_t locals[8];
    };

    /*
      Execute AML bytes which should be the *body* of a control method,
      not the surrounding MethodOp object.

      Example:
        aml_tiny_method m = {
            .aml = body_ptr,
            .aml_len = body_len,
            .scope_prefix = "\\_SB.I2C1.TPD0"
        };
        aml_tiny_exec(&m, &host, &ret);
    */
    int aml_tiny_exec(
        const aml_tiny_method *method,
        const aml_tiny_host *host,
        uint64_t *out_return_value);

    /*
      Utility for tracing names in a method body without executing writes.
      Good for first-pass probing.
    */
    int aml_tiny_trace_names(
        const aml_tiny_method *method,
        const aml_tiny_host *host);

#ifdef __cplusplus
}
#endif

#endif