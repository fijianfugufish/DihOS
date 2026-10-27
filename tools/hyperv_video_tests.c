#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/hyperv/hyperv_video.c"
static uint8_t replies[16][1024];
static uint32_t lengths[16], read_at, queued, dirty_count, cursor_count;
static uint64_t ticks, rendered;
static int reject_version,panic_testing,panic_send_failure;
static uint32_t receive_calls;
uint64_t dihos_time_monotonic_ms(void) { return ++ticks; }
uint64_t kgfx_rendered_frame_count(void) { return rendered; }
uint32_t kgfx_cpu_present_damage(gpu_render_rect *out,uint32_t capacity)
{ assert(capacity>=2);out[0]=(gpu_render_rect){10,10,32,32};out[1]=(gpu_render_rect){1800,1000,32,32};return 2; }
void asm_relax(void) {}
void terminal_warn(const char *s) { (void)s; }
void terminal_success(const char *s) { (void)s; }
int vmbus_device_open(uint32_t d) { assert(d==1);return 0; }
static uint8_t *reply(uint32_t type,uint32_t size)
{
    assert(queued<16);
    uint32_t slot=(read_at+queued++)%16;
    uint8_t *p=replies[slot];memset(p,0,1024);
    lengths[slot]=(32+size+7)&~7u;
    hv_put16(p,6);hv_put16(p+2,2);hv_put16(p+4,(uint16_t)(lengths[slot]/8));
    hv_put32(p+16,1);hv_put32(p+20,8+size);hv_put32(p+24,type);hv_put32(p+28,8+size);
    return p+32;
}
int vmbus_device_send(uint32_t d,uint16_t type,uint64_t tid,const void *data,uint32_t bytes,const void *gpa,uint32_t n)
{
    (void)tid;(void)gpa;(void)n;assert(d==1 && type==6 && bytes>=16);
    const uint8_t *p=data;uint32_t kind=hv_get32(p+8);
    if (kind==1) { uint8_t *r=reply(2,6);hv_put32(r,0x50003);r[4]=(uint8_t)!reject_version;r[5]=1; }
    if (kind==13) {
        uint8_t *r=reply(14,139);r[128]=2;
        hv_put16(r+131,1024);hv_put16(r+133,768);
        hv_put16(r+135,1920);hv_put16(r+137,1080);
    }
    if (kind==3) { uint8_t *r=reply(4,8);memcpy(r,p+16,8); }
    if (kind==5) { reply(6,8); }
    if (kind==10) {
        if(panic_testing){
            assert(bytes==34 && p[17]==1);
            assert(hv_get32(p+18)==0 && hv_get32(p+22)==0);
            assert(hv_get32(p+26)==width && hv_get32(p+30)==height);
            ++dirty_count;return panic_send_failure?-1:0;
        }
        if (!pending) {
            assert(p[17]==2); /* distant updates stay disjoint, not full-screen */
            assert(hv_get32(p+26)==42 && hv_get32(p+30)==42);
        }
        ++dirty_count;
    }
    if (kind==8) ++cursor_count;
    return 0;
}
int vmbus_device_receive(uint32_t d,void *out,uint32_t cap,uint32_t *n)
{
    assert(d==1);++receive_calls;
    if (!queued) { *n=0;return 1; }
    *n=lengths[read_at];assert(cap>=*n);memcpy(out,replies[read_at],*n);
    read_at=(read_at+1)%16;--queued;return 0;
}
int main(void)
{
    boot_info bi={0};bi.fb=(boot_fb){.fb_base=0xf0000000,.fb_size=8*1024*1024,.width=1024,.height=768,.pitch=4096};
    assert(!hyperv_video_init(&bi));assert(bi.fb.width==1920 && bi.fb.height==1080 && bi.fb.pitch==7680 && bi.fb.fb_base==0xf0000000);
    assert(cursor_count==1);rendered=1;hyperv_video_present();assert(dirty_count==1);
    ticks+=20;hyperv_video_present();assert(dirty_count==1);
    reply(9,4);ticks+=20;hyperv_video_present();assert(cursor_count==2 && dirty_count==2);
    ++rendered;ticks+=20;hyperv_video_present();assert(dirty_count==3);
    /* Startup progress bypasses the throttle, including direct framebuffer
     * writes with no new kgfx frame. It must not notify while offline. */
    uint32_t before=dirty_count;
    hyperv_video_present_now(); assert(dirty_count==before+1);
    hyperv_video_present_now(); assert(dirty_count==before+2);
    panic_testing=1;
    uint64_t saved_ticks=ticks;uint32_t saved_receive=receive_calls,saved_pending=pending;
    hyperv_video_panic_present();assert(dirty_count==before+3);
    panic_send_failure=1;hyperv_video_panic_present();assert(dirty_count==before+4);
    assert(ticks==saved_ticks && receive_calls==saved_receive && pending==saved_pending);
    online=0;hyperv_video_panic_present();assert(dirty_count==before+4);panic_testing=0;
    hyperv_video_present_now(); assert(dirty_count==before+4);
    bi.fb=(boot_fb){.fb_base=0xf0000000,.fb_size=1024*768*4,.width=1024,.height=768,.pitch=4096};
    assert(!hyperv_video_init(&bi) && bi.fb.width==1024); /* never exceeds reserved aperture */
    online=0;reject_version=1;boot_fb original=bi.fb;
    assert(hyperv_video_init(&bi)<0 && !memcmp(&original,&bi.fb,sizeof(original)));
    puts("PASS: synthetic video negotiation, 1080p mode, VRAM capacity, dirty notifications, bounded panic notification without receive/wait, reconnect cursor hiding, rejected-version GOP fallback");
    return 0;
}
