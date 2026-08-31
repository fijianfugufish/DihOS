#include "gpu/gpu_qcom_scm.h"
#include "asm/asm.h"

/* ARM SMCCC standard, 64-bit SIP calls used by Qualcomm SCM.  Keep this
 * transport separate from the Adreno profile: another Qualcomm peripheral
 * can reuse it without inheriting GPU register policy. */
#define QCOM_SCM_OWNER_SIP              2u
#define QCOM_SCM_SVC_INFO               0x06u
#define QCOM_SCM_INFO_IS_CALL_AVAILABLE 0x01u
#define QCOM_SCM_SVC_MP                 0x0cu
#define QCOM_SCM_MP_CP_SMMU_APERTURE    0x1bu
/* ARM SMCCC encodes Qualcomm SCM calls as fast, 64-bit SiP calls.  The
 * previous value (0x42000000) accidentally selected the yielding/standard
 * convention; secure firmware accepts the SMC instruction but reports every
 * service as unavailable.  Qualcomm's qcom_scm transport uses the fast-call
 * convention for both INFO.IS_CALL_AVAILABLE and MP.CP_SMMU_APERTURE. */
#define QCOM_SCM_FAST_64_SIP            0xc2000000u

static uint32_t qcom_scm_fnid(uint32_t service, uint32_t command)
{
    return ((service & 0xffu) << 8) | (command & 0xffu);
}

static uint64_t qcom_scm_sip_function(uint32_t service, uint32_t command)
{
    return QCOM_SCM_FAST_64_SIP |
           ((uint32_t)QCOM_SCM_OWNER_SIP << 24) |
           qcom_scm_fnid(service, command);
}

int gpu_qcom_scm_open_gpu_smmu_aperture(uint32_t context_bank,
                                        uint64_t *out_available,
                                        uint64_t *out_status,
                                        uint64_t *out_esr)
{
    uint64_t x0 = qcom_scm_sip_function(QCOM_SCM_SVC_INFO,
                                        QCOM_SCM_INFO_IS_CALL_AVAILABLE);
    uint64_t x1 = 1u; /* QCOM_SCM_ARGS(1) */
    uint64_t x2 = ((uint32_t)QCOM_SCM_OWNER_SIP << 24) |
                  qcom_scm_fnid(QCOM_SCM_SVC_MP,
                                QCOM_SCM_MP_CP_SMMU_APERTURE);
    uint64_t x3 = 0u;
    uint64_t x4 = 0u;
    uint64_t x5 = 0u;
    uint64_t esr = 0u;

    if (out_available) *out_available = 0u;
    if (out_status) *out_status = 0u;
    if (out_esr) *out_esr = 0u;
    if (context_bank > 0xffu)
        return -1;

    if (asm_aa64_try_smc6(0u, &x0, &x1, &x2, &x3, &x4, &x5, &esr) != 0)
    {
        if (out_esr) *out_esr = esr;
        return -2;
    }
    if (!x0)
        return 1;
    if (out_available) *out_available = x0;

    x0 = qcom_scm_sip_function(QCOM_SCM_SVC_MP,
                                QCOM_SCM_MP_CP_SMMU_APERTURE);
    x1 = 4u; /* QCOM_SCM_ARGS(4) */
    x2 = 0xffff0000u | context_bank;
    x3 = 0xffffffffu;
    x4 = 0xffffffffu;
    x5 = 0xffffffffu;
    esr = 0u;
    if (asm_aa64_try_smc6(0u, &x0, &x1, &x2, &x3, &x4, &x5, &esr) != 0)
    {
        if (out_esr) *out_esr = esr;
        return -3;
    }
    if (out_status) *out_status = x0;
    return x0 ? -4 : 0;
}
