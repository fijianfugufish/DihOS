extern "C" int sacx_runtime_launch_ex(const char *raw_path,const char *friendly_path,
    const char *arg_raw_path,const char *arg_friendly_path,const sacx_runtime_io *io,uint32_t *out_task_id)
{
    if(out_task_id)*out_task_id=0;
    if(!raw_path||!raw_path[0])return -1;
    uint32_t length=0;while(raw_path[length]&&length<256u)++length;
    if(length>=256u)return -1;
    sacx_task *task=sacx_alloc_task_slot();
    if(!task){terminal_warn("sacx: no free task slot");return -1;}
    sacx_task_reset(task);task->task_id=G_next_task_id++;
    if(!G_next_task_id)G_next_task_id=1;
    task->state=SACX_TASK_SLEEPING;task->wake_tick=~(uint64_t)0;
    if(io)task->io=*io;
    sacx_copy_trunc(task->launch_path,sizeof task->launch_path,raw_path);
    sacx_copy_trunc(task->friendly_path,sizeof task->friendly_path,friendly_path?friendly_path:raw_path);
    sacx_copy_trunc(task->launch_arg_raw,sizeof task->launch_arg_raw,arg_raw_path?arg_raw_path:"");
    sacx_copy_trunc(task->launch_arg_friendly,sizeof task->launch_arg_friendly,arg_friendly_path?arg_friendly_path:"");
    sacx_copy_trunc(task->exit_message,sizeof task->exit_message,"queued: application load");
    task->launch_pending=1;sacx_task_init_api(task);
    if(out_task_id)*out_task_id=task->task_id;
    return 0;
}
