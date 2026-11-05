#include "sacx_user_api.h"
#include <stddef.h>
extern "C" void *memmove(void*,const void*,size_t);
extern "C" void *memset(void*,int,size_t);
extern "C" int memcmp(const void*,const void*,size_t);
static volatile int constructed,completed,destroyed;
static volatile int *volatile initialized_pointer=&constructed;
static int add(int value){return value+25;}
static int (*volatile initialized_function)(int)=add;
struct Lifetime {
    Lifetime(){constructed=42;}
    ~Lifetime(){
        if(!completed || destroyed)sacx_user_exit(132,"C++ destructor ordering failed");
        destroyed=1;sacx_user_log("SACX C++: startup, relocations, heap and destructors OK");
    }
};
static Lifetime lifetime;
struct Base {virtual int value() const=0;virtual ~Base(){};};
struct Widget final:Base {int number;explicit Widget(int n):number(n){}int value()const override{return number+42;}};
__attribute__((noinline)) static int dispatch(Base *object){return object->value();}
extern "C" int dihos_user_main(const sacx_api *api){
    if(constructed!=42 || initialized_pointer!=&constructed || *initialized_pointer!=42 || initialized_function(17)!=42)
        sacx_user_exit(130,"C++ initialization/relative pointers failed");
    Base *object=new Widget(17);
    if(dispatch(object)!=59)sacx_user_exit(131,"C++ virtual dispatch failed");
    delete object;
    char buffer[8];memset(buffer,0,8);buffer[0]='a';buffer[1]='b';buffer[2]='c';
    memmove(buffer+1,buffer,3);
    if(memcmp(buffer,"aabc",4))sacx_user_exit(133,"C runtime overlap failed");
    if(api->app_yield())sacx_user_exit(134,"C++ yield failed");
    completed=1;return 75;
}
