#pragma once
/* Use the upstream Release policy. Cryptographic verification and input
 * validation still return errors; these are internal developer assertions. */
#if !defined(NDEBUG)
#error "Compile the native TLS library with its Release assertion policy"
#endif
#define assert(condition) ((void)0)
#if !defined(__cplusplus)
#define static_assert _Static_assert
#endif
