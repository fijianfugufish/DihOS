#pragma once
#include <stdint.h>

static inline uint16_t hv_get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static inline uint32_t hv_get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static inline uint64_t hv_get64(const uint8_t *p) { return hv_get32(p) | (uint64_t)hv_get32(p+4)<<32; }
static inline void hv_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void hv_put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8)); }
static inline void hv_put64(uint8_t *p, uint64_t v) { hv_put32(p,(uint32_t)v); hv_put32(p+4,(uint32_t)(v>>32)); }
static inline int hv_range(uint32_t capacity, uint32_t offset, uint32_t length) { return offset <= capacity && length <= capacity-offset; }
static inline int hv_packet_bounds(const uint8_t *header, uint32_t available,
                                    uint32_t capacity, uint32_t *length)
{
    uint32_t n=hv_get16(header+4)*8u, off=hv_get16(header+2)*8u;
    if (off < 16 || n < off || n > capacity || available < 8 || n > available-8) return -1;
    *length=n;
    return 0;
}

/* RNDIS data offsets are relative to the start of the body, after type/size. */
static inline int hv_rndis_frame(const uint8_t *p, uint32_t bytes, uint32_t *offset, uint32_t *length)
{
    if (!p || bytes < 44 || hv_get32(p) != 1 || hv_get32(p+4) < 44 || hv_get32(p+4) > bytes) return -1;
    uint32_t off=hv_get32(p+8), len=hv_get32(p+12), size=hv_get32(p+4);
    if (off < 36 || off > size-8 || !hv_range(size, off+8, len) || len < 14 || len > 1514) return -1;
    *offset=off+8; *length=len;
    return 0;
}
