/* One core-0 transport owner. Continuations retain DNS/TCP/TLS state on a
 * private stack; no request pointer or DMA buffer belongs to an app's stack. */
static kfiber G_net_fiber;
static void *G_net_stack;
static uint64_t G_net_slice_end;
static uint32_t G_net_cursor;
static uint32_t G_net_background_active;
static uint64_t G_net_next_background;
static uint64_t G_net_slices,G_net_elapsed_ms,G_net_max_slice_ms;
static void sacx_network_checkpoint(void)
{
    if (smp_current_logical_id()==0u && kfiber_is_current(&G_net_fiber) && dihos_time_monotonic_ms()>=G_net_slice_end)
        kfiber_yield();
}
static void sacx_network_enter(void *slot) {
    if(slot)sacx_net_request_worker(slot);
    else knet_usb_background_poll();
}
extern "C" void sacx_runtime_network_poll(void)
{
    /* App callbacks must never be reentered from the network continuation. */
    if (G_current_task) return;
    if (!G_net_active_slot && !G_net_background_active) {
        const uint32_t count=SACX_MAX_TASKS*SACX_MAX_TASK_NET_REQUESTS;
        for (uint32_t i=0;i<count;++i) {
            uint32_t index=(G_net_cursor+i)%count;
            sacx_net_request_slot *slot=&G_tasks[index/SACX_MAX_TASK_NET_REQUESTS].net_requests[index%SACX_MAX_TASK_NET_REQUESTS];
            if (!slot->used || slot->status!=SACX_NET_STATUS_QUEUED) continue;
            G_net_cursor=(index+1)%count;
            G_net_active_slot=slot;
            break;
        }
        if(!G_net_active_slot) {
            uint64_t now=dihos_time_monotonic_ms();
            if(now<G_net_next_background)return;
            G_net_next_background=now+50;
            G_net_background_active=1;
        }
        if(!G_net_stack)G_net_stack=pmem_alloc_pages(64);
        if(!G_net_stack) {
            if(G_net_active_slot){
                sacx_copy_trunc(G_net_active_slot->info.error,sizeof(G_net_active_slot->info.error),"network continuation stack allocation failed");
                G_net_active_slot->status=SACX_NET_STATUS_FAILED;
            }
            G_net_active_slot=0;G_net_background_active=0;return;
        }
        kfiber_init(&G_net_fiber,G_net_stack,64*4096,sacx_network_enter,G_net_active_slot);
    }
    uint64_t began=dihos_time_monotonic_ms();
    G_net_slice_end=began+2;
    knet_link_set_checkpoint(sacx_network_checkpoint);
    knet_usb_set_worker_quiet(1);
    int done=kfiber_resume(&G_net_fiber);
    uint64_t elapsed=dihos_time_monotonic_ms()-began;
    ++G_net_slices;G_net_elapsed_ms+=elapsed;if(elapsed>G_net_max_slice_ms)G_net_max_slice_ms=elapsed;
    knet_usb_set_worker_quiet(0);
    knet_link_set_checkpoint(0);
    if (done) {
        sacx_net_request_slot *slot=G_net_active_slot;
        if(done<0&&slot){slot->status=SACX_NET_STATUS_FAILED;sacx_copy_trunc(slot->info.error,sizeof slot->info.error,"invalid network continuation");}
        G_net_active_slot=0;
        G_net_background_active=0;
        if(slot&&slot->release_when_done){if(slot->buffer&&slot->pages)pmem_free_pages(slot->buffer,slot->pages);memset(slot,0,sizeof(*slot));}
    }
}
