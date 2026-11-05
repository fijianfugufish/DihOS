#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t sacx_user_key;
// Process-local keys. Positive errno results; generation zero is invalid.
int sacx_user_key_create(sacx_user_key*,void (*destructor)(void*));
int sacx_user_key_delete(sacx_user_key);
int sacx_user_key_set(sacx_user_key,void*);
void *sacx_user_key_get(sacx_user_key);
#ifdef __cplusplus
}
#endif
