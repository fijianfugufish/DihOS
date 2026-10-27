#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
static std::string captured;
static void terminal_print_inline(const char *s){captured+=s;}
static void terminal_print(const char *s){captured+=s;captured+='\n';}
static void terminal_print_inline_hex64(uint64_t v){char out[32];snprintf(out,sizeof out,"%016llx",(unsigned long long)v);captured+=out;}
enum{SACX_MAX_TASKS=2,SACX_MAX_TASK_NET_REQUESTS=2,SACX_TASK_UNUSED=0};
struct request{char url[256];uint32_t status;};
struct sacx_task{uint32_t state,task_id;uint8_t *image_base;uint32_t image_size;void *execution_stack;const char *active_api,*last_api;uint32_t api_depth;request net_requests[2];};
static sacx_task G_tasks[2];static request *G_net_active_slot;
#include "../kernel/apps/sacx_panic_report.inc"
int main(){
 auto &task=G_tasks[0];task.state=1;task.task_id=7;task.image_base=(uint8_t*)0x100000;task.image_size=0x1000;task.execution_stack=(void*)0x200000;
 task.active_api="img_load_memory";task.last_api="net_response_progress";task.api_depth=1;
 G_net_active_slot=&task.net_requests[1];snprintf(G_net_active_slot->url,sizeof G_net_active_slot->url,"https://fixture.test/resource");G_net_active_slot->status=2;
 sacx_runtime_panic_report(0x100040,0x100080,0x23ff00);
 assert(captured.find("sacx: PC RVA=0000000000000040")!=std::string::npos);
 assert(captured.find("sacx: LR RVA=0000000000000080")!=std::string::npos);
 assert(captured.find("sacx: stack bytes remaining=000000000003ff00")!=std::string::npos);
 assert(captured.find("active API=img_load_memory")!=std::string::npos);
 assert(captured.find("active network URL=https://fixture.test/resource")!=std::string::npos);
 captured.clear();task.active_api=task.last_api=nullptr;G_net_active_slot=(request*)1;
 sacx_runtime_panic_report(0x1000,0x1000,0x1000);
 assert(captured.find("PC RVA=")==std::string::npos&&captured.find("LR RVA=")==std::string::npos);
 assert(captured.find("stack bytes remaining=")==std::string::npos);
 assert(captured.find("active API=none")!=std::string::npos&&captured.find("active network URL=")==std::string::npos);
 puts("PASS: panic context resolves app addresses, stack bounds, active API and owned network URL without walking interrupted memory");
}
