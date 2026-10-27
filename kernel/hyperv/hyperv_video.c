#include "hyperv/hyperv_video.h"
#include "hyperv/hv_wire.h"
#include "hyperv/vmbus.h"
#include "kwrappers/kgfx.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"

/* Reuse only the firmware-reserved VRAM aperture. Allocating ordinary RAM or
 * extending beyond GOP's reported aperture is not a safe substitute for VRAM. */
static uint32_t online, width, height, pending;
static uint64_t transaction, last_frame, next_present;
static gpu_render_rect damage[24];
static uint32_t damage_count;
static int send_message(uint32_t type, const uint8_t *body, uint32_t bytes)
{
    uint8_t msg[512]={0};
    if (bytes > sizeof(msg)-16) return -1;
    hv_put32(msg,1); hv_put32(msg+4,8+bytes); hv_put32(msg+8,type); hv_put32(msg+12,8+bytes);
    for (uint32_t i=0;i<bytes;++i) msg[16+i]=body[i];
    return vmbus_device_send(VMBUS_DEVICE_VIDEO,6,++transaction,msg,16+bytes,0,0);
}
static void hide_host_cursor(void)
{
    uint8_t pos[10]={1}, shape[22]={0};
    shape[0]=255; shape[1]=1;
    hv_put32(shape+2,1); hv_put32(shape+6,1);
    /* A transparent 1x1 ARGB shape suppresses VMConnect's extra pointer. */
    (void)send_message(7,pos,sizeof(pos));
    (void)send_message(8,shape,sizeof(shape));
}
static int receive_message(uint32_t *type, uint8_t *body, uint32_t capacity, uint32_t *size)
{
    uint8_t packet[1024]; uint32_t bytes=0;
    int rc=vmbus_device_receive(VMBUS_DEVICE_VIDEO,packet,sizeof(packet),&bytes);
    if (rc) return rc;
    uint32_t off=hv_get16(packet+2)*8u;
    if (hv_get16(packet) != 6) return 1;
    if (!hv_range(bytes,off,16)) return -1;
    uint8_t *p=packet+off;
    uint32_t len=hv_get32(p+12);
    if (hv_get32(p) != 1 || len < 8 || len > bytes-off-8 || hv_get32(p+4) < len || len-8 > capacity) return -1;
    *type=hv_get32(p+8); *size=len-8;
    for (uint32_t i=0;i<*size;++i) body[i]=p[16+i];
    return 0;
}
static int wait_message(uint32_t expected, uint8_t *body, uint32_t cap, uint32_t *size)
{
    uint64_t deadline=dihos_time_monotonic_ms()+2000;
    while (dihos_time_monotonic_ms() < deadline) {
        uint32_t type=0;
        int rc=receive_message(&type,body,cap,size);
        if (rc < 0) return -1;
        if (!rc && type == expected) return 0;
        if (!rc && type == 9) pending=1;
        asm_relax();
    }
    return -1;
}
static int situation(uint32_t w, uint32_t h, uint32_t pitch)
{
    uint8_t body[27]={0};
    body[8]=1; body[9]=1; body[14]=32;
    hv_put32(body+15,w); hv_put32(body+19,h); hv_put32(body+23,pitch);
    return send_message(5,body,sizeof(body));
}
int hyperv_video_init(boot_info *bi)
{
    uint8_t body[512]={0}; uint32_t size=0, best_w=0, best_h=0;
    const char *reason="synthvid: version negotiation failed; retaining GOP";
    if (!bi || vmbus_device_open(VMBUS_DEVICE_VIDEO)) return -1;
    hv_put32(body,0x00050003); /* synthvid 3.5 */
    if (send_message(1,body,4) || wait_message(2,body,sizeof(body),&size) || size < 6 || !body[4]) goto fallback;
    reason="synthvid: supported-resolution query failed; retaining GOP";
    body[0]=64;
    if (send_message(13,body,1) || wait_message(14,body,sizeof(body),&size) || size < 131 ||
        body[128] > 64 || !hv_range(size,131,(uint32_t)body[128]*4)) goto fallback;
    for (uint32_t i=0;i<body[128];++i) {
        uint32_t w=hv_get16(body+131+i*4), h=hv_get16(body+133+i*4);
        if (w < 640 || h < 480 || w > 1920 || h > 1080 || (uint64_t)w*h*4 > bi->fb.fb_size) continue;
        if (w*h > best_w*best_h) { best_w=w; best_h=h; }
    }
    reason="synthvid: no supported mode fits the firmware VRAM aperture; retaining GOP";
    if (!best_w || !bi->fb.fb_base) goto fallback;
    reason="synthvid: VRAM registration was not acknowledged; retaining GOP";
    hv_put64(body,bi->fb.fb_base); body[8]=1; hv_put64(body+9,bi->fb.fb_base);
    if (send_message(3,body,17) || wait_message(4,body,sizeof(body),&size) || size < 8 || hv_get64(body) != bi->fb.fb_base) goto fallback;
    reason="synthvid: display situation was not acknowledged; restoring GOP mode";
    if (situation(best_w,best_h,best_w*4) || wait_message(6,body,sizeof(body),&size)) {
        (void)situation(bi->fb.width,bi->fb.height,bi->fb.pitch);
        goto fallback;
    }
    bi->fb.width=best_w; bi->fb.height=best_h; bi->fb.pitch=best_w*4;
    bi->fb.pixel_format=1; bi->fb.rmask=0xff0000; bi->fb.gmask=0xff00; bi->fb.bmask=0xff;
    width=best_w; height=best_h; online=1; pending=1;
    hide_host_cursor();
    terminal_success("synthvid: mode selected within firmware VRAM aperture");
    return 0;
fallback:
    terminal_warn(reason);
    return -1;
}
int hyperv_video_online(void) { return online != 0; }
void hyperv_video_present_now(void)
{
    if (!online) return;
    /* Boot diagnostics may change the framebuffer without advancing a frame.
     * Force a complete update and bypass the interactive 16 ms rate limit. */
    pending=1;
    next_present=0;
    hyperv_video_present();
}
void hyperv_video_present(void)
{
    if (!online) return;
    uint8_t body[512]; uint32_t size=0,type=0;
    for (uint32_t i=0;i<8;++i) {
        int rc=receive_message(&type,body,sizeof(body),&size);
        if (rc) break;
        if (type == 9) {
            hide_host_cursor();
            if (size >= 4 && body[3]) (void)situation(width,height,width*4);
            pending=1;
        }
    }
    uint64_t frame=kgfx_rendered_frame_count(), now=dihos_time_monotonic_ms();
    if (frame != last_frame) {
        gpu_render_rect current[24];
        uint32_t count=kgfx_cpu_present_damage(current,24);
        for (uint32_t i=0;i<count && !pending;++i) {
            if (damage_count==24) { pending=1; break; }
            damage[damage_count++]=current[i];
        }
        last_frame=frame;
    }
    if ((!pending && !damage_count) || now < next_present) return;
    uint8_t dirt[2+24*16]={0};
    uint32_t count=pending ? 1:damage_count;
    dirt[1]=(uint8_t)count;
    for (uint32_t i=0;i<count;++i) {
        gpu_render_rect r=pending ? (gpu_render_rect){0,0,width,height}:damage[i];
        hv_put32(dirt+2+i*16,r.x); hv_put32(dirt+6+i*16,r.y);
        hv_put32(dirt+10+i*16,r.x+r.width); hv_put32(dirt+14+i*16,r.y+r.height);
    }
    if (!send_message(10,dirt,2+count*16)) { pending=0; damage_count=0; }
    next_present=now+16;
}

void hyperv_video_panic_present(void)
{
    if (!online) return;
    /* Panic has halted the normal compositor loop. Send one full rectangle
     * directly, without polling channels, changing modes or waiting for host. */
    uint8_t dirt[18]={0};dirt[1]=1;
    hv_put32(dirt+10,width);hv_put32(dirt+14,height);
    (void)send_message(10,dirt,sizeof dirt);
}
