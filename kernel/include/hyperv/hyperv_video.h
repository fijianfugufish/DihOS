#pragma once
#include "bootinfo.h"
int hyperv_video_init(boot_info *bi);
void hyperv_video_present(void);
/* Boot progress must reach VMConnect even before the frame loop. */
void hyperv_video_present_now(void);
int hyperv_video_online(void);

/* Best-effort full damage notification, with no receive loop or wait. */
void hyperv_video_panic_present(void);
