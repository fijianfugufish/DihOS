#pragma once

#include <stdint.h>

/* Ask Qualcomm secure firmware to permit GPU-owned updates in one SMMUv2
 * context bank's aperture.  `out_available` distinguishes an unsupported
 * service from a denied call; `out_status` is secure firmware's x0 result. */
int gpu_qcom_scm_open_gpu_smmu_aperture(uint32_t context_bank,
                                        uint64_t *out_available,
                                        uint64_t *out_status,
                                        uint64_t *out_esr);
