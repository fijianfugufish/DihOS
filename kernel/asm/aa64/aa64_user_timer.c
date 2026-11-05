#include "asm/aa64_user_slice.h"
int aa64_user_timer_validate(const aa64_user_timer_config *c)
{
    if(!c)return -110;
    if(!c->distributor)return -111;
    if(!c->redistributor)return -112;
    if(c->irq<16 || c->irq>31)return -113;
    if(c->redistributor_bytes<0x20000 || c->redistributor_bytes>0x1000000)return -114;
    if(c->distributor&0xffff)return -115;
    /* Use the ACPI-provided register base, not a rounded 64 KiB address.
     * Hyper-V can place this window on a 4 KiB page boundary. */
    if(c->redistributor&0xfff)return -116;
    if(c->redistributor>UINT64_MAX-c->redistributor_bytes)return -117;
    return 0;
}
#if defined(__aarch64__)
#include "memory/aarch64_mmu_map.h"
#include "asm/asm.h"

static uint64_t timer_core, timer_frequency, old_pmr, requested_ticks;
static uint32_t timer_ready, timer_irq, armed;
static aa64_user_timer_diagnostic diagnostic;
void aa64_user_timer_get_diagnostic(aa64_user_timer_diagnostic *out){if(out)*out=diagnostic;}
static uint64_t core_id(void) { uint64_t v; __asm__ volatile("mrs %0,mpidr_el1":"=r"(v));return v&0xff00ffffffull; }
static uint64_t counter(void) { uint64_t v; __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(v));return v; }
static int read32(uint64_t p,uint32_t *v) { return asm_aa64_try_read32(p,v); }
static int write32(uint64_t p,uint32_t v) { return asm_aa64_try_write32(p,v); }
static int wait_clear(uint64_t p,uint32_t mask) {
    uint64_t end=counter()+timer_frequency/100;uint32_t v;
    do { if(read32(p,&v))return -1;if(!(v&mask))return 0; } while(counter()<end);
    return -1;
}
static int timer_init(const aa64_user_timer_config *config)
{
    uint64_t sre,ctl,mpidr=core_id(),rd=0;uint32_t value,lo,hi;
    if(timer_ready)return timer_core==mpidr?0:-118;
    int validation=aa64_user_timer_validate(config);if(validation)return validation;
    __asm__ volatile("mrs %0,id_aa64pfr0_el1":"=r"(sre));
    if(!((sre>>24)&15))return -2;
    __asm__ volatile("mrs %0,icc_sre_el1":"=r"(sre));
    if(!(sre&1))return -2; /* firmware must have enabled the system interface */
    __asm__ volatile("mrs %0,cntv_ctl_el0":"=r"(ctl));
    if(ctl&1)return -3; /* do not steal an existing timer */
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(timer_frequency));
    if(timer_frequency<1000 || timer_frequency>10000000000ull)return -2;
    if(aarch64_mmu_map_device_identity(config->distributor,0x10000) ||
       aarch64_mmu_map_device_identity(config->redistributor,config->redistributor_bytes))return -4;
    if(read32(config->distributor+0xffe8,&value) || ((value>>4)&15)<3 || ((value>>4)&15)>4)return -2;
    uint32_t affinity=(uint32_t)(mpidr&0xffffff)|((uint32_t)(mpidr>>32)<<24);
    for(uint64_t offset=0;offset+0x20000<=config->redistributor_bytes;) {
        uint64_t p=config->redistributor+offset;
        if(read32(p+8,&lo)||read32(p+12,&hi))return -4;
        if(hi==affinity){rd=p;break;}
        if(lo&16)break;
        offset+=(lo&2)?0x40000:0x20000;
    }
    if(!rd)return -5;
    if(read32(rd+0x14,&value)||write32(rd+0x14,value&~2u)||wait_clear(rd+0x14,4))return -4;
    uint64_t sgi=rd+0x10000;uint32_t bit=1u<<config->irq;
    if(read32(sgi+0x100,&value)|| (value&bit))return -3;
    if(read32(sgi+0x80,&value)||write32(sgi+0x80,value|bit))return -4;
    if(read32(sgi+0xd00,&value)||write32(sgi+0xd00,value&~bit))return -4;
    uint32_t shift=(config->irq%16)*2;
    uint32_t trigger=(config->flags&1u)?2u:0u;
    if(read32(sgi+0xc04,&value)||write32(sgi+0xc04,(value&~(3u<<shift))|(trigger<<shift)))return -4;
    uint64_t priority=sgi+0x400+(config->irq&~3u);shift=(config->irq%4)*8;
    if(read32(priority,&value)||write32(priority,(value&~(255u<<shift))|(0x10u<<shift)))return -4;
    if(write32(sgi+0x280,bit)||write32(sgi+0x100,bit)||wait_clear(rd,1u<<3))return -4;
    if(read32(config->distributor,&value)||write32(config->distributor,value|0x13u)||wait_clear(config->distributor,1u<<31))return -4;
    ctl=1;__asm__ volatile("msr icc_igrpen1_el1,%0; isb"::"r"(ctl):"memory");
    timer_core=mpidr;timer_irq=config->irq;timer_ready=1;return 0;
}
int aa64_user_timer_init(const aa64_user_timer_config *config)
{
    diagnostic=(aa64_user_timer_diagnostic){0};
    if(config)diagnostic.config=*config;
    diagnostic.current_core=core_id();diagnostic.initialized_core=timer_core;diagnostic.was_ready=timer_ready;
    diagnostic.result=timer_init(config);
    return diagnostic.result;
}
int aa64_user_timer_arm(uint32_t microseconds)
{
    uint64_t ctl,daif;
    if(!timer_ready || armed || core_id()!=timer_core || microseconds<AA64_USER_TIMER_MIN_US || microseconds>AA64_USER_TIMER_MAX_US)return -1;
    __asm__ volatile("mrs %0,daif":"=r"(daif));if(!(daif&0x80))return -2;
    __asm__ volatile("mrs %0,cntv_ctl_el0":"=r"(ctl));if(ctl&1)return -2;
    requested_ticks=(timer_frequency*microseconds+999999)/1000000;
    __asm__ volatile("mrs %0,icc_pmr_el1":"=r"(old_pmr));
    uint64_t pmr=0x20;
    /* Entry assembly starts the timer immediately before ERET, after the
     * translation switch and register restore, not during kernel setup. */
    __asm__ volatile("msr icc_pmr_el1,%0; isb"::"r"(pmr):"memory");
    armed=1;return 0;
}
uint64_t aa64_user_timer_ticks(void){return requested_ticks;}
void aa64_user_timer_disarm(void)
{
    if(!armed)return;
    uint64_t zero=0;__asm__ volatile("msr cntv_ctl_el0,%0; isb; msr icc_pmr_el1,%1; isb"::"r"(zero),"r"(old_pmr):"memory");armed=0;
}
int aa64_user_timer_ack(void)
{
    uint64_t id,ctl;__asm__ volatile("mrs %0,icc_iar1_el1; isb":"=r"(id));
    if((id&0xffffff)>=1020)return 0;
    /* Deassert the timer before deactivating its level-triggered interrupt. */
    if(id==timer_irq){uint64_t zero=0;__asm__ volatile("msr cntv_ctl_el0,%0; isb"::"r"(zero):"memory");}
    __asm__ volatile("msr icc_eoir1_el1,%0; isb; mrs %1,icc_ctlr_el1":"=r"(id),"=r"(ctl):"0"(id):"memory");
    if(ctl&2)__asm__ volatile("msr icc_dir_el1,%0; isb"::"r"(id):"memory");
    return id==timer_irq?1:-1;
}
#else
void aa64_user_timer_get_diagnostic(aa64_user_timer_diagnostic *out){if(out)*out=(aa64_user_timer_diagnostic){0};}
int aa64_user_timer_init(const aa64_user_timer_config *c){(void)c;return -1;}
int aa64_user_timer_arm(uint32_t us){(void)us;return -1;}
uint64_t aa64_user_timer_ticks(void){return 0;}
void aa64_user_timer_disarm(void){}
int aa64_user_timer_ack(void){return -1;}
#endif
