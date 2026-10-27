/* One outstanding StorVSC request. A timed-out host-owned GPA buffer must not
 * be reused for another request: a late completion may still write into it. */
static uint64_t G_storvsc_sequence, G_storvsc_pending;
static uint32_t G_storvsc_faulted;

static int vmbus_storvsc_send_packet(const uint8_t payload[64],void *buffer,uint32_t bytes)
{
    uint8_t packet[104]={0};
    uint32_t offset=buffer ? 40 : 16;
    uint64_t physical=buffer ? pmem_virt_to_phys(buffer) : 0;
    if (!G_storvsc_ring || !G_storvsc_gpadl || G_storvsc_faulted || G_storvsc_pending ||
        (buffer && (!bytes || bytes>4096-(physical&4095)))) return -1;
    uint64_t id=++G_storvsc_sequence;
    hv_put16(packet,buffer ? 9 : 6); hv_put16(packet+2,(uint16_t)(offset/8));
    hv_put16(packet+4,(uint16_t)((offset+64)/8)); hv_put16(packet+6,1); hv_put64(packet+8,id);
    if (buffer) {
        hv_put32(packet+20,1); hv_put32(packet+24,bytes);
        hv_put32(packet+28,(uint32_t)(physical&4095)); hv_put64(packet+32,physical>>12);
        asm_dma_clean_range(buffer,bytes);
    }
    for (uint32_t i=0;i<64;++i) packet[offset+i]=payload[i];
    G_storvsc_pending=id;
    int rc=vmbus_ring_write(G_storvsc_ring,G_storvsc_connection_id,packet,offset+64);
    if (rc) {
        G_storvsc_pending=0;
        if (rc<0) G_storvsc_faulted=1;
        return -1;
    }
    return 0;
}
static int vmbus_storvsc_send_inband(const uint8_t payload[64])
{ return vmbus_storvsc_send_packet(payload,0,0); }
static int vmbus_storvsc_send_gpa_direct(const uint8_t payload[64],void *buffer,uint32_t bytes)
{ return buffer ? vmbus_storvsc_send_packet(payload,buffer,bytes) : -1; }

static int vmbus_storvsc_wait_completion(uint8_t payload[64],uint32_t *operation,uint32_t *status)
{
    uint8_t packet[256]; uint32_t bytes;
    uint64_t deadline=dihos_time_monotonic_ms()+2000;
    if (!G_storvsc_pending || G_storvsc_faulted) return -1;
    while (dihos_time_monotonic_ms()<deadline) {
        int rc=vmbus_ring_read(G_storvsc_ring+VMBUS_RING_DOWNSTREAM_OFFSET*4096,
            G_storvsc_connection_id,packet,sizeof(packet),&bytes);
        if (rc==1) { asm_relax(); continue; }
        if (rc<0) break;
        if (hv_get64(packet+8)!=G_storvsc_pending) continue; /* late/unrelated completion */
        uint32_t offset=hv_get16(packet+2)*8u;
        uint32_t type=hv_get16(packet);
        if ((type!=11 && type!=6) || !hv_range(bytes,offset,64)) break;
        for (uint32_t i=0;i<64;++i) payload[i]=packet[offset+i];
        if (hv_get32(payload)!=VSTOR_OPERATION_COMPLETE_IO) continue;
        if (operation) *operation=hv_get32(payload);
        if (status) *status=hv_get32(payload+8);
        G_storvsc_pending=0;
        return 0;
    }
    G_storvsc_faulted=1;
    terminal_error("storvsc: completion timeout/invalid packet; channel stopped to protect pending DMA");
    return -1;
}
