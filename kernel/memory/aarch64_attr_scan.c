#include "memory/aarch64_attr_scan.h"
#define TABLE_MASK 0x0000fffffffff000ull
int a64_attr_root_shape(unsigned tsz, unsigned *level, unsigned *entries)
{
    if (!level || !entries || tsz < 16 || tsz > 51) return -1;
    unsigned bits=64-tsz, levels=(bits-12+8)/9;
    *level=4-levels;
    *entries=1u << (bits-12-9*(levels-1));
    return 0;
}
int a64_attr_scan(uint64_t table, unsigned level, unsigned entries,
                  uint32_t *used, uint32_t *budget, a64_attr_read read)
{
    if (!table || (table&7) || level>3 || !entries || entries>512 ||
        !used || !budget || !*budget || !read || table+entries*8 < table) return -1;
    --*budget;
    for (unsigned i=0;i<entries;i++) {
        uint64_t d;
        if (read(table+i*8,&d)) return -2;
        if (!(d&1)) continue;
        if (level<3 && (d&2)) {
            if (a64_attr_scan(d&TABLE_MASK,level+1,512,used,budget,read)) return -2;
        } else {
            if (level==0 || (level==3 && !(d&2))) return -3;
            *used |= 1u << ((d>>2)&7);
        }
    }
    return 0;
}
