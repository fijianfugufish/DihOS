// Exercise actual target libm calls, including exceptional values and FP state.
#include "sacx_user_api.h"
#include <math.h>
#include <fenv.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <locale.h>
#define CHECK(test) do { if(!(test)) sacx_user_exit(196,"SACX math fixture failed: " #test); } while(0)
#if DIHOS_C_SUPPORT
extern "C" char **environ;
static unsigned exit_state;
static void early_exit(){CHECK(exit_state==1);exit_state=2;sacx_user_log("C support: string, sorting, environment, locale and exit callbacks OK");}
static void late_exit(){CHECK(exit_state==0);exit_state=1;}
static int compare_int(const void *a,const void *b){int x=*(const int*)a,y=*(const int*)b;return (x>y)-(x<y);}
#endif
extern "C" int dihos_user_main(const sacx_api *api){
    CHECK(sin(0)==0 && cos(0)==1 && exp(0)==1 && log(1)==0);
    CHECK(pow(2,10)==1024 && sqrt(4)==2 && cbrt(8)==2);
    CHECK(fabs(sin(0x1.0c152382d7366p-1)-0.5)<0x1p-51);
    CHECK(sqrt(2)>1.4142135623730949 && sqrt(2)<1.4142135623730954);
    CHECK(isfinite(sin(1e300)) && fabs(sin(1e300))<=1);
    CHECK(isnan(sqrt(-1)) && isnan(pow(-2,0.5)));
    CHECK(isinf(log(0)) && signbit(log(0)));
    CHECK(signbit(fmod(-0.0,3)) && !signbit(fabs(-0.0)));
    CHECK(fmin(NAN,3)==3 && fmax(3,NAN)==3);
    CHECK(fabs(log1p(1e-20)-1e-20)<1e-35 && fabs(expm1(1e-20)-1e-20)<1e-35);
    CHECK(nextafter(0,1)==0x1p-1074 && scalbn(1,-1074)==0x1p-1074);
    CHECK(!isnormal(nextafter(0,1)) && isfinite(nextafter(INFINITY,0)));
    CHECK(fma(0x1.0000002p0,0x1.ffffffcp-1,-1)==-0x1p-54);
    CHECK(round(2.5)==3 && round(-2.5)==-3 && trunc(-2.75)==-2);
    CHECK(fabsf(sinf(0.5f)-0.47942555f)<0x1p-23f);
    CHECK(!fesetround(FE_DOWNWARD) && rint(1.75)==1);
    CHECK(!fesetround(FE_UPWARD) && rint(1.25)==2);
    CHECK(!fesetround(FE_TONEAREST));
    CHECK(!feclearexcept(FE_ALL_EXCEPT));
#if DIHOS_C_SUPPORT
    char bytes[32];memset(bytes,0x55,sizeof(bytes));
    CHECK(strcpy(bytes,"hello")==bytes && !strcmp(bytes,"hello"));
    CHECK(strcat(bytes," world")==bytes && !strcmp(bytes,"hello world"));
    CHECK(strchr(bytes,'w')==bytes+6 && strrchr(bytes,'l')==bytes+9 && strchr(bytes,0)==bytes+11);
    CHECK(strstr(bytes,"lo wo")==bytes+3 && !strstr(bytes,"missing") && strstr(bytes,"")==bytes);
    CHECK(!strncmp(bytes,"hello!",5) && strncmp("a","b",1)<0 && !strncmp("a","b",0));
    CHECK(!strncasecmp("ABcd","abCD",4) && strncasecmp("abc","abd",3)<0);
    CHECK(strncpy(bytes,"x",4)==bytes && bytes[0]=='x' && !bytes[1] && !bytes[2] && !bytes[3]);
    bytes[4]=0x55;CHECK(strncat(bytes,"yzzz",1)==bytes && !strcmp(bytes,"xy") && bytes[4]==0x55);
    CHECK(abs(-17)==17 && labs(-23)==23 && div(-17,5).quot==-3 && div(-17,5).rem==-2);
    int values[512];for(unsigned i=0;i<512;++i)values[i]=(i*137)%97;
    qsort(values,512,sizeof(int),compare_int);
    for(unsigned i=1;i<512;++i)CHECK(values[i-1]<=values[i]);
    CHECK(values[0]==0 && values[511]==96);
    CHECK(!getenv("HOME"));char **saved=environ;
    char setting[]="DIHOS_TEST=owned";char *temporary[]={setting,nullptr};environ=temporary;
    CHECK(getenv("DIHOS_TEST") && !strcmp(getenv("DIHOS_TEST"),"owned") && !getenv("DIHOS"));
    environ=saved;
    // Native SACX exposes LC_ALL=0; it intentionally has a fixed C locale.
    CHECK(!strcmp(setlocale(0,nullptr),"C") && !strcmp(setlocale(0,"POSIX"),"C") && !setlocale(0,"unsupported"));
    CHECK(!atexit(early_exit) && !atexit(late_exit));
#endif
    volatile double invalid=sqrt(-1);(void)invalid;
    CHECK(fetestexcept(FE_INVALID));
    CHECK(!feclearexcept(FE_ALL_EXCEPT));
    api->log("SACX math: native arithmetic, special values, rounding and FP exceptions OK");
    return 96;
}
