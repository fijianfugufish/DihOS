/* SACX runs after scene rendering. Retain its hover request for the next
 * cursor commit, after native widgets have selected their shapes. */
static int sacx_api_mouse_set_cursor(uint32_t cursor)
{
    if(!G_current_task||smp_current_logical_id()!=0u)return -1;
    int rc=kmouse_set_cursor(sacx_to_mouse_cursor(cursor));
    if(!rc){
        G_current_task->requested_cursor=(uint32_t)sacx_to_mouse_cursor(cursor);
        G_current_task->cursor_request_ms=dihos_time_monotonic_ms();
        G_current_task->cursor_request_valid=1;
    }
    return rc;
}
extern "C" void sacx_runtime_update_cursor(void)
{
    /* Native resize, button, text-input and modal/busy cursors take priority. */
    if(kmouse_current_cursor()!=KMOUSE_CURSOR_ARROW)return;
    uint64_t now=dihos_time_monotonic_ms();
    for(auto &task:G_tasks){
        if(!task.cursor_request_valid||!task.started||
           (task.state!=SACX_TASK_READY&&task.state!=SACX_TASK_SLEEPING)||
           now-task.cursor_request_ms>500u)continue;
        for(auto &window:task.windows){
            if(window.used&&kwindow_point_can_receive_input(window.handle,kmouse_x(),kmouse_y())){
                (void)kmouse_set_cursor((kmouse_cursor)task.requested_cursor);return;
            }
        }
    }
}
