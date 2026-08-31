#include "gpu/gpu_smmu_v2.h"
#include "asm/asm.h"

#define SMMU_GR0_SCR0             0x0000u
#define SMMU_GR0_GFSR             0x0048u
#define SMMU_GR0_GFSYNR0          0x0050u
#define SMMU_GR0_GFSYNR1          0x0054u
#define SMMU_GR0_GFSYNR2          0x0058u
#define SMMU_GR0_ID0              0x0020u
#define SMMU_GR0_ID1              0x0024u
#define SMMU_GR0_ID2              0x0028u
#define SMMU_GR0_TLBIALLNSNH      0x0068u
#define SMMU_GR0_TLBSYNC          0x0070u
#define SMMU_GR0_TLBSTATUS        0x0074u
#define SMMU_GR0_SMR(n)           (0x0800u + ((n) << 2))
#define SMMU_GR0_S2CR(n)          (0x0c00u + ((n) << 2))

#define SMMU_SCR0_GCFGFIE         (1u << 5)
#define SMMU_SCR0_GCFGFRE         (1u << 4)
#define SMMU_SCR0_GFIE            (1u << 2)
#define SMMU_SCR0_GFRE            (1u << 1)
#define SMMU_SCR0_CLIENTPD        (1u << 0)
#define SMMU_SMR_VALID            (1u << 31)
#define SMMU_S2CR_TYPE_TRANS      0u
#define SMMU_S2CR_CBNDX_MASK      0xffu
#define SMMU_TLBSTATUS_ACTIVE     (1u << 0)

#define SMMU_GR1_CBAR(n)          (0x0000u + ((n) << 2))
#define SMMU_CBAR_S1_S2_BYPASS    (1u << 16)
#define SMMU_CBAR_MEMATTR_WB      (0xfu << 12)
#define SMMU_CBAR_BPSH_NSH        (3u << 8)
#define SMMU_GR1_CBA2R(n)         (0x0800u + ((n) << 2))
#define SMMU_CBA2R_VA64           (1u << 0)

#define SMMU_CB_SCTLR             0x0000u
#define SMMU_CB_TCR2              0x0010u
#define SMMU_CB_TTBR0             0x0020u
#define SMMU_CB_TCR               0x0030u
#define SMMU_CB_MAIR0             0x0038u
#define SMMU_CB_FSR               0x0058u
#define SMMU_CB_FAR_LO            0x0060u
#define SMMU_CB_FAR_HI            0x0064u
#define SMMU_CB_FSYNR0            0x0068u
#define SMMU_CB_FSYNR1            0x006cu
#define SMMU_CB_TLBIASID          0x0610u
#define SMMU_CB_TLBSYNC           0x07f0u
#define SMMU_CB_TLBSTATUS         0x07f4u

#define SMMU_SCTLR_S1_ASIDPNE     (1u << 12)
#define SMMU_SCTLR_HUPCF          (1u << 8)
#define SMMU_SCTLR_CFIE           (1u << 6)
#define SMMU_SCTLR_CFRE           (1u << 5)
#define SMMU_SCTLR_AFE            (1u << 2)
#define SMMU_SCTLR_TRE            (1u << 1)
#define SMMU_SCTLR_M              (1u << 0)
#define SMMU_TCR2_AS              (1u << 4)
#define SMMU_TCR_EAE              (1u << 31)
#define SMMU_TCR_EPD1             (1u << 23)
#define SMMU_TCR_SH0_INNER        (3u << 12)
#define SMMU_TCR_ORGN0_WB         (1u << 10)
#define SMMU_TCR_IRGN0_WB         (1u << 8)

#define SMMU_ID0_S1TS             (1u << 30)
#define SMMU_ID0_SMS              (1u << 27)
#define SMMU_ID2_PTFS_4K          (1u << 12)

#define SMMU_ATTACH_ASID          1u
#define SMMU_ATTACH_TLB_POLLS     100000u

static int write64(const gpu_mmio_window *window, uint32_t offset,
                   uint64_t value)
{
    /* Keep translation disabled until both halves of TTBR0 are present. */
    if (gpu_mmio_try_write32(window, offset + 4u, (uint32_t)(value >> 32)) != 0)
        return -1;
    return gpu_mmio_try_write32(window, offset, (uint32_t)value);
}

static int read64(const gpu_mmio_window *window, uint32_t offset,
                  uint64_t *out)
{
    uint32_t lo;
    uint32_t hi;

    if (!out || gpu_mmio_try_read32(window, offset, &lo) != 0 ||
        gpu_mmio_try_read32(window, offset + 4u, &hi) != 0)
        return -1;
    *out = (uint64_t)lo | ((uint64_t)hi << 32);
    return 0;
}

static int smmu_offset(const gpu_smmuv2_caps *caps, uint32_t page,
                       uint32_t register_offset, uint32_t *out)
{
    uint64_t offset;
    if (!caps || !out)
        return -1;
    offset = ((uint64_t)page << caps->page_shift) + register_offset;
    if (offset > 0xffffffffull)
        return -2;
    *out = (uint32_t)offset;
    return 0;
}

static int poll_clear(const gpu_mmio_window *window, uint32_t offset,
                      uint32_t mask)
{
    uint32_t value;
    for (uint32_t i = 0u; i < SMMU_ATTACH_TLB_POLLS; ++i)
    {
        if (gpu_mmio_try_read32(window, offset, &value) != 0)
            return -1;
        if ((value & mask) == 0u)
            return 0;
        asm_relax();
    }
    return -2;
}

int gpu_smmuv2_probe(const gpu_mmio_window *window, gpu_smmuv2_caps *out)
{
    uint32_t page_count_log2;

    if (!window || !out)
        return -1;
    *out = (gpu_smmuv2_caps){0};
    if (gpu_mmio_try_read32(window, SMMU_GR0_ID0, &out->id0) != 0 ||
        gpu_mmio_try_read32(window, SMMU_GR0_ID1, &out->id1) != 0 ||
        gpu_mmio_try_read32(window, SMMU_GR0_ID2, &out->id2) != 0)
        return -2;
    out->page_shift = (out->id1 & (1u << 31)) ? 16u : 12u;
    /* NUMPAGENDXB describes half the complete register aperture: GR0/GR1
     * occupy the first half and context-bank pages start after it. */
    page_count_log2 = ((out->id1 >> 28) & 0x7u) + 1u;
    if (page_count_log2 >= 31u)
        return -3;
    out->page_count = 1u << page_count_log2;
    out->context_bank_count = out->id1 & 0xffu;
    out->stream_group_count = out->id0 & 0xffu;
    if (!out->page_count || !out->context_bank_count ||
        !out->stream_group_count)
        return -4;
    return 0;
}

int gpu_smmuv2_read_context_fault(const gpu_mmio_window *window,
                                  const gpu_smmuv2_caps *caps,
                                  uint32_t context_bank,
                                  gpu_smmuv2_context_fault *out)
{
    uint32_t cb_offset;
    uint32_t far_lo;
    uint32_t far_hi;

    if (!window || !caps || !out || context_bank >= caps->context_bank_count ||
        smmu_offset(caps, caps->page_count + context_bank, 0u, &cb_offset) != 0 ||
        cb_offset > window->size_bytes ||
        window->size_bytes - cb_offset < SMMU_CB_FSYNR1 + 4u)
        return -1;
    *out = (gpu_smmuv2_context_fault){0};
    if (gpu_mmio_try_read32(window, cb_offset + SMMU_CB_FSR, &out->fsr) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_FAR_LO, &far_lo) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_FAR_HI, &far_hi) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_FSYNR0,
                            &out->fsynr0) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_FSYNR1,
                            &out->fsynr1) != 0)
        return -2;
    out->fault_address = ((uint64_t)far_hi << 32) | far_lo;
    return 0;
}

int gpu_smmuv2_read_context_state(const gpu_mmio_window *window,
                                  const gpu_smmuv2_caps *caps,
                                  uint32_t context_bank,
                                  gpu_smmuv2_context_state *out)
{
    uint32_t cb_offset;

    if (!window || !caps || !out || context_bank >= caps->context_bank_count ||
        smmu_offset(caps, caps->page_count + context_bank, 0u, &cb_offset) != 0 ||
        cb_offset > window->size_bytes ||
        window->size_bytes - cb_offset < SMMU_CB_MAIR0 + 4u)
        return -1;
    *out = (gpu_smmuv2_context_state){0};
    if (gpu_mmio_try_read32(window, cb_offset + SMMU_CB_SCTLR,
                            &out->sctlr) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_TCR2,
                            &out->tcr2) != 0 ||
        read64(window, cb_offset + SMMU_CB_TTBR0, &out->ttbr0) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_TCR,
                            &out->tcr) != 0 ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_MAIR0,
                            &out->mair0) != 0)
        return -2;
    return 0;
}

int gpu_smmuv2_read_global_fault(const gpu_mmio_window *window,
                                 gpu_smmuv2_global_fault *out)
{
    if (!window || !out)
        return -1;
    *out = (gpu_smmuv2_global_fault){0};
    if (gpu_mmio_try_read32(window, SMMU_GR0_GFSR, &out->gfsr) != 0 ||
        gpu_mmio_try_read32(window, SMMU_GR0_GFSYNR0, &out->gfsynr0) != 0 ||
        gpu_mmio_try_read32(window, SMMU_GR0_GFSYNR1, &out->gfsynr1) != 0 ||
        gpu_mmio_try_read32(window, SMMU_GR0_GFSYNR2, &out->gfsynr2) != 0)
        return -2;
    return 0;
}

int gpu_smmuv2_find_stream_binding(const gpu_mmio_window *window,
                                   const gpu_smmuv2_caps *caps,
                                   uint32_t stream_id,
                                   gpu_smmuv2_stream_binding *out)
{
    uint32_t smr;

    if (!window || !caps || !out)
        return -1;
    *out = (gpu_smmuv2_stream_binding){0};
    for (uint32_t index = 0u; index < caps->stream_group_count; ++index)
    {
        if (gpu_mmio_try_read32(window, SMMU_GR0_SMR(index), &smr) != 0)
            return -2;
        if ((smr & SMMU_SMR_VALID) != 0u &&
            (smr & 0xffffu) == (stream_id & 0xffffu))
        {
            out->stream_id = stream_id;
            out->smr_index = index;
            out->smr = smr;
            return gpu_mmio_try_read32(window, SMMU_GR0_S2CR(index),
                                       &out->s2cr) == 0 ? 0 : -3;
        }
    }
    return 1;
}

int gpu_smmuv2_attach_context(const gpu_mmio_window *window,
                              const gpu_smmuv2_caps *caps,
                              const gpu_iommu_attach_plan *plan,
                              uint32_t cb,
                              uint32_t *out_bound_stream_count)
{
    uint32_t cb_offset = 0u;
    uint32_t gr1_offset;
    uint32_t free_smrs[GPU_IOMMU_MAX_STREAM_IDS];
    uint32_t bound_smrs[GPU_IOMMU_MAX_STREAM_IDS];
    uint32_t free_smr_count = 0u;
    uint32_t bound_smr_count = 0u;
    uint32_t scratch;
    uint32_t scr0;
    uint32_t tcr;

    if (!window || !caps || !plan || !out_bound_stream_count ||
        plan->architecture != GPU_IOMMU_ARCH_SMMU_V1V2 ||
        !plan->domain_root_phys || !plan->stream_id_count ||
        plan->stream_id_count > GPU_IOMMU_MAX_STREAM_IDS ||
        plan->va_bits != GPU_IOMMU_MAX_VA_BITS ||
        !(caps->id0 & SMMU_ID0_S1TS) || !(caps->id0 & SMMU_ID0_SMS) ||
        !(caps->id2 & SMMU_ID2_PTFS_4K) || !caps->page_count)
        return -1;
    if (caps->page_shift != 12u || caps->context_bank_count == 0u ||
        cb >= caps->context_bank_count ||
        caps->stream_group_count < plan->stream_id_count)
        return -2;

    /* CB0 is reserved for Adreno render traffic.  The caller selects a
     * separate bank for the GMU, whose firmware performs independent DMA. */
    if (smmu_offset(caps, caps->page_count + cb, 0u, &cb_offset) != 0 ||
        cb_offset > window->size_bytes ||
        window->size_bytes - cb_offset < SMMU_CB_TLBSTATUS + 4u ||
        gpu_mmio_try_read32(window, cb_offset + SMMU_CB_SCTLR, &scratch) != 0)
        return -4;
    if (smmu_offset(caps, 1u, SMMU_GR1_CBAR(cb), &gr1_offset) != 0 ||
        gr1_offset > window->size_bytes ||
        window->size_bytes - gr1_offset < SMMU_GR1_CBA2R(cb) + 4u)
        return -3;

    /* Reserve only currently invalid SMRs.  This never overwrites a mapping
     * left by platform firmware or another device driver. */
    for (uint32_t index = 0u;
         index < caps->stream_group_count && free_smr_count < GPU_IOMMU_MAX_STREAM_IDS;
         ++index)
    {
        if (gpu_mmio_try_read32(window, SMMU_GR0_SMR(index), &scratch) != 0)
            return -6;
        if ((scratch & SMMU_SMR_VALID) == 0u)
            free_smrs[free_smr_count++] = index;
    }
    if (free_smr_count < plan->stream_id_count ||
        gpu_mmio_try_read32(window, SMMU_GR0_SCR0, &scr0) != 0)
        return -7;

    /* Stage the entire context bank while it is disabled. */
    tcr = SMMU_TCR_EAE | SMMU_TCR_EPD1 | SMMU_TCR_SH0_INNER |
          SMMU_TCR_ORGN0_WB | SMMU_TCR_IRGN0_WB |
          (64u - plan->va_bits);
    if (gpu_mmio_try_write32(window, gr1_offset,
                             SMMU_CBAR_S1_S2_BYPASS |
                             SMMU_CBAR_MEMATTR_WB |
                             SMMU_CBAR_BPSH_NSH) != 0 ||
        gpu_mmio_try_write32(window,
                             gr1_offset + (SMMU_GR1_CBA2R(cb) - SMMU_GR1_CBAR(cb)),
                             SMMU_CBA2R_VA64) != 0 ||
        gpu_mmio_try_write32(window, cb_offset + SMMU_CB_TCR2,
                             SMMU_TCR2_AS | ((caps->id2 >> 4) & 0xfu)) != 0 ||
        /* TCR determines how TTBR ASID bits are interpreted.  Program it
         * before TTBR0, matching the ARM SMMUv2-required ordering. */
        gpu_mmio_try_write32(window, cb_offset + SMMU_CB_TCR, tcr) != 0 ||
        write64(window, cb_offset + SMMU_CB_TTBR0,
                ((uint64_t)SMMU_ATTACH_ASID << 48) |
                plan->domain_root_phys) != 0 ||
        gpu_mmio_try_write32(window, cb_offset + SMMU_CB_MAIR0, 0xffu) != 0 ||
        gpu_mmio_try_write32(window, cb_offset + SMMU_CB_FSR, 0xffffffffu) != 0 ||
        gpu_mmio_try_write32(window, cb_offset + SMMU_CB_SCTLR,
                             SMMU_SCTLR_S1_ASIDPNE | SMMU_SCTLR_HUPCF |
                             SMMU_SCTLR_CFIE |
                             SMMU_SCTLR_CFRE | SMMU_SCTLR_AFE |
                             SMMU_SCTLR_TRE | SMMU_SCTLR_M) != 0)
        return -8;

    /* Enable the SMMU client interface before making a GPU stream translatable. */
    scr0 |= SMMU_SCR0_GCFGFIE | SMMU_SCR0_GCFGFRE |
            SMMU_SCR0_GFIE | SMMU_SCR0_GFRE;
    scr0 &= ~SMMU_SCR0_CLIENTPD;
    if (gpu_mmio_try_write32(window, SMMU_GR0_SCR0, scr0) != 0)
        return -9;

    for (uint32_t i = 0u; i < plan->stream_id_count; ++i)
    {
        uint32_t smr = free_smrs[i];
        if (gpu_mmio_try_write32(window, SMMU_GR0_S2CR(smr),
                                 SMMU_S2CR_TYPE_TRANS |
                                 (cb & SMMU_S2CR_CBNDX_MASK)) != 0 ||
            gpu_mmio_try_write32(window, SMMU_GR0_SMR(smr),
                                 SMMU_SMR_VALID |
                                 (plan->stream_ids[i] & 0xffffu)) != 0)
        {
            /* Qualcomm firmware exposes a finite writable SMR prefix.  Do
             * not turn a single protected boundary into a storm of faults. */
            (void)gpu_mmio_try_write32(window, SMMU_GR0_S2CR(smr), 1u << 16);
            break;
        }
        bound_smrs[bound_smr_count++] = smr;
    }
    if (!bound_smr_count)
        goto detach_streams;
    asm_mmio_barrier();
    if (gpu_mmio_try_write32(window, SMMU_GR0_TLBIALLNSNH, 0xffffffffu) != 0 ||
        gpu_mmio_try_write32(window, SMMU_GR0_TLBSYNC, 0u) != 0 ||
        poll_clear(window, SMMU_GR0_TLBSTATUS, SMMU_TLBSTATUS_ACTIVE) != 0)
        goto detach_streams;
    *out_bound_stream_count = bound_smr_count;
    return 0;

detach_streams:
    while (bound_smr_count)
    {
        --bound_smr_count;
        (void)gpu_mmio_try_write32(window, SMMU_GR0_SMR(bound_smrs[bound_smr_count]), 0u);
        (void)gpu_mmio_try_write32(window, SMMU_GR0_S2CR(bound_smrs[bound_smr_count]), 1u << 16);
    }
    return -10;
}

int gpu_smmuv2_attach(const gpu_mmio_window *window,
                      const gpu_smmuv2_caps *caps,
                      const gpu_iommu_attach_plan *plan,
                      uint32_t *out_context_bank,
                      uint32_t *out_bound_stream_count)
{
    int rc;

    if (!out_context_bank)
        return -1;
    rc = gpu_smmuv2_attach_context(window, caps, plan, 0u,
                                   out_bound_stream_count);
    if (rc == 0)
        *out_context_bank = 0u;
    return rc;
}
