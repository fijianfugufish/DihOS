#pragma once
#include <stdint.h>
typedef struct knet_link_status { uint8_t online; uint16_t mtu; uint8_t mac[6]; char driver[24], detail[224]; } knet_link_status;
void knet_link_init(int hyperv);
uint32_t knet_link_online(void);
void knet_link_get_status(knet_link_status *status);
int knet_link_get_mac(uint8_t mac[6]);
int knet_link_prepare_receive(void);
int knet_link_send_frame(const void *frame, uint32_t bytes);
int knet_link_recv_frame(void *frame, uint32_t capacity, uint32_t *bytes);
uint32_t knet_link_pending_frames(void);
void knet_link_discard_pending_frames(void);
int knet_link_is_hyperv(void);

void knet_link_diag_print(void);

/* Core-0 continuation checkpoint, invoked outside USB controller locks. */
void knet_link_set_checkpoint(void (*checkpoint)(void));
void knet_link_checkpoint(void);
