/* Included by vmbus.c: use the existing SynIC transport and ring helpers. */
typedef struct vmbus_device_channel {
    uint32_t relid, connection, gpadl, opened, failed;
    uint8_t *ring;
    uint32_t share_pages, share_sent, share_status, share_retries;
    const char *share_stage;
} vmbus_device_channel;
static vmbus_device_channel G_devices[2];
static uint32_t G_next_gpadl = 0x000e2000;
static const uint8_t G_device_guids[2][16] = {
    {0x63,0x51,0x61,0xf8,0x3e,0xdf,0xc5,0x46,0x91,0x3f,0xf2,0xd2,0xf9,0x65,0xed,0x0e},
    {0x02,0x78,0x0a,0xda,0x77,0xe3,0xac,0x4a,0x8e,0x77,0x05,0x58,0xeb,0x10,0x73,0xf8}
};

static void vmbus_device_offer(const volatile uint8_t *offer)
{
    for (uint32_t i = 0; i < 2; ++i)
        if (!G_devices[i].relid && vmbus_guid_equal(offer+8, G_device_guids[i])) {
            G_devices[i].relid = vmbus_get_u32(offer+184);
            G_devices[i].connection = vmbus_get_u32(offer+192);
            terminal_print(i == 0 ? "vmbus: NetVSC offer" : "vmbus: synthetic video offer");
        }
}

/* Match both channel and request ID, never consume another open's reply. */
static int vmbus_device_response(uint32_t type, uint32_t relid, uint32_t id)
{
    volatile uint8_t *message = G_message_page + VMBUS_MESSAGE_SINT*256u;
    uint64_t deadline = dihos_time_monotonic_ms()+2000;
    while (dihos_time_monotonic_ms() < deadline) {
        (void)hyperv_set_vpreg(HV_REGISTER_EOM, 0);
        asm_dma_invalidate_range((const void *)message, 256);
        if (!vmbus_get_u32(message)) { asm_relax(); continue; }
        const volatile uint8_t *p = message+16;
        uint32_t kind = vmbus_get_u32(p);
        if (kind == VMBUS_CHANNEL_OFFER) vmbus_device_offer(p);
        int match = kind == type && vmbus_get_u32(p+8) == relid && vmbus_get_u32(p+12) == id;
        uint32_t status = vmbus_get_u32(p+16);
        vmbus_release_message(message);
        if (match) {
            if (type==VMBUS_CHANNEL_GPADL_CREATED)
                for (uint32_t i=0;i<2;++i)
                    if (G_devices[i].relid==relid) G_devices[i].share_status=status;
            return status == 0 ? 0 : -1;
        }
    }
    return -1;
}

int vmbus_device_share(uint32_t device, void *buffer, uint32_t pages, uint32_t *out)
{
    uint8_t msg[240] = {0};
    uint32_t id, first, sent;
    uint64_t phys;
    if (device >= 2 || !G_devices[device].relid || !buffer || !pages || pages > 512 || !out) return -1;
    phys = pmem_virt_to_phys(buffer);
    if (phys & 4095) return -1;
    vmbus_device_channel *channel=&G_devices[device];
    channel->share_pages=pages;channel->share_sent=0;
    channel->share_retries=0;channel->share_status=0xffffffffu;
    channel->share_stage="posting GPADL header";
    id = G_next_gpadl++;
    first = pages < 26 ? pages : 26;
    vmbus_put_u32(msg, VMBUS_CHANNEL_GPADL_HEADER);
    vmbus_put_u32(msg+8, G_devices[device].relid);
    vmbus_put_u32(msg+12, id);
    vmbus_put_u16(msg+16, (uint16_t)(8+pages*8));
    vmbus_put_u16(msg+18, 1);
    vmbus_put_u32(msg+20, pages*4096);
    for (uint32_t i = 0; i < first; ++i) vmbus_put_u64(msg+28+i*8, (phys>>12)+i);
    asm_dma_clean_range(buffer, pages*4096);
    uint32_t status=(uint16_t)vmbus_device_post_channel_message(G_vmbus_message_connection,msg,28+first*8);
    channel->share_retries+=G_post_retries;
    if (status) { channel->share_status=status; return -1; }
    sent = first;channel->share_sent=sent;
    channel->share_stage="posting GPADL body";
    while (sent < pages) {
        uint32_t count = pages-sent < 28 ? pages-sent : 28;
        for (uint32_t i = 0; i < sizeof(msg); ++i) msg[i] = 0;
        vmbus_put_u32(msg, 9); /* GPADL_BODY */
        vmbus_put_u32(msg+8, 0); /* message number, reserved by protocol */
        vmbus_put_u32(msg+12, id);
        for (uint32_t i = 0; i < count; ++i) vmbus_put_u64(msg+16+i*8, (phys>>12)+sent+i);
        status=(uint16_t)vmbus_device_post_channel_message(G_vmbus_message_connection,msg,16+count*8);
        channel->share_retries+=G_post_retries;
        if (status) { channel->share_status=status; return -1; }
        sent += count;channel->share_sent=sent;
    }
    channel->share_stage="waiting for GPADL creation";
    if (vmbus_device_response(VMBUS_CHANNEL_GPADL_CREATED, G_devices[device].relid, id)) return -1;
    channel->share_stage="GPADL created";
    *out = id;
    return 0;
}

int vmbus_device_open(uint32_t device)
{
    uint8_t msg[148] = {0};
    vmbus_device_channel *c;
    if (device >= 2) return -1;
    c = &G_devices[device];
    if (c->opened) return 0;
    if (!c->relid || c->failed || !G_message_page) return -1;
    /* Failed allocations stay pinned: the host may still own their GPADLs. */
    c->failed = 1;
    c->ring = pmem_alloc_pages(VMBUS_RING_PAGES);
    if (!c->ring) return -1;
    for (uint32_t i = 0; i < VMBUS_RING_PAGES*4096; ++i) c->ring[i] = 0;
    hv_ring_store(c->ring+64,1);
    hv_ring_store(c->ring+VMBUS_RING_DOWNSTREAM_OFFSET*4096+64,1);
    if (vmbus_device_share(device, c->ring, VMBUS_RING_PAGES, &c->gpadl)) return -1;
    vmbus_put_u32(msg, VMBUS_CHANNEL_OPEN);
    vmbus_put_u32(msg+8, c->relid);
    vmbus_put_u32(msg+12, 4+device);
    vmbus_put_u32(msg+16, c->gpadl);
    vmbus_put_u32(msg+24, VMBUS_RING_DOWNSTREAM_OFFSET);
    if ((uint16_t)vmbus_device_post_channel_message(G_vmbus_message_connection, msg, sizeof(msg)) ||
        vmbus_device_response(VMBUS_CHANNEL_OPEN_RESULT, c->relid, 4+device)) return -1;
    c->opened = 1;
    c->failed = 0;
    return 0;
}

int vmbus_device_send(uint32_t device, uint16_t type, uint64_t transaction,
                      const void *payload, uint32_t bytes, const void *gpa, uint32_t gpa_bytes)
{
    uint8_t packet[2048] = {0};
    uint32_t off = gpa ? 40 : 16, len = (off+bytes+7)&~7u;
    uint64_t phys = gpa ? pmem_virt_to_phys(gpa) : 0;
    if (device >= 2 || !G_devices[device].opened || bytes > sizeof(packet)-off ||
        (bytes && !payload) || (gpa && (!gpa_bytes || gpa_bytes > 4096-(phys&4095)))) return -1;
    vmbus_device_channel *c = &G_devices[device];
    /* High bit is a local API flag, never part of the wire packet type. */
    vmbus_put_u16(packet, gpa ? 9 : (uint16_t)(type&0x7fff));
    vmbus_put_u16(packet+2, (uint16_t)(off/8));
    vmbus_put_u16(packet+4, (uint16_t)(len/8));
    vmbus_put_u16(packet+6, gpa || (type&0x8000) ? 1 : 0);
    vmbus_put_u64(packet+8, transaction);
    if (gpa) {
        vmbus_put_u32(packet+20, 1);
        vmbus_put_u32(packet+24, gpa_bytes);
        vmbus_put_u32(packet+28, (uint32_t)(phys&4095));
        vmbus_put_u64(packet+32, phys>>12);
        asm_dma_clean_range(gpa, gpa_bytes);
    }
    for (uint32_t i = 0; i < bytes; ++i) packet[off+i] = ((const uint8_t *)payload)[i];
    return vmbus_ring_write(c->ring,c->connection,packet,len);
}

int vmbus_device_receive(uint32_t device, void *packet, uint32_t capacity, uint32_t *bytes)
{
    if (bytes) *bytes=0;
    if (device>=2 || !G_devices[device].opened) return -1;
    int rc=vmbus_ring_read(G_devices[device].ring+VMBUS_RING_DOWNSTREAM_OFFSET*4096,
        G_devices[device].connection,packet,capacity,bytes);
    if (rc<0) {
        G_devices[device].opened=0; G_devices[device].failed=1;
        terminal_error("vmbus: malformed device packet; channel disabled");
    }
    return rc;
}

void vmbus_device_diag_print(uint32_t device)
{
    if (device>=2) return;
    vmbus_device_channel *c=&G_devices[device];
    terminal_print(c->share_stage ? c->share_stage : "GPADL not attempted");
    terminal_print_inline("vmbus: GPADL pages/sent/status/retries: ");
    terminal_print_inline_hex32(c->share_pages);terminal_print_inline(" ");
    terminal_print_inline_hex32(c->share_sent);terminal_print_inline(" ");
    terminal_print_inline_hex32(c->share_status);terminal_print_inline(" ");
    terminal_print_inline_hex32(c->share_retries);terminal_print("");
}
