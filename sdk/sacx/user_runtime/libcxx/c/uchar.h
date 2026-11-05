#pragma once
#include <stdint.h>
/* DihOS's local conversion-state ABI. Only the type is supplied at this stage;
 * multibyte conversion entry points are not implemented/advertised. */
typedef struct {uint32_t codepoint,state;} mbstate_t;
#ifndef __cplusplus
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif
