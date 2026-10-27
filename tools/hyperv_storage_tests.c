#define main channel_baseline_main
#include "hyperv_channel_tests.c"
#undef main
#define VSTOR_OPERATION_COMPLETE_IO 1u
static uint8_t *G_storvsc_ring;
static uint32_t G_storvsc_gpadl=1,G_storvsc_connection_id=10;
#include "../kernel/hyperv/vmbus_storage_io.inc"
static uint8_t *dma;
static uint32_t host_requests;
static void storage_host(void)
{
    uint8_t request[104],completion[80]={0},trailer[8]={0};
    uint8_t *out=G_storvsc_ring,*in=out+4*4096;
    uint32_t r=hv_ring_load(out+4),w=hv_ring_load(out);
    if (r==w) return;
    vmbus_ring_copy_from(out,r,request,104);
    uint32_t bytes=hv_get16(request+4)*8u;
    vmbus_ring_copy_from(out,(r+bytes)%VMBUS_RING_DATA_SIZE,trailer,8);
    assert(hv_get64(trailer)==(uint64_t)r<<32);
    hv_ring_store(out+4,(r+bytes+8)%VMBUS_RING_DATA_SIZE);
    ++host_requests;
    for (uint32_t i=0;i<4096;++i) dma[i]=(uint8_t)(host_requests+i);
    hv_put16(completion,host_requests%2 ? 11:6);hv_put16(completion+2,2);hv_put16(completion+4,10);
    hv_put64(completion+8,hv_get64(request+8));hv_put32(completion+16,1);completion[30]=1;hv_put32(completion+40,4096);
    uint32_t pos=hv_ring_load(in);
    if (host_requests%17==0) { /* out-of-order stale completion must be ignored */
        hv_put64(completion+8,0xffff);
        pos=vmbus_ring_copy_to(in,pos,completion,80);pos=vmbus_ring_copy_to(in,pos,trailer,8);
        hv_put64(completion+8,hv_get64(request+8));
    }
    pos=vmbus_ring_copy_to(in,pos,completion,80);pos=vmbus_ring_copy_to(in,pos,trailer,8);hv_ring_store(in,pos);
}
int main(void)
{
    G_storvsc_ring=pmem_alloc_pages(8);dma=pmem_alloc_pages(1);
    hv_ring_store(G_storvsc_ring+64,1);hv_ring_store(G_storvsc_ring+4*4096+64,1);
    on_signal=storage_host;on_poll=storage_host;
    uint8_t req[64]={0},response[64];uint32_t op,status;
    for (uint32_t i=0;i<2000;++i) {
        /* Alternate empty->nonempty notifications with a host already draining. */
        hv_ring_store(G_storvsc_ring+8,i%2);
        uint32_t signals=signal_count;
        assert(!vmbus_storvsc_send_gpa_direct(req,dma,4096));
        assert(signal_count-signals==(i%2 ? 0u:1u));
        assert(!vmbus_storvsc_wait_completion(response,&op,&status));
        assert(op==1 && status==0 && dma[0]==(uint8_t)(i+1));
    }
    assert(host_requests==2000 && G_storvsc_sequence==2000);
    on_signal=on_poll=0;hv_ring_store(G_storvsc_ring+8,0);
    assert(!vmbus_storvsc_send_gpa_direct(req,dma,4096));
    uint32_t signals=signal_count;
    assert(vmbus_storvsc_wait_completion(response,&op,&status)<0);
    assert(signal_count==signals && G_storvsc_faulted); /* no timeout interrupt flood */
    assert(vmbus_storvsc_send_gpa_direct(req,dma,4096)<0 && signal_count==signals);
    puts("PASS: 2000 storage DMA completions across ring wrap, masked-host signaling, stale transaction rejection, bounded timeout, no buffer reuse after timeout");
    return 0;
}
