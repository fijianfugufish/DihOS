#pragma once
#include "../../../sdk/sacx/include/sacx_user_network.h"
#include "../../../sdk/sacx/include/sacx_user_http_request.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "memory/aarch64_user_vm.h"
int64_t dihos_user_network_call(const aarch64_user_vm*,uint64_t owner,uint64_t operation,const uint64_t arguments[3]);
void dihos_user_network_cleanup(uint64_t owner);
#ifdef __cplusplus
}
#endif
