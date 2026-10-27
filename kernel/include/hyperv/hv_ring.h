#pragma once
#include "hyperv/hv_wire.h"

/* VMBus shared pages are coherent normal memory. Cache-line invalidation on
 * headers shared with the host/other VPs can discard unrelated field updates. */
static inline uint32_t hv_ring_load(const uint8_t *p)
{ return __atomic_load_n((const uint32_t *)p,__ATOMIC_ACQUIRE); }
static inline void hv_ring_store(uint8_t *p,uint32_t value)
{ __atomic_store_n((uint32_t *)p,value,__ATOMIC_RELEASE); }
static inline void hv_ring_barrier(void)
{ __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline int hv_ring_notify_write(const uint8_t *ring,uint32_t previous_write)
{
    hv_ring_barrier();
    if (hv_ring_load(ring+8)) return 0;
    return hv_ring_load(ring+4)==previous_write;
}
static inline int hv_ring_notify_read(uint8_t *ring,uint32_t old_read,uint32_t next,uint32_t capacity)
{
    hv_ring_barrier();
    hv_ring_store(ring+4,next);
    hv_ring_barrier();
    if (!(hv_ring_load(ring+64)&1)) return 0;
    uint32_t pending=hv_ring_load(ring+12);
    if (!pending) return 0;
    uint32_t write=hv_ring_load(ring), freed=(next+capacity-old_read)%capacity;
    uint32_t space=capacity-(write+capacity-next)%capacity;
    return space>pending && space>=freed && space-freed<=pending;
}
