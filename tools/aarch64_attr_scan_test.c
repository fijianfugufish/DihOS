#include <stdio.h>
#include <string.h>
#include "memory/aarch64_attr_scan.h"
static uint64_t root[512], child[512];
static unsigned reads, limit=512;
static int read_desc(uint64_t a,uint64_t *v)
{
    ++reads;
    if (a>=0x1000 && a<0x1000+limit*8) { *v=root[(a-0x1000)/8]; return 0; }
    if (a>=0x2000 && a<0x3000) { *v=child[(a-0x2000)/8]; return 0; }
    return -1;
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d\n",__LINE__); return 1; } } while(0)
int main(void)
{
    unsigned level,n;
    CHECK(!a64_attr_root_shape(28,&level,&n) && level==1 && n==64);
    CHECK(!a64_attr_root_shape(16,&level,&n) && level==0 && n==512);
    CHECK(!a64_attr_root_shape(32,&level,&n) && level==1 && n==4);
    CHECK(a64_attr_root_shape(0,&level,&n)!=0);
    CHECK(a64_attr_root_shape(52,&level,&n)!=0);
    uint32_t used=0,budget=16;
    limit=4; root[0]=1|(3<<2); root[3]=1|(2<<2);
    /* Garbage after the truncated root must not be interpreted as a table. */
    root[4]=0x3d99199fb003ull;
    CHECK(!a64_attr_scan(0x1000,1,4,&used,&budget,read_desc));
    CHECK(reads==4 && used==12);
    root[1]=0x2003; child[0]=1|(6<<2); used=0; budget=16; reads=0;
    CHECK(!a64_attr_scan(0x1000,1,4,&used,&budget,read_desc));
    CHECK(reads==516 && used==76);
    root[1]=0x3d99199fb003ull; budget=16;
    CHECK(a64_attr_scan(0x1000,1,4,&used,&budget,read_desc)!=0);
    root[1]=0x2003; budget=1;
    CHECK(a64_attr_scan(0x1000,1,4,&used,&budget,read_desc)!=0);
    budget=16; root[0]=1;
    CHECK(a64_attr_scan(0x1000,0,4,&used,&budget,read_desc)!=0);
    budget=16;
    CHECK(a64_attr_scan(0x1000,3,4,&used,&budget,read_desc)!=0);
    puts("PASS: root sizes, truncated-root garbage, child scan, inaccessible table, budget, invalid leaves");
    return 0;
}
