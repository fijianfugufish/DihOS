#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <mbedtls/mbedtls_config.h>
/* No files, host sockets, operating-system RNG, environment or wall clock.
 * All dependencies are explicitly supplied by the single network owner. */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_DEBUG_C
#undef MBEDTLS_ENTROPY_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_HAVE_ASM
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_AESCE_C
/* Use upstream's limb division implementation; freestanding ARM64 has no
 * compiler-runtime __udivti3 helper for 128-bit division. */
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO dihos_tls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO dihos_tls_free
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO dihos_tls_snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO dihos_tls_vsnprintf
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO int64_t
#define MBEDTLS_PLATFORM_MS_TIME_TYPE_MACRO int64_t
#define MBEDTLS_PLATFORM_TIME_MACRO dihos_tls_time
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
void *dihos_tls_calloc(size_t,size_t);
void dihos_tls_free(void*);
int dihos_tls_snprintf(char*,size_t,const char*,...);
int dihos_tls_vsnprintf(char*,size_t,const char*,va_list);
int64_t dihos_tls_time(int64_t*);
