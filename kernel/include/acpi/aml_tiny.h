#ifndef AML_TINY_H
#define AML_TINY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

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
#define AML_TINY_MAX_BUFFER_BYTES 64
#define AML_TINY_MAX_PACKAGE_ELEMS 8
#define AML_TINY_MAX_WHILE_ITERS 16

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
        const char *scope_prefix;

        uint32_t arg_count;
        uint64_t args[7];
    } aml_tiny_method;

    typedef struct
    {
        /*
          0 = integer
          1 = name ref
          2 = local ref
          3 = arg ref
          4 = buffer
          5 = package
        */
        uint8_t type;
        uint64_t ivalue;
        char name[AML_TINY_MAX_NAMESTRING];

        uint32_t buf_len;
        uint8_t buf[AML_TINY_MAX_BUFFER_BYTES];

        uint32_t pkg_count;
        uint64_t pkg_elems[AML_TINY_MAX_PACKAGE_ELEMS];
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

    int aml_tiny_exec(
        const aml_tiny_method *method,
        const aml_tiny_host *host,
        uint64_t *out_return_value);

    int aml_tiny_trace_names(
        const aml_tiny_method *method,
        const aml_tiny_host *host);

#ifdef __cplusplus
}
#endif

#endif