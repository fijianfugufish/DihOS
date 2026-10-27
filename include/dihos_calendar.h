#pragma once
#include <stdint.h>
/* UEFI timezone convention: UTC = local time + TimeZone minutes.
 * An unspecified RTC timezone follows the conventional UTC RTC policy. */
static inline int dihos_calendar_utc(uint32_t year,uint32_t month,uint32_t day,
    uint32_t hour,uint32_t minute,uint32_t second,int32_t timezone,uint64_t *out)
{
    static const uint8_t lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (!out || year<1970 || year>9999 || !month || month>12 ||
        hour>23 || minute>59 || second>59 ||
        (timezone!=2047 && (timezone < -1440 || timezone>1440))) return -1;
    uint32_t leap=year%4==0 && (year%100!=0 || year%400==0);
    if (!day || day>lengths[month-1]+(month==2 ? leap:0)) return -1;
    uint64_t days=0;
    for (uint32_t y=1970;y<year;++y)
        days+=365+(y%4==0 && (y%100!=0 || y%400==0));
    for (uint32_t m=1;m<month;++m) days+=lengths[m-1]+(m==2 ? leap:0);
    int64_t seconds=(int64_t)((days+day-1)*86400+hour*3600+minute*60+second);
    if (timezone!=2047) seconds+=(int64_t)timezone*60;
    if (seconds<0) return -1;
    *out=(uint64_t)seconds;return 0;
}
