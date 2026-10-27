#pragma once
#include <stdint.h>
static inline int32_t browser_scroll_clamp(int64_t value,int extent,int viewport)
{
    int64_t limit=(int64_t)extent-viewport;
    if(limit<0)limit=0;
    if(limit>INT32_MAX)limit=INT32_MAX;
    return value<0?0:value>limit?(int32_t)limit:(int32_t)value;
}
