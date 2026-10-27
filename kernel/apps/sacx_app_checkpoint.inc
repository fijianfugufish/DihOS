static int sacx_api_app_checkpoint(void)
{
    sacx_task *task=G_current_task;
    if(!task||smp_current_logical_id()!=0u||!kfiber_is_current(&task->execution)||task->api_depth||task->preempt_guard_depth) return 0;
    if(task->exit_requested||dihos_time_monotonic_ms()>=task->execution_deadline) {
        ++task->preemptions;kfiber_yield();
    }
    return 0;
}

