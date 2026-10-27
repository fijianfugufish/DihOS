static int sacx_api_net_request_cancel(uint32_t request_id)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    if (!slot)
        return -1;
    __atomic_store_n(&slot->cancelled, 1u, __ATOMIC_RELEASE);
    asm_dma_clean_range((const void *)&slot->cancelled, sizeof(slot->cancelled));
    (void)kwork_cancel(slot->job_id);
    if (__atomic_load_n(&slot->status, __ATOMIC_ACQUIRE) == SACX_NET_STATUS_QUEUED)
    {
        __atomic_store_n(&slot->status, SACX_NET_STATUS_CANCELLED, __ATOMIC_RELEASE);
        asm_dma_clean_range((const void *)&slot->status, sizeof(slot->status));
    }
    return 0;
}

static int sacx_api_net_request_release(uint32_t request_id)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    uint32_t status;
    if (!slot)
        return -1;
    status = __atomic_load_n(&slot->status, __ATOMIC_ACQUIRE);
    if (slot==G_net_active_slot) {
        slot->cancelled=1;slot->release_when_done=1;return 0;
    }
    if (status==SACX_NET_STATUS_LOADING) return -2;
    if (slot->buffer && slot->pages)
        pmem_free_pages(slot->buffer, slot->pages);
    memset(slot, 0, sizeof(*slot));
    return 0;
}

