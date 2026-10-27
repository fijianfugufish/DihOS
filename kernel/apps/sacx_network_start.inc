static int sacx_api_net_request_start_ex(const sacx_net_request_desc_ex *desc, uint32_t *out_request_id)
{
    sacx_net_request_slot *slot = 0;
    uint32_t capacity;
    if (out_request_id)
        *out_request_id = 0u;
    if (!G_current_task || !desc || !desc->url || !desc->url[0] || !out_request_id ||
        desc->body_size > 4096u || (desc->body_size && !desc->body))
        return -1;
    uint32_t url_bytes=0;while(url_bytes<sizeof(slot->url)&&desc->url[url_bytes])++url_bytes;
    if(url_bytes==sizeof(slot->url))return -1; /* never silently truncate navigation */
    for (uint32_t i = 0u; i < SACX_MAX_TASK_NET_REQUESTS; ++i)
        if (!G_current_task->net_requests[i].used)
        {
            slot = &G_current_task->net_requests[i];
            memset(slot, 0, sizeof(*slot));
            slot->used = 1u;
            slot->id = i + 1u;
            break;
        }
    if (!slot)
        return -3;
    capacity = desc->max_response_bytes ? desc->max_response_bytes : SACX_NET_DEFAULT_BYTES;
    if (capacity < 4096u)
        capacity = 4096u;
    if (capacity > SACX_NET_MAX_BYTES)
        capacity = SACX_NET_MAX_BYTES;
    slot->pages = ((uint64_t)capacity + 4095ull) >> 12;
    slot->buffer = (uint8_t *)pmem_alloc_pages(slot->pages);
    if (!slot->buffer)
    {
        memset(slot, 0, sizeof(*slot));
        return -4;
    }
    slot->capacity = capacity;
    slot->redirect_limit = desc->redirect_limit > 10u ? 10u : desc->redirect_limit;
    if (!slot->redirect_limit)
        slot->redirect_limit = 5u;
    slot->timeout_ms = desc->timeout_ms;
    if (slot->timeout_ms < 1000u)
        slot->timeout_ms = 45000u;
    if (slot->timeout_ms > 120000u)
        slot->timeout_ms = 120000u;
    sacx_copy_trunc(slot->url, sizeof(slot->url), desc->url);
    sacx_copy_trunc(slot->method, sizeof(slot->method), desc->method && desc->method[0] ? desc->method : "GET");
    sacx_copy_trunc(slot->content_type, sizeof(slot->content_type),
                    desc->content_type && desc->content_type[0] ? desc->content_type : "application/x-www-form-urlencoded");
    slot->request_body_size = desc->body_size;
    if (desc->body_size)
        memcpy(slot->request_body, desc->body, desc->body_size);
    __atomic_store_n(&slot->status, SACX_NET_STATUS_QUEUED, __ATOMIC_RELEASE);
    asm_dma_clean_range(slot, sizeof(*slot));

    /* The core-0 network continuation owns the request from the next pump.
     * Never execute a DNS/TCP/TLS transaction inside request-start. */
    slot->job_id = 0u;
    *out_request_id = slot->id;
    return 0;
}


static int sacx_api_net_request_start_stream(const sacx_net_request_desc_ex *desc,uint32_t *id) {
    int rc=sacx_api_net_request_start_ex(desc,id);
    if(!rc)G_current_task->net_requests[*id-1].no_follow=1;
    return rc;
}
