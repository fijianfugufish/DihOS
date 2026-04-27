#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DIHOS_PATH_CAP 256u

    int dihos_path_raw_to_friendly(const char *raw, char *out, uint32_t cap);
    int dihos_path_canonicalize_friendly(const char *source, char *out, uint32_t cap);
    int dihos_path_resolve_friendly(const char *cwd, const char *input, char *out, uint32_t cap);
    int dihos_path_friendly_to_raw(const char *friendly, char *raw, uint32_t cap);
    int dihos_path_resolve_raw(const char *cwd, const char *input,
                               char *friendly, uint32_t friendly_cap,
                               char *raw, uint32_t raw_cap);
    int dihos_path_join_raw(const char *base, const char *name, char *out, uint32_t cap);
    int dihos_path_join_friendly(const char *base, const char *name, char *out, uint32_t cap);

#ifdef __cplusplus
}
#endif
