#pragma once
#include "memory/aarch64_user_vm.h"
#include "../../../sdk/sacx/include/sacx_user_syscall.h"
enum { DIHOS_SACX_READY=0,DIHOS_SACX_SLEEP=1,DIHOS_SACX_EXIT=2,DIHOS_SACX_MEMORY=3,DIHOS_SACX_THREAD=4,DIHOS_SACX_IDENTITY=5,DIHOS_SACX_RESOURCES=6,DIHOS_SACX_NETWORK=7,DIHOS_SACX_WINDOW=8,DIHOS_SACX_BROWSER=9 };
typedef struct dihos_user_sacx_reply {
    int64_t result;
    uint32_t action,log_bytes;
    uint64_t argument;
    char log[SACX_USER_BUFFER_MAX+1];
} dihos_user_sacx_reply;
void dihos_user_sacx_service(const aarch64_user_vm *vm,uint64_t grants,uint64_t version,
    uint64_t operation,const uint64_t arguments[3],uint64_t now,dihos_user_sacx_reply *out);
