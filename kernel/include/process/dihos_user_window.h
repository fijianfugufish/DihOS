#pragma once
#include "memory/aarch64_user_vm.h"
#include "../../../sdk/sacx/include/sacx_user_window.h"
#ifdef __cplusplus
extern "C" {
#endif
// Core-0 only. Backend cannot retain any EL0 address; pixels are broker-owned.
typedef struct dihos_user_window_backend {
 int (*create)(uint64_t,const sacx_user_window_desc*,const uint32_t*);
 void (*present)(uint64_t,const uint32_t*);
 void (*destroy)(uint64_t);
} dihos_user_window_backend;
int dihos_user_window_set_backend(const dihos_user_window_backend*);
int64_t dihos_user_window_call(const aarch64_user_vm*,uint64_t,uint64_t,const uint64_t[3]);
void dihos_user_window_cleanup(uint64_t);
int dihos_user_window_push(uint64_t,const sacx_user_window_event*);
uint64_t dihos_user_window_owner(uint64_t);
#ifdef __cplusplus
}
#endif
