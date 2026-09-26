#pragma once

#include <stdint.h>

/* Optional, read-only presentation clock for X1E80100 DPU 9.2.  It never
 * changes a display register: UEFI/GOP remains the scanout owner and the
 * universal fallback. */
int qcom_dpu_present_init(uint32_t width, uint32_t height,
                          uint64_t gop_phys, uint32_t gop_pitch,
                          uint64_t candidate_phys);
uint8_t qcom_dpu_present_ready(void);
uint8_t qcom_dpu_present_frame_boundary(void);
uint8_t qcom_dpu_present_takeover_ready(void);
int qcom_dpu_present_flip(uint64_t scanout_phys);
void qcom_dpu_present_restore_gop(void);
