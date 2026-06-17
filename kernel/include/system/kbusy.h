#pragma once

#include <stdint.h>
#include "kwrappers/kwindow.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void kbusy_begin(void);
    void kbusy_begin_region(int32_t x, int32_t y, uint32_t w, uint32_t h);
    void kbusy_begin_window(kwindow_handle window);
    void kbusy_pump(void);
    void kbusy_end(void);
    int kbusy_active(void);

#ifdef __cplusplus
}
#endif
