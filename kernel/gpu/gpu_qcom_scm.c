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
/* Fast 64-bit SiP convention for this boot-time wrapper. Upstream also
 * supports standard calls for sleepable operations. Do not infer convention
 * support from x0 alone: x0 is transport status, and x1 is the result. */
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

static int pas_call(uint32_t service, uint32_t command, uint32_t info,
                    uint64_t a, uint64_t b, uint64_t c, gpu_scm_result *out)
{
    uint64_t x0 = 0x42000000u | qcom_scm_fnid(service, command);
    uint64_t x1 = info, x2 = a, x3 = b, x4 = c, x5 = 0, esr = 0;
    int rc = asm_aa64_try_smc6(0, &x0, &x1, &x2, &x3, &x4, &x5, &esr);
    if (out) *out = (gpu_scm_result){x0, x1, esr};
    return rc ? -2 : x0 ? -3 : 0;
}

int gpu_qcom_pas_available(gpu_scm_result *out)
{
    gpu_scm_result r;
    int rc = pas_call(6, 1, 1, 0x02000207u, 0, 0, &r);
    if (out) *out = r;
    return rc ? rc : r.result == 1 ? 0 : -4;
}

int gpu_qcom_pas_supported(uint32_t peripheral, gpu_scm_result *out)
{
    gpu_scm_result r;
    int rc = pas_call(2, 7, 1, peripheral, 0, 0, &r);
    if (out) *out = r;
    return rc ? rc : r.result == 1 ? 0 : -4;
}

int gpu_qcom_pas_init(uint32_t peripheral, uint64_t metadata_phys, gpu_scm_result *out)
{
    gpu_scm_result r;
    if (out) *out = (gpu_scm_result){0};
    if (!metadata_phys || (metadata_phys & 4095u)) return -1;
    /* Two arguments, second is a read/write physical buffer (2 << 6). */
    int rc = pas_call(2, 1, 0x82u, peripheral, metadata_phys, 0, &r);
    if (out) *out = r;
    return rc ? rc : r.result ? -4 : 0;
}

int gpu_qcom_pas_memory(uint32_t peripheral, uint64_t phys, uint64_t bytes, gpu_scm_result *out)
{
    gpu_scm_result r;
    if (out) *out = (gpu_scm_result){0};
    if (!phys || !bytes || ((phys | bytes) & 4095u) || phys + bytes < phys) return -1;
    int rc = pas_call(2, 2, 3, peripheral, phys, bytes, &r);
    if (out) *out = r;
    return rc ? rc : r.result ? -4 : 0;
}

int gpu_qcom_pas_start(uint32_t peripheral, gpu_scm_result *out)
{
    gpu_scm_result r;
    int rc = pas_call(2, 5, 1, peripheral, 0, 0, &r);
    if (out) *out = r;
    return rc ? rc : r.result ? -4 : 0;
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
    /* SCM separates transport status (x0) from service result (x1).
     * Success is x0 == 0; it must not be mistaken for unavailable. */
    if (out_status) *out_status = x0;
    if (x0 != 0u)
        return -4;
    if (out_available) *out_available = x1;
    if (x1 != 1u)
        return 1;

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
    if (out_status) *out_status = x0 ? x0 : x1;
    return (x0 || x1) ? -4 : 0;
}
