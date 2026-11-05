#pragma once
#include "memory/aarch64_user_vm.h"
#include "../../../sdk/sacx/include/sacx_browser_session.h"
#ifdef __cplusplus
extern "C" {
#endif
int dihos_browser_open(uint64_t frontend,uint32_t width,uint32_t height,uint32_t *session);
int dihos_browser_bind(uint64_t frontend,uint32_t session,uint64_t renderer);
int dihos_browser_send(uint64_t frontend,const sacx_browser_send*);
int dihos_browser_status(uint64_t frontend,uint32_t session,sacx_browser_info*);
int dihos_browser_close(uint64_t frontend,uint32_t session);
uint64_t dihos_browser_renderer(uint64_t frontend,uint32_t session);
int dihos_browser_set_state(uint64_t frontend,uint32_t session,uint32_t state,const char *message);
int dihos_browser_native_call(const aarch64_user_vm*,uint64_t renderer,uint32_t operation,uint64_t buffer,uint32_t bytes);
#ifdef __cplusplus
}
#endif
