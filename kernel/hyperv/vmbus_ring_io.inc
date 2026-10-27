static int vmbus_signal_connection(uint32_t connection)
{
    if (!connection) return -1;
    /* Modern VMBus uses the channel connection ID, not a legacy interrupt
     * bitmap in the SynIC event page. Do not modify the host's event flags. */
    return (uint16_t)hyperv_hypercall(HV_CALL_SIGNAL_EVENT|HV_HYPERCALL_FAST_BIT,connection,0) ? -1 : 0;
}
static int vmbus_ring_write(uint8_t *ring,uint32_t connection,const uint8_t *packet,uint32_t bytes)
{
    uint8_t trailer[8];
    uint32_t w=hv_ring_load(ring), r=hv_ring_load(ring+4);
    if (w>=VMBUS_RING_DATA_SIZE || r>=VMBUS_RING_DATA_SIZE || !bytes || bytes%8) return -1;
    uint32_t space=(r+VMBUS_RING_DATA_SIZE-w-1)%VMBUS_RING_DATA_SIZE;
    if (bytes+8>space) return 1;
    hv_put64(trailer,(uint64_t)w<<32);
    uint32_t next=vmbus_ring_copy_to(ring,w,packet,bytes);
    next=vmbus_ring_copy_to(ring,next,trailer,sizeof(trailer));
    hv_ring_store(ring,next);
    return hv_ring_notify_write(ring,w) ? vmbus_signal_connection(connection) : 0;
}
static int vmbus_ring_read(uint8_t *ring,uint32_t connection,uint8_t *packet,uint32_t capacity,uint32_t *bytes)
{
    uint8_t header[16];
    uint32_t w=hv_ring_load(ring), r=hv_ring_load(ring+4), length;
    if (bytes) *bytes=0;
    if (w>=VMBUS_RING_DATA_SIZE || r>=VMBUS_RING_DATA_SIZE || !packet || capacity<16) return -1;
    uint32_t available=(w+VMBUS_RING_DATA_SIZE-r)%VMBUS_RING_DATA_SIZE;
    if (!available) return 1;
    if (available<24) return -1;
    vmbus_ring_copy_from(ring,r,header,sizeof(header));
    if (hv_packet_bounds(header,available,capacity,&length)) return -1;
    vmbus_ring_copy_from(ring,r,packet,length);
    uint32_t next=(r+length+8)%VMBUS_RING_DATA_SIZE;
    if (hv_ring_notify_read(ring,r,next,VMBUS_RING_DATA_SIZE) && vmbus_signal_connection(connection)) return -1;
    if (bytes) *bytes=length;
    return 0;
}
