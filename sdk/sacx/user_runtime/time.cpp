#include <time.h>
#include <stdint.h>
#include <limits.h>
#ifndef DIHOS_USER_TIME_TEST
#include "sacx_user_syscall.h"
#else
extern "C" int64_t dihos_user_time_test_ms();
#endif
static_assert(sizeof(time_t)==8,"native calendar requires signed 64-bit seconds");
namespace {
thread_local tm calendar;
constexpr int64_t minimumYear=(int64_t)INT_MIN+1900,maximumYear=(int64_t)INT_MAX+1900;
int64_t floorDivision(int64_t value,int64_t divisor){int64_t q=value/divisor;return q-(value%divisor<0);}
bool leap(int64_t year){return year%4==0 && (year%100!=0 || year%400==0);}
int64_t yearStart(int64_t year){
    // Count Gregorian leap days before each year, relative to 1970-01-01.
    int64_t last=year-1;
    return 365*(year-1970)+floorDivision(last,4)-floorDivision(last,100)+floorDivision(last,400)-477;
}
unsigned monthLength(unsigned month,bool isLeap){
    static constexpr unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return lengths[month]+(month==1 && isLeap);
}
}
extern "C" {
tm *gmtime_r(const time_t *input,tm *out){
    if(!input || !out)return nullptr;
    int64_t days=floorDivision(*input,86400),seconds=*input%86400;
    if(seconds<0)seconds+=86400;
    if(days<yearStart(minimumYear) || days>=yearStart(maximumYear+1))return nullptr;
    int64_t low=minimumYear,high=maximumYear+1;
    while(high-low>1){int64_t mid=low+(high-low)/2;if(yearStart(mid)<=days)low=mid;else high=mid;}
    int64_t within=days-yearStart(low);unsigned month=0,day=(unsigned)within;
    while(day>=monthLength(month,leap(low))){day-=monthLength(month,leap(low));++month;}
    int weekday=(int)((days+4)%7);if(weekday<0)weekday+=7;
    tm result={};result.tm_year=(int)(low-1900);result.tm_mon=month;result.tm_mday=day+1;
    result.tm_yday=(int)within;result.tm_wday=weekday;result.tm_hour=seconds/3600;
    result.tm_min=seconds/60%60;result.tm_sec=seconds%60;result.tm_isdst=0;
    *out=result;return out;
}
tm *localtime_r(const time_t *input,tm *out){return gmtime_r(input,out);}
tm *gmtime(const time_t *input){return gmtime_r(input,&calendar);}
tm *localtime(const time_t *input){return localtime_r(input,&calendar);}
time_t mktime(tm *value){
    if(!value)return -1;
    int64_t year=(int64_t)value->tm_year+1900+floorDivision(value->tm_mon,12);
    int64_t month=(int64_t)value->tm_mon-floorDivision(value->tm_mon,12)*12;
    if(year<minimumYear || year>maximumYear)return -1;
    int64_t days=yearStart(year)+value->tm_mday-1;
    for(unsigned i=0;i<(unsigned)month;++i)days+=monthLength(i,leap(year));
    // tm fields are signed 32-bit. Their normalized result fits int64 seconds
    // even at the largest representable tm_year; gmtime_r checks the final year.
    time_t result=days*86400+(int64_t)value->tm_hour*3600+(int64_t)value->tm_min*60+value->tm_sec;
    tm normalized;if(!gmtime_r(&result,&normalized))return -1;
    *value=normalized;return result;
}
time_t timegm(tm *value){return mktime(value);}
double difftime(time_t end,time_t begin){
    return end>=begin?(double)((uint64_t)end-(uint64_t)begin):-(double)((uint64_t)begin-(uint64_t)end);
}
time_t time(time_t *out){
#ifdef DIHOS_USER_TIME_TEST
    int64_t ms=dihos_user_time_test_ms();
#else
    int64_t ms=sacx_user_call(SACX_USER_UTC_MS,0,0,0);
#endif
    time_t result=ms<0?-1:ms/1000;if(out)*out=result;return result;
}
}
