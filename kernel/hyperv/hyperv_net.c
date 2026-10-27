#include "hyperv/hyperv_net.h"
#include "hyperv/hv_wire.h"
#include "hyperv/vmbus.h"
#include "memory/pmem.h"
#include "asm/asm.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"

/* One queue, no offloads. Shared memory remains pinned until reboot, including
 * on failure: no host-owned page may be returned to the allocator. */
#define NET_RX_PAGES 512u
static uint8_t *rx_buffer, *tx_buffer;
static uint8_t mac_address[6], response[512], nv_response[512];
static uint8_t frames[32][1514];
static uint16_t frame_lengths[32];
static uint32_t head, count, response_size, nv_size, request_id;
static uint64_t sequence, tx_wait, tx_done, nv_id;
static uint32_t tx_status, ready, link, failed;
static uint32_t init_status, init_reply_bytes;
static int init_send_result, init_wait_result;
static uint32_t control_type, control_oid;
static int control_send_result;
static uint32_t data_tx, data_tx_ok, data_tx_failed, transfer_rx, ethernet_rx;
static uint32_t malformed_rx, full_rx, last_rx_type, last_rx_bytes, last_tx_status;
static const char *detail = "Hyper-V Ethernet not initialized";

static void bytes_copy(void *dst, const void *src, uint32_t n)
{ for (uint32_t i=0;i<n;++i) ((uint8_t *)dst)[i]=((const uint8_t *)src)[i]; }

static void rndis_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t off, len;
    if (bytes < 8 || hv_get32(p+4) > bytes || hv_get32(p+4) < 8) return;
    bytes = hv_get32(p+4);
    if (hv_get32(p) == 1) {
        last_rx_type=1; last_rx_bytes=bytes;
        if (hv_rndis_frame(p, bytes, &off, &len)) { ++malformed_rx; return; }
        ++ethernet_rx;
        if (count < 32) {
            uint32_t slot=(head+count)%32;
            bytes_copy(frames[slot],p+off,len); frame_lengths[slot]=(uint16_t)len; ++count;
        } else ++full_rx;
    } else if (hv_get32(p) == 7 && bytes >= 12) {
        last_rx_type=7; last_rx_bytes=bytes;
        uint32_t status=hv_get32(p+8);
        if (status == 0x4001000c) { link=0; count=0; detail="Hyper-V Ethernet disconnected"; }
        if (status == 0x4001000b) { link=1; detail="Hyper-V Ethernet link connected"; }
    } else if ((hv_get32(p)&0x80000000u) && bytes >= 16 &&
               hv_get32(p+8) == request_id && bytes <= sizeof(response)) {
        bytes_copy(response,p,bytes); response_size=bytes;
    }
}

/* NetVSP expects the reserved tail of its fixed v1 message envelope too.
 * In particular INIT has 20 reserved bytes after type/min/max (32 total),
 * as documented by iPXE netvsc_init_message. Short requests can be ignored. */
static int nvsp_send(uint16_t type, uint64_t tid, const void *data, uint32_t bytes,
                     const void *gpa, uint32_t gpa_bytes)
{
    uint8_t wire[32]={0};
    if (bytes>sizeof(wire) || (bytes && !data)) return -1;
    bytes_copy(wire,data,bytes);
    return vmbus_device_send(VMBUS_DEVICE_NET,type,tid,wire,sizeof(wire),gpa,gpa_bytes);
}

/* Receive completions are mandatory even when an Ethernet frame is dropped. */
static int pump_one(void)
{
    uint8_t packet[4096];
    uint32_t bytes=0, off, type;
    int rc=vmbus_device_receive(VMBUS_DEVICE_NET, packet,sizeof(packet),&bytes);
    if (rc) return rc;
    off=hv_get16(packet+2)*8u; type=hv_get16(packet);
    if (off > bytes) return -1;
    if (bytes-off < 4) return type == 11 ? 0 : -1;
    uint8_t *p=packet+off;
    uint32_t len=bytes-off;
    uint64_t tid=hv_get64(packet+8);
    if (type == 11 && hv_get32(p) == 108 && len >= 8 && tid == tx_wait) {
        tx_done=tid; tx_status=hv_get32(p+4);
    } else if ((type == 6 || type == 11) && (hv_get32(p) == 2 || hv_get32(p) == 102)) {
        if (len <= sizeof(nv_response)) { bytes_copy(nv_response,p,len); nv_size=len; nv_id=tid; }
    } else if (type == 7) {
        ++transfer_rx;
        uint8_t ack[8]={0};
        int valid=off >= 24 && hv_get16(packet+16) == 0xcafe && hv_get32(p) == 107;
        uint32_t ranges=off >= 24 ? hv_get32(packet+20) : 0;
        if (ranges > (off-24)/8) valid=0;
        if (valid) for (uint32_t i=0;i<ranges;++i) {
            uint32_t n=hv_get32(packet+24+i*8), start=hv_get32(packet+28+i*8);
            if (!hv_range(NET_RX_PAGES*4096,start,n)) { valid=0; break; }
            asm_dma_invalidate_range(rx_buffer+start,n);
            /* A transfer range may aggregate several RNDIS messages. */
            uint32_t used=0;
            while (n-used >= 8) {
                uint32_t size=hv_get32(rx_buffer+start+used+4);
                if (size < 8 || size > n-used) { valid=0; break; }
                rndis_receive(rx_buffer+start+used,size);
                used+=size;
            }
        }
        if (!valid) ++malformed_rx;
        hv_put32(ack,108); hv_put32(ack+4,valid ? 1 : 6);
        if (nvsp_send(11,tid,ack,sizeof(ack),0,0)) return -1;
    }
    return 0;
}

void hyperv_net_poll(void)
{
    if (!ready || failed) return;
    for (uint32_t i=0;i<32;++i) {
        int rc=pump_one();
        if (rc == 1) break;
        if (rc < 0) { failed=1; link=0; detail="Hyper-V Ethernet transport failed; reboot required"; break; }
    }
}

static int wait_nvsp(uint32_t type, uint64_t tid)
{
    uint64_t deadline=dihos_time_monotonic_ms()+2000;
    while (dihos_time_monotonic_ms() < deadline) {
        if (nv_size >= 4 && nv_id == tid && hv_get32(nv_response) == type) return 0;
        if (pump_one() < 0) return -1;
        asm_relax();
    }
    return -1;
}

static int send_rndis(uint32_t bytes, uint32_t control)
{
    uint8_t nv[16]={0};
    hv_put32(nv,107); hv_put32(nv+4,control); hv_put32(nv+8,0xffffffffu);
    tx_wait=++sequence; tx_done=0;
    if (nvsp_send(6,tx_wait,nv,sizeof(nv),tx_buffer,bytes)) return -1;
    uint64_t deadline=dihos_time_monotonic_ms()+2000;
    while (tx_done != tx_wait && dihos_time_monotonic_ms() < deadline) {
        if (pump_one() < 0) break;
        asm_relax();
    }
    if (tx_done != tx_wait) {
        failed=1; link=0; detail="Hyper-V Ethernet TX timeout; shared buffer pinned";
        return -1; /* Never overwrite a buffer whose completion was lost. */
    }
    return tx_status == 1 ? 0 : -1;
}

static int control(uint32_t type, uint32_t oid, uint32_t value)
{
    uint32_t size=type == 2 ? 24 : (type == 5 ? 32 : 28);
    for (uint32_t i=0;i<64;++i) tx_buffer[i]=0;
    hv_put32(tx_buffer,type); hv_put32(tx_buffer+4,size); hv_put32(tx_buffer+8,++request_id);
    if (type == 2) { hv_put32(tx_buffer+12,1); hv_put32(tx_buffer+20,4096); }
    else {
        hv_put32(tx_buffer+12,oid);
        /* Relative to RequestId (byte 8): the query/set body is 20 bytes,
         * including the VC handle. Set this even with no query input data. */
        hv_put32(tx_buffer+20,20);
        if (type == 5) { hv_put32(tx_buffer+16,4); hv_put32(tx_buffer+20,20); hv_put32(tx_buffer+28,value); }
    }
    response_size=0;control_type=type;control_oid=oid;
    control_send_result=send_rndis(size,1);
    if (control_send_result) return -1;
    uint64_t deadline=dihos_time_monotonic_ms()+2000;
    while (!response_size && dihos_time_monotonic_ms() < deadline) {
        if (pump_one() < 0) return -1;
        asm_relax();
    }
    return response_size >= 16 && hv_get32(response) == (type|0x80000000u) && hv_get32(response+12) == 0 ? 0 : -1;
}

static const uint8_t *query_data(uint32_t expected_bytes)
{
    if (response_size<24) return 0;
    uint32_t length=hv_get32(response+16), offset=hv_get32(response+20);
    /* QUERY_CMPLT's fixed body occupies 16 bytes from RequestId. */
    if (length!=expected_bytes || offset<16 || offset>response_size-8 ||
        !hv_range(response_size,8+offset,length)) return 0;
    return response+8+offset;
}

int hyperv_net_init(void)
{
    uint8_t msg[16]={0};
    uint32_t handle;
    const char *stage="netvsc: NVSP version negotiation failed";
    if (ready) return 0;
    if (failed) return -1;
    failed=1;
    if (vmbus_device_open(VMBUS_DEVICE_NET)) { detail="Hyper-V network channel unavailable"; return -1; }
    hv_put32(msg,1); hv_put32(msg+4,2); hv_put32(msg+8,2); /* NVSP v1, no optional offloads */
    uint64_t tid=++sequence;
    nv_size=0; init_status=0xffffffffu;
    init_send_result=nvsp_send(6|VMBUS_SEND_REQUEST_COMPLETION,tid,msg,12,0,0);
    init_wait_result=init_send_result ? -1 : wait_nvsp(2,tid);
    init_reply_bytes=nv_size;
    if (nv_size>=16) init_status=hv_get32(nv_response+12);
    if (init_send_result || init_wait_result || nv_size<16 || init_status!=1) goto error;
    stage="netvsc: NDIS version announcement failed";
    hv_put32(msg,100); hv_put32(msg+4,6); hv_put32(msg+8,1); /* NDIS 6.1, matching Linux/iPXE */
    if (nvsp_send(6,++sequence,msg,12,0,0)) goto error;
    stage="netvsc: shared-buffer allocation failed";
    rx_buffer=pmem_alloc_pages(NET_RX_PAGES); tx_buffer=pmem_alloc_pages(1);
    if (!rx_buffer || !tx_buffer) goto error;
    for (uint32_t i=0;i<NET_RX_PAGES*4096;++i) rx_buffer[i]=0;
    stage="netvsc: receive-buffer GPADL failed";
    if (vmbus_device_share(0,rx_buffer,NET_RX_PAGES,&handle)) goto error;
    stage="netvsc: receive-buffer registration failed";
    hv_put32(msg,101); hv_put32(msg+4,handle); hv_put16(msg+8,0xcafe);
    tid=++sequence; nv_size=0;
    if (nvsp_send(6|VMBUS_SEND_REQUEST_COMPLETION,tid,msg,10,0,0) || wait_nvsp(102,tid) || nv_size < 28 ||
        hv_get32(nv_response+4) != 1 || hv_get32(nv_response+8) != 1 ||
        hv_get32(nv_response+12) != 0 || hv_get32(nv_response+24) > NET_RX_PAGES*4096) goto error;
    stage="netvsc: RNDIS initialization failed";
    if (control(2,0,0)) goto error;
    stage="netvsc: MAC query failed";
    if (control(4,0x01010101,0)) goto error;
    const uint8_t *mac=query_data(6);
    if (!mac) goto error;
    bytes_copy(mac_address,mac,6);
    /* Directed, broadcast, all multicast. No checksum/segmentation offloads. */
    stage="netvsc: packet filter setup failed";
    if (control(5,0x0001010e,0x0d)) goto error;
    stage="netvsc: link query failed";
    if (control(4,0x00010114,0)) goto error;
    const uint8_t *media=query_data(4);
    if (!media) goto error;
    link=hv_get32(media) == 0;
    failed=0; ready=1; detail=link ? "Hyper-V Ethernet connected (MTU 1500)" : "Hyper-V Ethernet disconnected";
    terminal_success("netvsc: RNDIS Ethernet initialized");
    return 0;
error:
    detail=stage;
    terminal_warn(detail);
    hyperv_net_diag_print();
    return -1;
}

int hyperv_net_online(void) { return ready && !failed && link; }
int hyperv_net_mac(uint8_t mac[6]) { if (!ready || !mac) return -1; bytes_copy(mac,mac_address,6); return 0; }
int hyperv_net_send(const void *frame, uint32_t bytes)
{
    if (!frame || bytes < 14 || bytes > 1514 || !hyperv_net_online()) return -1;
    for (uint32_t i=0;i<44;++i) tx_buffer[i]=0;
    hv_put32(tx_buffer,1); hv_put32(tx_buffer+4,44+bytes);
    hv_put32(tx_buffer+8,36); hv_put32(tx_buffer+12,bytes);
    bytes_copy(tx_buffer+44,frame,bytes);
    ++data_tx;
    int rc=send_rndis(44+bytes,0);
    last_tx_status=tx_done==tx_wait ? tx_status : 0xffffffffu;
    if (rc) ++data_tx_failed; else ++data_tx_ok;
    return rc;
}
int hyperv_net_receive(void *frame, uint32_t capacity, uint32_t *bytes)
{
    if (bytes) *bytes=0;
    if (!frame) return -1;
    hyperv_net_poll();
    if (!count) return 1;
    uint32_t n=frame_lengths[head];
    if (n > capacity) { head=(head+1)%32; --count; return -1; }
    bytes_copy(frame,frames[head],n); head=(head+1)%32; --count;
    if (bytes) *bytes=n;
    return 0;
}
uint32_t hyperv_net_pending(void) { return count; }
void hyperv_net_discard(void) { head=count=0; }
const char *hyperv_net_detail(void) { return detail; }

void hyperv_net_diag_print(void)
{
    vmbus_device_diag_print(VMBUS_DEVICE_NET);
    terminal_print(detail);
    terminal_print_inline("netvsc: MAC bytes: ");
    for (uint32_t i=0;i<6;++i) { terminal_print_inline_hex32(mac_address[i]); terminal_print_inline(" "); }
    terminal_print("");
    terminal_print_inline("netvsc: Ethernet TX attempted/OK/failed/last status: ");
    terminal_print_inline_hex32(data_tx); terminal_print_inline(" ");
    terminal_print_inline_hex32(data_tx_ok); terminal_print_inline(" ");
    terminal_print_inline_hex32(data_tx_failed); terminal_print_inline(" ");
    terminal_print_inline_hex32(last_tx_status); terminal_print("");
    terminal_print_inline("netvsc: RX transfers/Ethernet/invalid/queue full: ");
    terminal_print_inline_hex32(transfer_rx); terminal_print_inline(" ");
    terminal_print_inline_hex32(ethernet_rx); terminal_print_inline(" ");
    terminal_print_inline_hex32(malformed_rx); terminal_print_inline(" ");
    terminal_print_inline_hex32(full_rx); terminal_print("");
    terminal_print_inline("netvsc: last data/status RNDIS type/bytes: ");
    terminal_print_inline_hex32(last_rx_type); terminal_print_inline(" ");
    terminal_print_inline_hex32(last_rx_bytes); terminal_print("");
    terminal_print_inline("netvsc: init send/wait/response bytes/status: ");
    terminal_print_inline_hex32((uint32_t)init_send_result); terminal_print_inline(" ");
    terminal_print_inline_hex32((uint32_t)init_wait_result); terminal_print_inline(" ");
    terminal_print_inline_hex32(init_reply_bytes); terminal_print_inline(" ");
    terminal_print_inline_hex32(init_status); terminal_print("");
    terminal_print_inline("netvsc: RNDIS request type/OID/id/send: ");
    terminal_print_inline_hex32(control_type);terminal_print_inline(" ");
    terminal_print_inline_hex32(control_oid);terminal_print_inline(" ");
    terminal_print_inline_hex32(request_id);terminal_print_inline(" ");
    terminal_print_inline_hex32((uint32_t)control_send_result);terminal_print("");
    terminal_print_inline("netvsc: RNDIS reply bytes/type/id/status/length/offset: ");
    terminal_print_inline_hex32(response_size);
    for (uint32_t i=0;i<6;++i) {
        if (i==1) continue; /* MessageLength is already represented by bytes. */
        terminal_print_inline(" ");
        terminal_print_inline_hex32(hv_range(response_size,i*4,4) ? hv_get32(response+i*4):0xffffffffu);
    }
    terminal_print("");
}
