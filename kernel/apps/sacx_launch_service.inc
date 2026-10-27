static void sacx_launch_enter(void *context)
{
    sacx_task *task=(sacx_task*)context;
    uint8_t *data=0;uint32_t size=0;
    G_launch_result=-1;
    if(sacx_loader_checkpoint())return;
    sacx_copy_trunc(task->exit_message,sizeof task->exit_message,"loading: reading application");
    terminal_print("sacx: startup reading application");
    if(sacx_read_file_all(task->launch_path,&data,&size))return;
    sacx_copy_trunc(task->exit_message,sizeof task->exit_message,"loading: validating and mapping image");
    terminal_print("sacx: startup validating and mapping image");
    int rc=sacx_loader_checkpoint()?-1:sacx_load_image(task,data,size,task->friendly_path);
    pmem_free_pages(data,(size+4095u)/4096u);
    if(sacx_loader_checkpoint())rc=-1;
    G_launch_result=rc;
}

static void sacx_runtime_launch_poll(void)
{
    if(G_current_task)return;
    if(!G_launch_active){
        for(auto &task:G_tasks)if(task.launch_pending){G_launch_active=&task;break;}
        if(!G_launch_active)return;
        if(!G_launch_stack)G_launch_stack=pmem_alloc_pages(64);
        if(!G_launch_stack){
            sacx_task *task=G_launch_active;G_launch_active=0;task->launch_pending=0;
            sacx_task_finish(task,-1,"application loader stack allocation failed",SACX_TASK_FAULTED);return;
        }
        G_launch_deadline=dihos_time_monotonic_ms()+60000;
        kfiber_init(&G_launch_fiber,G_launch_stack,64*4096,sacx_launch_enter,G_launch_active);
    }
    G_launch_slice_end=dihos_time_monotonic_ms()+2;
    int done=kfiber_resume(&G_launch_fiber);
    if(!done)return;
    sacx_task *task=G_launch_active;G_launch_active=0;task->launch_pending=0;
    if(done<0||G_launch_result||task->launch_cancelled){
        const char *reason=task->launch_cancelled?"application launch cancelled":
          dihos_time_monotonic_ms()>=G_launch_deadline?"application startup timed out":
          done<0?"invalid application loader continuation":task->exit_message;
        char message[128];sacx_copy_trunc(message,sizeof message,reason);
        sacx_task_finish(task,-1,message,task->launch_cancelled?SACX_TASK_EXITED:SACX_TASK_FAULTED);
        return;
    }
    task->state=SACX_TASK_READY;task->wake_tick=dihos_time_ticks();
    sacx_copy_trunc(task->exit_message,sizeof task->exit_message,"loaded; awaiting application entry");
    terminal_print("sacx: startup image ready");
}
