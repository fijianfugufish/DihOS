#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/system/cpu_info.c"

static unsigned char madt[512], gtdt[80], xsdt[52];
static cpu_acpi_rsdp rsdp;
static void put64(unsigned char *p, uint64_t v) { memcpy(p,&v,8); }
static void put32(unsigned char *p, uint32_t v) { memcpy(p,&v,4); }
static void setup(void) {
    memset(madt,0,sizeof(madt));memset(gtdt,0,sizeof(gtdt));memset(xsdt,0,sizeof(xsdt));memset(&rsdp,0,sizeof(rsdp));
    memcpy(rsdp.sig,"RSD PTR ",8);rsdp.xsdt_address=(uintptr_t)xsdt;
    memcpy(xsdt,"XSDT",4);put32(xsdt+4,sizeof(xsdt));
    put64(xsdt+36,(uintptr_t)madt);put64(xsdt+44,(uintptr_t)gtdt);
    memcpy(madt,"APIC",4);memcpy(gtdt,"GTDT",4);put32(gtdt+4,72);put32(gtdt+64,27);put32(gtdt+68,4);
    unsigned char *p=madt+44;
    p[0]=12;p[1]=24;put64(p+8,0x8000000);p[20]=3;p+=24;
    p[0]=14;p[1]=16;put64(p+4,0x80a0000);put32(p+12,0x40000);p+=16;
    cpu_acpi_gicc_min cpu={0};cpu.type=11;cpu.length=sizeof(cpu);cpu.flags=1;cpu.mpidr=0;cpu.gicr_base_address=0x80ce000;
    memcpy(p,&cpu,sizeof(cpu));p+=sizeof(cpu);put32(madt+4,(uint32_t)(p-madt));
}
int main(void) {
    cpu_info_snapshot info;
    setup();cpu_info_init((uintptr_t)&rsdp);cpu_info_get(&info);
    assert(info.initialized && info.gic_version==3 && info.gic_distributor==0x8000000);
    assert(info.gic_redistributor==0x80ce000 && info.gic_redistributor_bytes==0x20000);
    assert(info.virtual_timer_irq==27 && info.virtual_timer_flags==4 && info.enabled_count==1);
    put64(madt+44+24+16+offsetof(cpu_acpi_gicc_min,gicr_base_address),0);
    cpu_info_init((uintptr_t)&rsdp);cpu_info_get(&info);
    assert(info.gic_redistributor==0x80a0000 && info.gic_redistributor_bytes==0x40000);
    put32(gtdt+4,71);madt[45]=1;
    cpu_info_init((uintptr_t)&rsdp);cpu_info_get(&info);
    assert(!info.virtual_timer_irq && !info.gic_distributor && !info.core_count);
    setup();put32(madt+4,44+23);cpu_info_init((uintptr_t)&rsdp);cpu_info_get(&info);
    assert(!info.gic_distributor);
    cpu_info_init(0);cpu_info_get(&info);assert(info.initialized && !info.acpi_madt_found);
    puts("PASS: ACPI GIC/GTDT discovery, boot-core preference, region fallback and truncated tables");
    return 0;
}
