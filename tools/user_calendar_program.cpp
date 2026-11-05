#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include <time.h>
#include <limits.h>
static void check(bool ok){if(!ok)sacx_user_exit(195,"SACX native calendar fixture failed");}
static uint32_t entered,releaseWorker;
static uint64_t worker(uint64_t){
    time_t epoch=0;tm *value=localtime(&epoch);check(value && value->tm_mday==1);
    __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&releaseWorker,__ATOMIC_ACQUIRE))sacx_user_call(SACX_USER_YIELD,0,0,0);
    check(value->tm_year==70 && value->tm_mon==0 && value->tm_mday==1);return 12;
}
extern "C" int dihos_user_main(const sacx_api *api){
    struct Vector {time_t seconds;int year,month,day,hour,minute,second,weekday,yearday;};
    static const Vector vectors[]={
        {0,1970,1,1,0,0,0,4,0},
        {-1,1969,12,31,23,59,59,3,364},
        {951827696,2000,2,29,12,34,56,2,59},
        {-2203891200,1900,3,1,0,0,0,4,59},
        {13574563200,2400,2,29,0,0,0,2,59},
        {8640000000000ll,275760,9,13,0,0,0,6,256},
        {-8640000000000ll,-271821,4,20,0,0,0,2,109},
#include "user_calendar_vectors.inc"
    };
    for(auto &v:vectors){
        tm value;check(gmtime_r(&v.seconds,&value)==&value);
        check(value.tm_year==v.year-1900 && value.tm_mon==v.month-1 && value.tm_mday==v.day);
        check(value.tm_hour==v.hour && value.tm_min==v.minute && value.tm_sec==v.second);
        check(value.tm_wday==v.weekday && value.tm_yday==v.yearday && !value.tm_isdst);
        check(timegm(&value)==v.seconds);
    }
    tm value={};value.tm_year=124;value.tm_mon=13;value.tm_mday=0;value.tm_min=120;value.tm_sec=-1;
    check(mktime(&value)!=-1 && value.tm_year==125 && value.tm_mon==0 && value.tm_mday==31);
    check(value.tm_hour==1 && value.tm_min==59 && value.tm_sec==59);
    time_t extreme=INT64_MAX;check(!gmtime_r(&extreme,&value));
    extreme=INT64_MIN;check(!gmtime_r(&extreme,&value));
    check(difftime(INT64_MAX,INT64_MAX-1)==1 && difftime(INT64_MIN,INT64_MIN+1)==-1);
    time_t now=0;check(time(&now)>0 && now>0);
    int64_t handle=sacx_user_thread_create(worker,0);check(handle>0);
    while(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE))sacx_user_call(SACX_USER_YIELD,0,0,0);
    time_t other=951827696;tm *local=localtime(&other);check(local && local->tm_mday==29);
    __atomic_store_n(&releaseWorker,1,__ATOMIC_RELEASE);
    uint64_t result=0;check(!sacx_user_thread_join(handle,&result,3000) && result==12);
    api->log("SACX calendar: UTC, negative epochs, Gregorian dates, JS limits, normalization and thread-local storage OK");return 101;
}
