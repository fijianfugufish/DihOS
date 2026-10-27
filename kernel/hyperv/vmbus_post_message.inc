/* Legacy storage/HID boot calls keep their original one-shot transport and
 * pinned input lifetime. Device GPADLs add retries without changing that path.
 * Input pages are retained until reboot; do not return hypercall memory to
 * the boot allocator while investigating guest-only reuse failures. */
static uint32_t G_post_retries, G_post_status;
static uint64_t vmbus_post_channel_message(uint32_t connection_id,
                                           const uint8_t *payload,
                                           uint32_t payload_size)
{
    uint8_t *input=(uint8_t *)pmem_alloc_pages(1);
    if (!input || !payload || payload_size>240u) return ~0ull;
    vmbus_zero_page(input);
    vmbus_put_u32(input,connection_id);
    vmbus_put_u32(input+8,HV_MESSAGE_TYPE_CHANNEL);
    vmbus_put_u32(input+12,payload_size);
    for (uint32_t i=0;i<payload_size;++i) input[16+i]=payload[i];
    asm_dma_clean_range(input,256);
    return hyperv_hypercall(HV_CALL_POST_MESSAGE,pmem_virt_to_phys(input),0);
}
static uint64_t vmbus_device_post_channel_message(uint32_t connection_id,
                                                  const uint8_t *payload,
                                                  uint32_t payload_size)
{
    uint64_t result=~0ull, deadline=dihos_time_monotonic_ms()+1000;
    uint32_t delay=1;
    G_post_retries=0;G_post_status=0xffffffffu;
    if (!payload || payload_size>240u) return result;
    for (uint32_t attempt=0;attempt<64;++attempt) {
        result=vmbus_post_channel_message(connection_id,payload,payload_size);
        G_post_status=(uint16_t)result;
        if (!G_post_status) break;
        if (G_post_status!=0xbu && G_post_status!=0x12u && G_post_status!=0x13u) break;
        uint64_t now=dihos_time_monotonic_ms();
        if (now>=deadline || attempt==63) break;
        uint64_t retry_at=now+delay;
        if (retry_at>deadline) retry_at=deadline;
        while (dihos_time_monotonic_ms()<retry_at) asm_relax();
        ++G_post_retries;
        if (delay<16) delay*=2;
    }
    return result;
}
