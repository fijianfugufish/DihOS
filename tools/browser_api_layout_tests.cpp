#include <cassert>
#include <cstddef>
#include <cstdio>
#include "../sdk/sacx/include/sacx_browser_frontend.h"
extern "C" size_t browser_kernel_offset(void);
extern "C" size_t browser_kernel_bytes(void);
int main(){
 assert(browser_kernel_offset()==offsetof(sacx_browser_frontend_api,browser_session));
 assert(browser_kernel_bytes()==sizeof(sacx_browser_frontend_api));
 sacx_browser_frontend_api api={};api.base.struct_size=sizeof(sacx_api);assert(!sacx_browser_frontend(&api.base));
 api.base.struct_size=sizeof(api);assert(!sacx_browser_frontend(&api.base));
 puts("PASS: append-only browser API layout matches kernel and preserves the pinned base SDK");
}
