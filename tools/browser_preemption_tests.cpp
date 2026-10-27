#include <cassert>
#include <cstdlib>
#include <cstdio>
extern "C" {
#include "system/kfiber.h"
void browser_infinite_fixture(void*);
}
static unsigned checkpoints;
struct test_task { kfiber execution;uint32_t api_depth,preempt_guard_depth,exit_requested,preemptions;uint64_t execution_deadline; };
using sacx_task=test_task;
static test_task task,*G_current_task=&task;
static uint64_t now;
static uint32_t smp_current_logical_id() {return 0;}
static uint64_t dihos_time_monotonic_ms() {return now;}
#include "../kernel/apps/sacx_app_checkpoint.inc"
extern "C" void __sanitizer_cov_trace_pc(void)
{
    if(++checkpoints%1024==0){++now;if(now==6)task.api_depth=0;sacx_api_app_checkpoint();}
}
int main()
{
    kfiber &fiber=task.execution;uint64_t counter=0;void *stack=malloc(65536);assert(stack);
    assert(!kfiber_init(&fiber,stack,65536,browser_infinite_fixture,&counter));
    task.api_depth=1;
    for(unsigned frame=0;frame<100;++frame) {
        task.execution_deadline=now+2;
        uint64_t before=counter;assert(!kfiber_resume(&fiber));assert(counter>before);
        assert(task.preemptions==frame+1 && !task.api_depth); /* a desktop frame runs between slices */
    }
    assert(fiber.state==KFIBER_SUSPENDED);free(stack);
    puts("PASS: instrumented callback with an infinite API-free loop suspends for 100 scheduler frames with production elapsed-time checkpoint and deferred kernel-call guard");
}

