#include "hyperv/hv_hid.h"

typedef struct hid_globals {
    uint32_t page, bits, count, id;
    int32_t minimum, maximum;
} hid_globals;

static int32_t signed_item(uint32_t value, uint32_t bytes)
{
    if (bytes == 1) return (int8_t)value;
    if (bytes == 2) return (int16_t)value;
    return (int32_t)value;
}

int hv_hid_parse(hv_hid_layout *out, const uint8_t *desc, uint32_t size)
{
    hid_globals g = {0}, stack[8];
    uint32_t depth = 0, usages[48], n = 0, low = 0, high = 0;
    uint8_t range = 0, axes = 0;
    if (!out || !desc || !size || size > 4096) return -1;
    *out = (hv_hid_layout){0};
    for (uint32_t i = 0; i < size;) {
        uint8_t prefix = desc[i++];
        uint32_t bytes = prefix & 3, type = (prefix >> 2) & 3, tag = prefix >> 4, v = 0;
        if (prefix == 0xfe) return -1; /* no long items in supported mouse layouts */
        if (bytes == 3) bytes = 4;
        if (bytes > size - i) return -1;
        for (uint32_t j = 0; j < bytes; ++j) v |= (uint32_t)desc[i++] << (8*j);
        if (type == 1) {
            switch (tag) {
            case 0: g.page = v; break;
            case 1: g.minimum = signed_item(v, bytes); break;
            case 2: g.maximum = g.minimum < 0 ? signed_item(v, bytes) : (int32_t)v; break;
            case 7: g.bits = v; break;
            case 8: if (!v || v > 255) return -1; g.id = v; out->numbered = 1; break;
            case 9: g.count = v; break;
            case 10: if (depth == 8) return -1; stack[depth++] = g; break;
            case 11: if (!depth) return -1; g = stack[--depth]; break;
            }
        } else if (type == 2) {
            if (tag == 0) { if (n == 48) return -1; usages[n++] = v; }
            if (tag == 1) { low = v; range = 1; }
            if (tag == 2) high = v;
        } else if (type == 0) {
            if (tag == 8) {
                uint32_t bit = out->report_bits[g.id];
                if (!g.bits || g.bits > 32 || g.count > 512 || g.bits*g.count > 32768-bit) return -1;
                for (uint32_t j = 0; j < g.count; ++j) {
                    uint32_t usage = j < n ? usages[j] : (range && low+j <= high ? low+j : 0);
                    uint32_t page = usage > 65535 ? usage >> 16 : g.page;
                    usage &= 65535;
                    if (!(v & 1) && (v & 2) &&
                        ((page == 1 && (usage == 0x30 || usage == 0x31 || usage == 0x38)) ||
                         (page == 9 && usage >= 1 && usage <= 8))) {
                        if (out->count == 48 || g.maximum <= g.minimum) return -1;
                        out->fields[out->count++] = (hv_hid_field){
                            (uint16_t)(bit+j*g.bits), (uint8_t)g.bits, (uint8_t)g.id,
                            (uint8_t)usage, (uint8_t)page, (uint8_t)((v >> 2) & 1), g.minimum, g.maximum};
                        if (page == 1 && usage == 0x30) axes |= 1;
                        if (page == 1 && usage == 0x31) axes |= 2;
                    }
                }
                out->report_bits[g.id] = (uint16_t)(bit+g.bits*g.count);
            }
            n = 0; low = high = 0; range = 0;
        }
    }
    return axes == 3 && depth == 0 ? 0 : -1;
}

int hv_hid_decode(const hv_hid_layout *l, const uint8_t *data,
                  uint32_t size, hv_pointer_report *out)
{
    uint32_t id = 0, axes = 0;
    int relative = -1;
    if (!l || !data || !out || !size) return -1;
    if (l->numbered) { id = *data++; --size; }
    if (!l->report_bits[id] || size < (l->report_bits[id]+7u)/8u) return -1;
    *out = (hv_pointer_report){0};
    for (uint32_t i = 0; i < l->count; ++i) {
        const hv_hid_field *f = &l->fields[i];
        uint32_t raw = 0;
        int32_t value;
        if (f->id != id) continue;
        for (uint32_t j = 0; j < f->bits; ++j)
            raw |= (uint32_t)((data[(f->bit+j)/8] >> ((f->bit+j)%8)) & 1) << j;
        if (f->minimum < 0 && f->bits < 32 && (raw & (1u << (f->bits-1))))
            raw |= ~((1u << f->bits)-1u);
        value = (int32_t)raw;
        if (value < f->minimum) value = f->minimum;
        if (value > f->maximum) value = f->maximum;
        if (f->page == 9) { if (value) out->buttons |= (uint8_t)(1u << (f->usage-1)); continue; }
        if (f->usage == 0x38) { out->wheel = value; continue; }
        if (relative >= 0 && relative != f->relative) return -1;
        relative = f->relative;
        if (!f->relative)
            value = (int32_t)(((int64_t)value-f->minimum)*32767 / ((int64_t)f->maximum-f->minimum));
        if (f->usage == 0x30) { out->x = value; axes |= 1; }
        if (f->usage == 0x31) { out->y = value; axes |= 2; }
    }
    out->absolute = relative == 0;
    return axes == 3 ? 0 : -1;
}
