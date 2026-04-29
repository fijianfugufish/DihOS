#include "system/dihos_time.h"

extern volatile uint64_t g_dihos_tick;

static uint8_t dihos_is_leap_year(uint32_t year)
{
    if ((year % 400u) == 0u)
        return 1u;
    if ((year % 100u) == 0u)
        return 0u;
    return (year % 4u) == 0u ? 1u : 0u;
}

static uint8_t dihos_days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t k_days[12] = {
        31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

    if (month < 1u || month > 12u)
        return 31u;
    if (month == 2u && dihos_is_leap_year(year))
        return 29u;
    return k_days[month - 1u];
}

uint64_t dihos_time_ticks(void)
{
    return g_dihos_tick;
}

uint64_t dihos_time_seconds(void)
{
    return dihos_time_ticks() / (uint64_t)DIHOS_TIME_TICKS_PER_SECOND;
}

uint32_t dihos_time_fattime(void)
{
    uint64_t seconds = dihos_time_seconds();
    uint64_t days = seconds / 86400u;
    uint32_t day_seconds = (uint32_t)(seconds % 86400u);
    uint32_t year = 2026u;
    uint32_t month = 1u;
    uint32_t day = 1u;
    uint32_t hour = day_seconds / 3600u;
    uint32_t minute = (day_seconds % 3600u) / 60u;
    uint32_t second = day_seconds % 60u;

    while (days > 0u)
    {
        uint32_t dim = dihos_days_in_month(year, month);
        if (days < dim)
            break;
        days -= dim;
        ++month;
        if (month > 12u)
        {
            month = 1u;
            ++year;
            if (year > 2107u)
                year = 2107u;
        }
    }

    day = 1u + (uint32_t)days;
    if (day > 31u)
        day = 31u;

    if (year < 1980u)
        year = 1980u;
    if (year > 2107u)
        year = 2107u;

    return ((year - 1980u) << 25) |
           ((month & 0x0Fu) << 21) |
           ((day & 0x1Fu) << 16) |
           ((hour & 0x1Fu) << 11) |
           ((minute & 0x3Fu) << 5) |
           ((second / 2u) & 0x1Fu);
}
