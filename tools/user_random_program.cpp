#include "sacx_user_random.h"
#include "sacx_user_api.h"
#ifndef USER_RANDOM_UNREADY
#define USER_RANDOM_UNREADY 0
#endif
static unsigned char first[1024],second[1024];
static void check(bool value){if(!value)sacx_user_exit(186,"SACX random fixture failed");}
extern "C" int dihos_user_main(const sacx_api *api){
    for(unsigned i=0;i<sizeof(first);++i)first[i]=0xa5;
    if(USER_RANDOM_UNREADY){check(sacx_user_random(first,sizeof(first))==-11);for(auto b:first)check(b==0xa5);return 92;}
    check(!sacx_user_random(first,sizeof(first)) && !sacx_user_random(second,sizeof(second)));
    bool different=false;for(unsigned i=0;i<sizeof(first);++i)different|=first[i]!=second[i];check(different);
    void *page=nullptr;check(!sacx_user_mem_alloc(4096,&page));
    ((unsigned char*)page)[0]=0xa5;
    check(!sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)page,4096,1));
    check(sacx_user_random(page,32)==-14 && ((unsigned char*)page)[0]==0xa5);
    check(sacx_user_call(SACX_USER_RANDOM,(uintptr_t)page,257,0)==-7);
    check(!sacx_user_mem_free(page));
    check(sacx_user_random((void*)UINTPTR_MAX,2)==-22 && !sacx_user_random(nullptr,0));
    api->log("SACX random: bounded chunks, owned buffers and protected destination rejection OK");return 93;
}
