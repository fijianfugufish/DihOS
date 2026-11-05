#include <stddef.h>
#include "apps/sacx_api.h"
size_t browser_kernel_offset(void){return offsetof(sacx_api,browser_session);}
size_t browser_kernel_bytes(void){return sizeof(sacx_api);}
