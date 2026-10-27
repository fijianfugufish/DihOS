#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include "sacx_api.h"
struct timeval {time_t tv_sec;long tv_usec;};
static uint64_t now=12345678,utc=1709164800;
static uint64_t elapsed(){return now;}
static int wall(uint64_t *value){*value=utc;return 0;}
static sacx_api api;
static const sacx_api *dihscover_netsurf_api(){return &api;}
#define gettimeofday browser_gettimeofday
#define time browser_time
#define clock browser_clock
#define gmtime browser_gmtime
#define localtime browser_localtime
#define mktime browser_mktime
#define strftime browser_strftime
#define ctime browser_ctime
#include "../sdk/sacx/apps/dihscover/browser_clock.inc"
int main(){
 api.struct_size=sizeof(api);api.time_monotonic_ms=elapsed;api.time_utc=wall;
 timeval v;assert(!gettimeofday(&v,nullptr)&&v.tv_sec==(time_t)utc&&v.tv_usec==678000);assert(clock()==(clock_t)now);
 time_t stamp=1709164800;tm date=*gmtime(&stamp);assert(date.tm_year==124&&date.tm_mon==1&&date.tm_mday==29&&date.tm_wday==4&&date.tm_yday==59);
 char text[80];assert(strftime(text,sizeof text,"%a, %d %b %Y %H:%M:%S GMT",&date));assert(!strcmp(text,"Thu, 29 Feb 2024 00:00:00 GMT"));assert(mktime(&date)==stamp);
 for(int year=1900;year<=2200;++year)for(int month=0;month<12;++month){tm x={};x.tm_year=year-1900;x.tm_mon=month;x.tm_mday=1;time_t t=mktime(&x);tm round=*gmtime(&t);assert(round.tm_year==year-1900&&round.tm_mon==month&&round.tm_mday==1);}
 assert(!strftime(text,4,"%Y-%m",&date));assert(!strftime(text,sizeof text,"%Q",&date));
 puts("PASS: actual millisecond clock, firmware UTC, leap day, weekday, HTTP dates, 301-year calendar round trips and bounded formatting");
}
