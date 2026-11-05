#pragma once
#include "sacx_user_api.h"
#include "sacx_user_threads.h"
// Process-local userspace mutex. The kernel only checks/waits on its word.
struct dihos_user_lock {
    uint32_t *word;
    explicit dihos_user_lock(uint32_t &value):word(&value){
        uint32_t expected=0;
        if(__atomic_compare_exchange_n(word,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
        while(__atomic_exchange_n(word,2,__ATOMIC_ACQUIRE)){
            int rc=sacx_user_wait32(word,2,60000);
            if(rc && rc!=-11 && rc!=-110)sacx_user_exit(152,"C++ runtime lock wait failed");
        }
    }
    ~dihos_user_lock(){
        if(__atomic_exchange_n(word,0,__ATOMIC_RELEASE)==2 && sacx_user_wake32(word,4)<0)
            sacx_user_exit(152,"C++ runtime lock wake failed");
    }
    dihos_user_lock(const dihos_user_lock&)=delete;
};
