/* Panic-only diagnostics: fixed-size task storage and constant API names.
 * No app callback, allocation, NIC polling or stack-memory walk is allowed. */
static void sacx_panic_value(const char *name,uint64_t value)
{
    terminal_print_inline(name);terminal_print_inline_hex64(value);terminal_print("");
}
extern "C" void sacx_runtime_panic_report(uint64_t pc,uint64_t lr,uint64_t sp)
{
    terminal_print("sacx: panic execution context");
    for(uint32_t i=0;i<SACX_MAX_TASKS;++i) {
        const sacx_task *task=&G_tasks[i];
        if(task->state==SACX_TASK_UNUSED)continue;
        sacx_panic_value("sacx: task=",task->task_id);
        sacx_panic_value("sacx: image base=",(uintptr_t)task->image_base);
        sacx_panic_value("sacx: image size=",task->image_size);
        uintptr_t base=(uintptr_t)task->image_base;
        if(pc>=base && pc-base<task->image_size)sacx_panic_value("sacx: PC RVA=",pc-base);
        if(lr>=base && lr-base<task->image_size)sacx_panic_value("sacx: LR RVA=",lr-base);
        sacx_panic_value("sacx: stack low=",(uintptr_t)task->execution_stack);
        sacx_panic_value("sacx: stack high=",(uintptr_t)task->execution_stack+64u*4096u);
        if(task->execution_stack && sp>=(uintptr_t)task->execution_stack &&
           sp-(uintptr_t)task->execution_stack<64u*4096u)
            sacx_panic_value("sacx: stack bytes remaining=",sp-(uintptr_t)task->execution_stack);
        terminal_print_inline("sacx: active API=");terminal_print(task->active_api?task->active_api:"none");
        terminal_print_inline("sacx: last completed API=");terminal_print(task->last_api?task->last_api:"none");
        sacx_panic_value("sacx: API depth=",task->api_depth);
    }
    /* Resolve the owner through fixed slots, not an unchecked panic pointer. */
    for(uint32_t i=0;i<SACX_MAX_TASKS;++i)
        for(uint32_t n=0;n<SACX_MAX_TASK_NET_REQUESTS;++n)
            if(G_net_active_slot==&G_tasks[i].net_requests[n]) {
                terminal_print_inline("sacx: active network URL=");terminal_print(G_net_active_slot->url);
                sacx_panic_value("sacx: network request status=",G_net_active_slot->status);
            }
}
