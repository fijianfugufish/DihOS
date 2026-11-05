#undef weak
#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <wchar.h>
#define CHECK(test) do{if(!(test))sacx_user_exit(202,"C stdio fixture failed: " #test);}while(0)
static uintptr_t errorAddresses[3];
static uint64_t worker(uint64_t index){
    errorAddresses[index]=reinterpret_cast<uintptr_t>(&errno);
    errno=900+index;
    CHECK(!sacx_user_call(SACX_USER_SLEEP_MS,2,0,0));
    CHECK(errno==int(900+index));
    char buffer[32];CHECK(snprintf(buffer,sizeof(buffer),"%u %.2f",unsigned(index),2.5)==6);
    CHECK(!strcmp(buffer+1," 2.50"));
    CHECK(fprintf(stdout,"stdio worker %u\n",unsigned(index))==15);
    return index+7;
}
extern "C" int dihos_user_main(const sacx_api *api){
    struct {char text[8];char guard;} small{};small.guard=0x55;
    CHECK(snprintf(small.text,8,"0123456789")==10 && !strcmp(small.text,"0123456") && small.guard==0x55);
    CHECK(snprintf(nullptr,0,"hello %d",123)==9);
    char buffer[160];CHECK(snprintf(buffer,1,"abc")==3 && !buffer[0]);
    CHECK(snprintf(buffer,sizeof(buffer),"%s %zu %lld","x",size_t(7),-9LL)==6 && !strcmp(buffer,"x 7 -9"));
    CHECK(snprintf(buffer,sizeof(buffer),"%2$d %1$s","one",2)==5 && !strcmp(buffer,"2 one"));
    CHECK(snprintf(buffer,sizeof(buffer),"%.17g",0.1)>0 && !strcmp(buffer,"0.10000000000000001"));
    CHECK(snprintf(buffer,sizeof(buffer),"%a",1.5)>0 && !strcmp(buffer,"0x1.8p+0"));
    CHECK(snprintf(buffer,sizeof(buffer),"%.5Lf",1.25L)==7 && !strcmp(buffer,"1.25000"));
    CHECK(snprintf(buffer,sizeof(buffer),"%lc %ls",L'A',L"BC")==4 && !strcmp(buffer,"A BC"));
    errno=0;CHECK(snprintf(buffer,sizeof(buffer),"%ls",L"\u00e9")==-1 && errno==EILSEQ);
    char *end;errno=0;
    CHECK(strtod(" \t-12.5e2tail",&end)==-1250 && !strcmp(end,"tail"));
    CHECK(strtod("0x1.8p+2!",&end)==6 && *end=='!');
    CHECK(isnan(strtod("nan",&end)) && !*end && isinf(strtod("INF",&end)));
    errno=0;CHECK(isinf(strtod("1e9999",&end)) && errno==ERANGE);
    errno=0;CHECK(strtod("1e-9999",&end)==0 && errno==ERANGE);
    CHECK(strtod("2.2250738585072014e-308",&end)==0x1p-1022);
    CHECK(strtod("4.9406564584124654e-324",&end)==0x1p-1074);
    const char *bad="not a number";CHECK(strtod(bad,&end)==0 && end==bad);
    errno=0;CHECK(strtol("-9223372036854775808",&end,10)==LONG_MIN && !*end && !errno);
    errno=0;CHECK(strtol("9223372036854775808",&end,10)==LONG_MAX && errno==ERANGE);
    errno=0;CHECK(strtoul("0xffffffffffffffffZ",&end,0)==ULONG_MAX && *end=='Z' && !errno);
    CHECK(strtoul("-1",&end,10)==ULONG_MAX && strtol("1012",&end,2)==5 && *end=='2');
    CHECK(atoi(" -23!")==-23);
    int a=0,b=0,count=0;double d=0;char word[4];
    CHECK(sscanf("42 0x10 -3.5 end","%d %i %lf %3s%n",&a,&b,&d,word,&count)==4);
    CHECK(a==42 && b==16 && d==-3.5 && !strcmp(word,"end") && count==16);
    CHECK(!fopen("0:/OS/System/Images/bgpaper.jpg","rb") && errno==EACCES);
    CHECK(setvbuf(stdout,nullptr,_IONBF,0)==0);
    CHECK(setvbuf(stdout,nullptr,_IOFBF,256)==-1 && errno==EINVAL);
    CHECK(fseek(stdout,0,SEEK_SET)==-1 && errno==ESPIPE && ftell(stdout)==-1);
    CHECK(fwrite("C stdio output\n",1,15,stdout)==15 && !ferror(stdout));
    char input[4]={1,2,3,4};CHECK(fread(input,1,4,stdin)==0 && feof(stdin) && !ferror(stdin) && input[0]==1);
    clearerr(stdin);CHECK(!feof(stdin) && !fgets(input,4,stdin) && feof(stdin));
    CHECK(!fclose(stdin) && !fclose(stderr));
    CHECK(fwrite("x",1,1,stderr)==0 && ferror(stderr) && fclose(stderr)==EOF);
    CHECK(fflush(nullptr)==0);
    errno=1111;sacx_user_thread threads[3];
    for(unsigned i=0;i<3;++i){auto handle=sacx_user_thread_create(worker,i);CHECK(handle>0);threads[i]=handle;}
    for(unsigned i=0;i<3;++i){uint64_t result=0;CHECK(!sacx_user_thread_join(threads[i],&result,60000) && result==i+7);}
    CHECK(errno==1111);
    for(unsigned i=0;i<3;++i){CHECK(errorAddresses[i] && errorAddresses[i]!=reinterpret_cast<uintptr_t>(&errno));for(unsigned j=0;j<i;++j)CHECK(errorAddresses[i]!=errorAddresses[j]);}
    api->log("SACX C stdio: formatting, parsing, denied files, stream states, TLS errors and concurrent output OK");
    return 99;
}
