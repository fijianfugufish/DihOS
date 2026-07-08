#pragma once
#include "kwrappers/ktext.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void kearly_console_begin(const kfont *font);
    void kearly_console_end(void);
    void kearly_console_write(const char *text);

#ifdef __cplusplus
}
#endif
