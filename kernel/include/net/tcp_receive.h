#pragma once
#include <stdint.h>
/* Keep a receive flight comfortably below the synthetic NIC's bounded queue.
 * No window scaling is offered. The worker may yield between receive polls. */
#define DIHOS_TCP_RECEIVE_WINDOW 16384u
/* Accept only a contiguous suffix. Sequence subtraction also handles wrap.
 * Future packets and complete duplicates are ACKed but not delivered twice. */
static inline uint32_t dihos_tcp_receive_skip(uint32_t next,uint32_t sequence,uint32_t bytes)
{
    uint32_t skip=next-sequence;
    return skip<bytes?skip:bytes;
}
static inline int dihos_tcp_receive_fin(uint32_t next,uint32_t sequence,uint32_t bytes)
{ return sequence+bytes==next; }
