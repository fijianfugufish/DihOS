/* UTC calendar operations and an elapsed clock; the UI tick counter is 60 Hz,
 * not milliseconds. Browser timing must keep progressing while callbacks pause. */
static uint64_t browser_clock_ms(void) {
    const sacx_api *api=dihscover_netsurf_api();
    return api?(SACX_API_HAS(api,time_monotonic_ms)?api->time_monotonic_ms():api->time_ticks()*1000/60):0;
}
extern "C" int gettimeofday(timeval *value,void *zone) {
    (void)zone;if(!value)return -1;const sacx_api *api=dihscover_netsurf_api();uint64_t seconds=browser_clock_ms()/1000;
    if(api&&SACX_API_HAS(api,time_utc))(void)api->time_utc(&seconds);
    value->tv_sec=(time_t)seconds;value->tv_usec=(long)(browser_clock_ms()%1000)*1000;return 0;
}
extern "C" time_t time(time_t *out) {timeval value;gettimeofday(&value,0);if(out)*out=value.tv_sec;return value.tv_sec;}
extern "C" clock_t clock(void) {return (clock_t)browser_clock_ms();}
extern "C" tm *gmtime(const time_t *value) {
    static tm result;if(!value)return 0;int64_t seconds=*value,days=seconds/86400,remainder=seconds%86400;
    if(remainder<0){remainder+=86400;--days;}
    int64_t z=days+719468,era=(z>=0?z:z-146096)/146097;
    unsigned doe=(unsigned)(z-era*146097),yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int year=(int)(yoe+era*400);unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    unsigned day=doy-(153*mp+2)/5+1,month=mp+(mp<10?3:-9);year+=month<=2;
    result={};result.tm_year=year-1900;result.tm_mon=month-1;result.tm_mday=day;
    result.tm_hour=(int)(remainder/3600);result.tm_min=(int)(remainder/60%60);result.tm_sec=(int)(remainder%60);
    result.tm_wday=(int)((days+4)%7);if(result.tm_wday<0)result.tm_wday+=7;
    static const unsigned before[]={0,31,59,90,120,151,181,212,243,273,304,334};
    result.tm_yday=before[month-1]+day-1+(month>2&&year%4==0&&(year%100!=0||year%400==0));return &result;
}
extern "C" tm *localtime(const time_t *value) {return gmtime(value);} /* DihOS uses UTC */
extern "C" time_t mktime(tm *value) {
    if(!value||value->tm_year<-1899||value->tm_year>8099)return (time_t)-1;
    int64_t year=value->tm_year+1900,month=value->tm_mon;
    year+=month/12;month%=12;if(month<0){month+=12;--year;}++month;year-=month<=2;
    int64_t era=(year>=0?year:year-399)/400,yoe=year-era*400;
    int64_t doy=(153*(month+(month>2?-3:9))+2)/5+value->tm_mday-1;
    int64_t days=era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
    time_t seconds=(time_t)(days*86400+value->tm_hour*3600+value->tm_min*60+value->tm_sec);
    *value=*gmtime(&seconds);return seconds;
}
extern "C" size_t strftime(char *dst,size_t capacity,const char *format,const tm *value) {
    if(!dst||!capacity||!format||!value)return 0;size_t used=0;bool valid=true;
    auto append=[&](const char *text){while(*text){if(used+1>=capacity){valid=false;return;}dst[used++]=*text++;}};
    auto number=[&](int64_t n,unsigned width){char digits[32],text[40];unsigned count=0,at=0;bool negative=n<0;uint64_t v=negative?0-(uint64_t)n:(uint64_t)n;do{digits[count++]=(char)('0'+v%10);v/=10;}while(v);if(negative)text[at++]='-';while(at+count<width)text[at++]='0';while(count)text[at++]=digits[--count];text[at]=0;append(text);};
    static const char *week[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},*months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    for(const char *p=format;*p&&valid;++p){if(*p!='%'){char c[]={*p,0};append(c);continue;}switch(*++p){
        case '%':append("%");break;case 'a':if(value->tm_wday>=0&&value->tm_wday<7)append(week[value->tm_wday]);else valid=false;break;
        case 'b':if(value->tm_mon>=0&&value->tm_mon<12)append(months[value->tm_mon]);else valid=false;break;
        case 'Y':number(value->tm_year+1900,4);break;case 'm':number(value->tm_mon+1,2);break;case 'd':number(value->tm_mday,2);break;
        case 'H':number(value->tm_hour,2);break;case 'M':number(value->tm_min,2);break;case 'S':number(value->tm_sec,2);break;
        case 's':{tm copy=*value;number(mktime(&copy),1);break;}
        case 'Z':append("GMT");break;case 'z':append("+0000");break;default:valid=false;break;
    }}
    dst[used<capacity?used:capacity-1]=0;return valid?used:0;
}
extern "C" char *ctime(const time_t *value) {static char text[32];tm *v=gmtime(value);if(!v)return 0;if(!strftime(text,sizeof text,"%a %b %d %H:%M:%S %Y",v))return 0;size_t n=strlen(text);text[n++]='\n';text[n]=0;return text;}
