#pragma once

#include <stdint.h>

typedef struct gpu_scm_result {
    uint64_t transport, result, esr;
} gpu_scm_result;

/* Standard 64-bit PAS calls. No automatic retry of state-changing operations.
 * Interrupted/busy/wait-queue responses fail closed and retain caller buffers. */
int gpu_qcom_pas_available(gpu_scm_result *out);
int gpu_qcom_pas_supported(uint32_t peripheral, gpu_scm_result *out);
int gpu_qcom_pas_init(uint32_t peripheral, uint64_t metadata_phys, gpu_scm_result *out);
int gpu_qcom_pas_memory(uint32_t peripheral, uint64_t phys, uint64_t bytes, gpu_scm_result *out);
int gpu_qcom_pas_start(uint32_t peripheral, gpu_scm_result *out);

/* Ask Qualcomm secure firmware to permit GPU-owned updates in one SMMUv2
 * context bank's aperture.  `out_available` distinguishes an unsupported
 * service from a denied call; `out_status` is the nonzero transport x0 or,
 * after successful transport, the service result x1. */
int gpu_qcom_scm_open_gpu_smmu_aperture(uint32_t context_bank,
                                        uint64_t *out_available,
                                        uint64_t *out_status,
                                        uint64_t *out_esr);
