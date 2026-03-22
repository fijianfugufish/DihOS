// kernel/ff_time.c
#include <stdint.h>

// FatFs expects a DWORD (32-bit) packed time:
// [31:25]=year from 1980 (0..127), [24:21]=month (1..12),
// [20:16]=day (1..31), [15:11]=hour (0..23), [10:5]=min (0..59), [4:0]=sec/2 (0..29).
uint32_t get_fattime(void)
{
    // TODO: replace with real RTC later.
    unsigned year = 2025 - 1980; // 45
    unsigned month = 1;
    unsigned day = 1;
    unsigned hour = 0;
    unsigned min = 0;
    unsigned sec2 = 0; // seconds / 2

    return (year << 25) | (month << 21) | (day << 16) | (hour << 11) | (min << 5) | (sec2);
}
