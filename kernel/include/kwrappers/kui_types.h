#ifndef KUI_TYPES_H
#define KUI_TYPES_H

#include <stdint.h>

/* kcolor lives here (single source of truth) */
typedef struct
{
    uint8_t r, g, b;
} kcolor;

/* forward declaration ONLY; no typedef here */
struct kfont;

/* shared text alignment enum */
typedef enum
{
    KTEXT_ALIGN_LEFT = 0,
    KTEXT_ALIGN_CENTER = 1,
    KTEXT_ALIGN_RIGHT = 2
} ktext_align;

#endif /* KUI_TYPES_H */
