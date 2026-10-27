#include "net/knet_link.h"
#include "usb/usb_ethernet.h"
#include "hyperv/hyperv_net.h"
static int use_hyperv;
static void (*checkpoint_fn)(void);
void knet_link_set_checkpoint(void (*checkpoint)(void)) { checkpoint_fn=checkpoint; }
void knet_link_checkpoint(void) { if (checkpoint_fn) checkpoint_fn(); }
static void text_copy(char *dst, uint32_t cap, const char *src)
{ uint32_t i=0; for (; i+1<cap && src[i]; ++i) dst[i]=src[i]; dst[i]=0; }
void knet_link_init(int hyperv) { use_hyperv=hyperv; if (hyperv) (void)hyperv_net_init(); }
int knet_link_is_hyperv(void) { return use_hyperv; }
uint32_t knet_link_online(void) { if (use_hyperv) { hyperv_net_poll(); return (uint32_t)hyperv_net_online(); } return usb_ethernet_online(); }
int knet_link_get_mac(uint8_t mac[6]) { return use_hyperv ? hyperv_net_mac(mac) : usb_ethernet_get_mac(mac); }
int knet_link_prepare_receive(void) { return use_hyperv ? (hyperv_net_online() ? 0 : -1) : usb_ethernet_prepare_receive(); }
int knet_link_send_frame(const void *p, uint32_t n) { knet_link_checkpoint(); return use_hyperv ? hyperv_net_send(p,n) : usb_ethernet_send_frame(p,n); }
int knet_link_recv_frame(void *p, uint32_t cap, uint32_t *n) { knet_link_checkpoint(); return use_hyperv ? hyperv_net_receive(p,cap,n) : usb_ethernet_recv_frame(p,cap,n); }
uint32_t knet_link_pending_frames(void) { return use_hyperv ? hyperv_net_pending() : usb_ethernet_pending_frames(); }
void knet_link_discard_pending_frames(void) { if (use_hyperv) hyperv_net_discard(); else usb_ethernet_discard_pending_frames(); }
void knet_link_get_status(knet_link_status *s)
{
    if (!s) return;
    *s=(knet_link_status){0};
    if (use_hyperv) {
        s->online=(uint8_t)hyperv_net_online(); s->mtu=1500; (void)hyperv_net_mac(s->mac);
        text_copy(s->driver,sizeof(s->driver),"netvsc");
        text_copy(s->detail,sizeof(s->detail),hyperv_net_detail());
    } else {
        usb_ethernet_status usb; usb_ethernet_get_status(&usb);
        s->online=usb.online; s->mtu=usb.mtu;
        for (uint32_t i=0;i<6;++i) s->mac[i]=usb.mac[i];
        text_copy(s->driver,sizeof(s->driver),usb.driver);
        text_copy(s->detail,sizeof(s->detail),usb.detail);
    }
}

void knet_link_diag_print(void) { if (use_hyperv) hyperv_net_diag_print(); }
