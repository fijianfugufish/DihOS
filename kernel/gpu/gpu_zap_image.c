#include "gpu/gpu_zap.h"

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t u16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static int inside(uint64_t n, uint32_t offset, uint32_t size)
{ return offset <= n && size <= n - offset; }

int gpu_zap_parse(const void *data, uint64_t bytes, gpu_zap_layout *out)
{
    const uint8_t *p = data, *h, *c, *s;
    if (out) *out = (gpu_zap_layout){0};
    if (!p || !out || bytes < 148 || bytes > 1024*1024) return -1;
    if (u32(p) != 0x464c457fu || p[4] != 1 || p[5] != 1 || p[6] != 1 ||
        u16(p+16) != 2 || u16(p+18) != 0xa4 || u32(p+20) != 1 ||
        u32(p+28) != 52 || u16(p+40) != 52 ||
        u16(p+42) != 32 || u16(p+44) != 3) return -2;
    h=p+52; c=h+32; s=c+32;
    /* Reject split files, fixed-address images, extra load segments and
     * layouts we cannot safely relocate. TZ still verifies the signature. */
    if (u32(h) != 0 || u32(h+4) != 0 || u32(h+16) != 148 ||
        u32(h+24) != 0x07000000u ||
        u32(c) != 1 || u32(c+8) != 0x1000 || u32(c+12) != 0x1000 ||
        u32(c+24) != 0x08000007u || u32(c+28) != 0x100000 ||
        u32(s) != 0 || u32(s+24) != 0x02000000u) return -3;
    uint32_t co=u32(c+4), cb=u32(c+16), mb=u32(c+20);
    uint32_t so=u32(s+4), sb=u32(s+16), entry=u32(p+24);
    if (co < 148 || !cb || cb > mb || mb > 1024*1024 ||
        !inside(bytes,co,cb) || so < co || so-co < cb ||
        !sb || sb > 4096-148 || !inside(bytes,so,sb) ||
        u32(s+20) != sb || entry < 0x1000 || entry-0x1000 >= cb) return -4;
    *out=(gpu_zap_layout){148,so,sb,co,cb,(mb+4095u)&~4095u};
    return 0;
}
