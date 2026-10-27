#pragma once
#include "bootinfo.h"
int hyperv_video_init(boot_info *bi);
void hyperv_video_present(void);
/* Boot progress must reach VMConnect even before the frame loop. */
void hyperv_video_present_now(void);
int hyperv_video_online(void);
