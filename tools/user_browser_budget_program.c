#include "sacx_user_api.h"
#include "sacx_user_resources.h"
#include "sacx_user_network.h"
#include "sacx_user_window.h"
__attribute__((noreturn)) void _start(void){
    sacx_user_resources r;sacx_user_info info;
    if(sacx_user_resources_get(&r) || sacx_user_call(SACX_USER_QUERY,(uintptr_t)&info,sizeof(info),0))
        sacx_user_exit(101,"budget probe failed");
    int browser=r.heap_limit_bytes>384ull*1024*1024;
    if(browser && (r.heap_limit_bytes<700ull*1024*1024 || r.heap_limit_bytes>768ull*1024*1024))
        sacx_user_exit(103,"trusted browser budget outside its 768 MiB cap");
    if(!browser && (r.heap_limit_bytes<240ull*1024*1024 || r.heap_limit_bytes>256ull*1024*1024))
        sacx_user_exit(104,"ordinary app budget changed");
    if(browser && (info.capabilities&(SACX_USER_CAP_NETWORK|SACX_USER_CAP_SURFACE))!=(SACX_USER_CAP_NETWORK|SACX_USER_CAP_SURFACE))
        sacx_user_exit(102,"browser launch grants changed");
    sacx_user_exit(browser?87:88,browser?"Trusted browser budget OK":"Ordinary native budget OK");
}
