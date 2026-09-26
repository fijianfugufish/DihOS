#include "gpu/qcom_dpu_present.h"

#include "gpu/gpu_mmio.h"
#include "gpu/gpu_smmu_v2.h"
#include "memory/mmio_map.h"
#include "terminal/terminal_api.h"

#include <stdint.h>

#define X1E80100_DPU_BASE       0x000000000AE01000ull
#define X1E80100_DPU_SIZE       0x000000000008F000ull
#define X1E80100_APPS_SMMU_BASE 0x0000000015000000ull
#define X1E80100_APPS_SMMU_SIZE 0x0000000000100000ull
#define X1E80100_MDP_STREAM_ID   0x00001C00u
#define DPU_INTF_COUNT          9u
#define DPU_INTF_FIRST          0x00034000u
#define DPU_INTF_STRIDE         0x00001000u
#define INTF_HSYNC_CTL          0x008u
#define INTF_VSYNC_PERIOD_F0    0x00Cu
#define INTF_DISPLAY_V_START_F0 0x01Cu
#define INTF_DISPLAY_V_END_F0   0x024u
#define INTF_DISPLAY_HCTL       0x03Cu
#define INTF_FRAME_COUNT        0x0ACu
#define INTF_LINE_COUNT         0x0B0u
#define INTF_STATUS             0x26Cu

#define SSPP_SRC_SIZE           0x000u
#define SSPP_OUT_SIZE           0x00Cu
#define SSPP_SRC0_ADDR          0x014u
#define SSPP_SRC_YSTRIDE0       0x024u
#define SSPP_SRC_FORMAT         0x030u
#define SSPP_SRC_OP_MODE        0x038u
#define SSPP_SRC_ADDR_STATUS    0x070u

#define CTL_TOP                 0x014u
#define CTL_FLUSH               0x018u
#define CTL_START               0x01Cu
#define CTL_INTF_ACTIVE         0x0F4u
#define CTL_FETCH_PIPE_ACTIVE   0x0FCu
#define CTL_PIPE_ACTIVE         0x12Cu
#define CTL_LAYER_ACTIVE        0x130u

typedef struct dpu_sspp_probe
{
    uint32_t base;
    uint8_t id;
} dpu_sspp_probe;

static const dpu_sspp_probe G_sspp_probes[] = {
    {0x04000u, 0u}, {0x06000u, 1u}, {0x08000u, 2u}, {0x0A000u, 3u},
    {0x24000u, 8u}, {0x26000u, 9u}, {0x28000u, 10u},
    {0x2A000u, 11u}, {0x2C000u, 12u}, {0x2E000u, 13u},
};

typedef struct qcom_dpu_present_state
{
    volatile uint8_t *pipe[2];
    volatile uint8_t *ctl;
    volatile uint8_t *intf;
    uint32_t gop_address;
    uint32_t frame_count;
    uint32_t line_count;
    uint32_t htotal;
    uint32_t vtotal;
    uint8_t intf_index;
    uint8_t ready;
    uint8_t takeover_ready;
    uint8_t owned;
} qcom_dpu_present_state;

static qcom_dpu_present_state G_dpu_present;

static uint32_t dpu_read32(volatile const uint8_t *base, uint32_t offset)
{
    return *(volatile const uint32_t *)(base + offset);
}

static void dpu_write_barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint8_t qcom_dpu_audit_smmu(void)
{
    gpu_mmio_window smmu = {0};
    gpu_smmuv2_caps caps;
    gpu_smmuv2_stream_binding binding;
    gpu_smmuv2_context_state context;
    gpu_smmuv2_context_fault fault;
    uint32_t cb;
    int rc;

    if (gpu_mmio_map_window(&smmu, X1E80100_APPS_SMMU_BASE,
                            X1E80100_APPS_SMMU_SIZE) != 0 ||
        gpu_smmuv2_probe(&smmu, &caps) != 0)
    {
        terminal_warn("DPU ownership audit could not read apps SMMU");
        return 0u;
    }
    terminal_print("DPU apps SMMU id0/id1/id2 pages/cb/smr=");
    terminal_print_inline_hex32(caps.id0);
    terminal_print("/");
    terminal_print_inline_hex32(caps.id1);
    terminal_print("/");
    terminal_print_inline_hex32(caps.id2);
    terminal_print(" ");
    terminal_print_inline_hex32(caps.page_count);
    terminal_print("/");
    terminal_print_inline_hex32(caps.context_bank_count);
    terminal_print("/");
    terminal_print_inline_hex32(caps.stream_group_count);

    rc = gpu_smmuv2_find_stream_binding(&smmu, &caps,
                                        X1E80100_MDP_STREAM_ID, &binding);
    if (rc != 0)
    {
        terminal_print("DPU MDP SID 0x1C00 binding unavailable rc=");
        terminal_print_inline_hex32((uint32_t)rc);
        gpu_mmio_unmap_window(&smmu);
        return 0u;
    }
    cb = binding.s2cr & 0xffu;
    terminal_print("DPU MDP SID binding smr-index/smr/s2cr/cb=");
    terminal_print_inline_hex32(binding.smr_index);
    terminal_print("/");
    terminal_print_inline_hex32(binding.smr);
    terminal_print("/");
    terminal_print_inline_hex32(binding.s2cr);
    terminal_print("/");
    terminal_print_inline_hex32(cb);

    rc = gpu_smmuv2_read_context_state(&smmu, &caps, cb, &context);
    if (rc == 0)
    {
        terminal_print("DPU MDP CB sctlr/tcr2/ttbr0/tcr/mair=");
        terminal_print_inline_hex32(context.sctlr);
        terminal_print("/");
        terminal_print_inline_hex32(context.tcr2);
        terminal_print("/");
        terminal_print_inline_hex64(context.ttbr0);
        terminal_print("/");
        terminal_print_inline_hex32(context.tcr);
        terminal_print("/");
        terminal_print_inline_hex32(context.mair0);
    }
    if (gpu_smmuv2_read_context_fault(&smmu, &caps, cb, &fault) == 0)
    {
        terminal_print("DPU MDP CB fault fsr/far/fsynr0/fsynr1=");
        terminal_print_inline_hex32(fault.fsr);
        terminal_print("/");
        terminal_print_inline_hex64(fault.fault_address);
        terminal_print("/");
        terminal_print_inline_hex32(fault.fsynr0);
        terminal_print("/");
        terminal_print_inline_hex32(fault.fsynr1);
    }
    gpu_mmio_unmap_window(&smmu);
    /* UEFI leaves the MDP context bank disabled and puts the physical GOP
     * address directly in each VIG pipe. That is the safe ownership case:
     * no foreign page table has to be modified or inherited. */
    return rc == 0 && !(context.sctlr & 1u);
}

static void qcom_dpu_discover_takeover(volatile uint8_t *dpu,
                                       uint32_t width, uint32_t height,
                                       uint64_t gop_phys,
                                       uint32_t gop_pitch,
                                       uint64_t candidate_phys,
                                       uint8_t physical_scanout)
{
    uint32_t expected_size;
    uint32_t found = 0u;

    if (!physical_scanout || !gop_phys || gop_phys > UINT32_MAX ||
        !candidate_phys || candidate_phys > UINT32_MAX ||
        (candidate_phys & 0xfffu) || width < 2u || (width & 1u) ||
        gop_pitch != width * 4u)
        return;
    expected_size = (height << 16) | (width / 2u);
    for (uint32_t i = 0u;
         i < sizeof(G_sspp_probes) / sizeof(G_sspp_probes[0]) && found < 2u;
         ++i)
    {
        volatile uint8_t *pipe = dpu + G_sspp_probes[i].base;
        if (G_sspp_probes[i].id > 1u ||
            dpu_read32(pipe, SSPP_SRC0_ADDR) != (uint32_t)gop_phys ||
            dpu_read32(pipe, SSPP_SRC_YSTRIDE0) != gop_pitch ||
            dpu_read32(pipe, SSPP_SRC_SIZE) != expected_size ||
            dpu_read32(pipe, SSPP_OUT_SIZE) != expected_size)
            continue;
        G_dpu_present.pipe[found++] = pipe;
    }
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        volatile uint8_t *ctl = dpu + 0x15000u + i * 0x1000u;
        if (dpu_read32(ctl, CTL_INTF_ACTIVE) == (1u << 5) &&
            (dpu_read32(ctl, CTL_FETCH_PIPE_ACTIVE) & 0x00030000u) ==
                0x00030000u)
        {
            G_dpu_present.ctl = ctl;
            break;
        }
    }
    if (found == 2u && G_dpu_present.ctl)
    {
        G_dpu_present.gop_address = (uint32_t)gop_phys;
        G_dpu_present.takeover_ready = 1u;
        terminal_success("DPU firmware split-pipe scanout is safe for physical flips");
    }
}

static void qcom_dpu_audit_firmware_pipeline(volatile uint8_t *dpu,
                                             uint64_t gop_phys,
                                             uint32_t gop_pitch,
                                             uint64_t candidate_phys)
{
    terminal_print("DPU ownership audit GOP-PA/pitch/next-PA=");
    terminal_print_inline_hex64(gop_phys);
    terminal_print("/");
    terminal_print_inline_hex32(gop_pitch);
    terminal_print("/");
    terminal_print_inline_hex64(candidate_phys);

    for (uint32_t i = 0u;
         i < sizeof(G_sspp_probes) / sizeof(G_sspp_probes[0]); ++i)
    {
        volatile uint8_t *pipe = dpu + G_sspp_probes[i].base;
        uint32_t address = dpu_read32(pipe, SSPP_SRC0_ADDR);
        uint32_t source_size = dpu_read32(pipe, SSPP_SRC_SIZE);
        uint32_t output_size = dpu_read32(pipe, SSPP_OUT_SIZE);
        if (!address || !source_size || !output_size)
            continue;
        terminal_print("DPU live SSPP id/base addr/status stride src/out fmt/op=");
        terminal_print_inline_hex8(G_sspp_probes[i].id);
        terminal_print("/");
        terminal_print_inline_hex32(G_sspp_probes[i].base);
        terminal_print(" ");
        terminal_print_inline_hex32(address);
        terminal_print("/");
        terminal_print_inline_hex32(dpu_read32(pipe, SSPP_SRC_ADDR_STATUS));
        terminal_print(" ");
        terminal_print_inline_hex32(dpu_read32(pipe, SSPP_SRC_YSTRIDE0));
        terminal_print(" ");
        terminal_print_inline_hex32(source_size);
        terminal_print("/");
        terminal_print_inline_hex32(output_size);
        terminal_print(" ");
        terminal_print_inline_hex32(dpu_read32(pipe, SSPP_SRC_FORMAT));
        terminal_print("/");
        terminal_print_inline_hex32(dpu_read32(pipe, SSPP_SRC_OP_MODE));
    }

    for (uint32_t i = 0u; i < 6u; ++i)
    {
        volatile uint8_t *ctl = dpu + 0x15000u + i * 0x1000u;
        uint32_t intf = dpu_read32(ctl, CTL_INTF_ACTIVE);
        uint32_t fetch = dpu_read32(ctl, CTL_FETCH_PIPE_ACTIVE);
        uint32_t pipes = dpu_read32(ctl, CTL_PIPE_ACTIVE);
        uint32_t layers = dpu_read32(ctl, CTL_LAYER_ACTIVE);
        if (!intf && !fetch && !pipes && !layers)
            continue;
        terminal_print("DPU live CTL id intf/fetch/pipe/layer top/flush/start=");
        terminal_print_inline_hex8((uint8_t)i);
        terminal_print(" ");
        terminal_print_inline_hex32(intf);
        terminal_print("/");
        terminal_print_inline_hex32(fetch);
        terminal_print("/");
        terminal_print_inline_hex32(pipes);
        terminal_print("/");
        terminal_print_inline_hex32(layers);
        terminal_print(" ");
        terminal_print_inline_hex32(dpu_read32(ctl, CTL_TOP));
        terminal_print("/");
        terminal_print_inline_hex32(dpu_read32(ctl, CTL_FLUSH));
        terminal_print("/");
        terminal_print_inline_hex32(dpu_read32(ctl, CTL_START));
    }
    terminal_flush_log();
}

int qcom_dpu_present_init(uint32_t width, uint32_t height,
                          uint64_t gop_phys, uint32_t gop_pitch,
                          uint64_t candidate_phys)
{
    volatile uint8_t *dpu = (volatile uint8_t *)(uintptr_t)X1E80100_DPU_BASE;

    G_dpu_present = (qcom_dpu_present_state){0};
    if (!width || !height ||
        mmio_map_device_identity(X1E80100_DPU_BASE, X1E80100_DPU_SIZE) != 0)
        return -1;

    qcom_dpu_audit_firmware_pipeline(dpu, gop_phys, gop_pitch,
                                     candidate_phys);
    {
        uint8_t physical_scanout = qcom_dpu_audit_smmu();
        qcom_dpu_discover_takeover(dpu, width, height, gop_phys, gop_pitch,
                                   candidate_phys, physical_scanout);
    }

    for (uint32_t i = 0u; i < DPU_INTF_COUNT; ++i)
    {
        volatile uint8_t *intf = dpu + DPU_INTF_FIRST + i * DPU_INTF_STRIDE;
        uint32_t status = dpu_read32(intf, INTF_STATUS);
        uint32_t hsync = dpu_read32(intf, INTF_HSYNC_CTL);
        uint32_t htotal = hsync >> 16;
        uint32_t vsync_period = dpu_read32(intf, INTF_VSYNC_PERIOD_F0);
        uint32_t vstart = dpu_read32(intf, INTF_DISPLAY_V_START_F0);
        uint32_t vend = dpu_read32(intf, INTF_DISPLAY_V_END_F0);
        uint32_t hctl = dpu_read32(intf, INTF_DISPLAY_HCTL);
        uint32_t hstart = hctl & 0xffffu;
        uint32_t hend = hctl >> 16;
        uint32_t active_width;
        uint32_t active_height;
        uint32_t vtotal;

        if (!(status & 1u) || !htotal || !vsync_period || vend < vstart ||
            hend < hstart)
            continue;
        active_width = hend - hstart + 1u;
        vtotal = vsync_period / htotal;
        /* DISPLAY_V_END is a flattened pixel position.  DP timing adjusts
         * the first and final scanline horizontally, so truncating the span
         * reports one line too few (1839 for the 2944x1840 panel). */
        active_height = (vend - vstart + 1u + htotal - 1u) / htotal;
        if (!vtotal || !active_height || active_height != height ||
            (active_width != width && active_width * 2u != width))
            continue;

        G_dpu_present.intf = intf;
        G_dpu_present.frame_count = dpu_read32(intf, INTF_FRAME_COUNT);
        G_dpu_present.line_count = dpu_read32(intf, INTF_LINE_COUNT);
        G_dpu_present.htotal = htotal;
        G_dpu_present.vtotal = vtotal;
        G_dpu_present.intf_index = (uint8_t)i;
        G_dpu_present.ready = 1u;
        terminal_print("DPU present clock: INTF");
        terminal_print_inline_hex8((uint8_t)i);
        terminal_print(" frame/line=");
        terminal_print_inline_hex32(G_dpu_present.frame_count);
        terminal_print("/");
        terminal_print_inline_hex32(G_dpu_present.line_count);
        terminal_print(" total=");
        terminal_print_inline_hex32(htotal);
        terminal_print("x");
        terminal_print_inline_hex32(vtotal);
        terminal_success("DPU scan counters discovered (diagnostic; GOP owns presentation)");
        return 0;
    }

    terminal_warn("DPU presentation clock unavailable; GOP immediate fallback retained");
    return -2;
}

uint8_t qcom_dpu_present_ready(void)
{
    return G_dpu_present.ready;
}

uint8_t qcom_dpu_present_frame_boundary(void)
{
    uint32_t frame;
    uint32_t line;
    uint8_t wrapped;

    if (!G_dpu_present.ready || !G_dpu_present.intf)
        return 1u;
    line = dpu_read32(G_dpu_present.intf, INTF_LINE_COUNT);
    frame = dpu_read32(G_dpu_present.intf, INTF_FRAME_COUNT);
    if (line == G_dpu_present.line_count)
        return 0u;
    /* Firmware/Panel Replay can expose a FRAME_COUNT that advances at only
     * a few hertz even while the timing engine's scanline counter is live.
     * A descending line count is the actual start-of-frame boundary. */
    wrapped = line < G_dpu_present.line_count;
    G_dpu_present.line_count = line;
    G_dpu_present.frame_count = frame;
    return wrapped;
}

uint8_t qcom_dpu_present_takeover_ready(void)
{
    return G_dpu_present.takeover_ready;
}

int qcom_dpu_present_flip(uint64_t scanout_phys)
{
    if (!G_dpu_present.takeover_ready || !G_dpu_present.pipe[0] ||
        !G_dpu_present.pipe[1] || !G_dpu_present.ctl || !scanout_phys ||
        scanout_phys > UINT32_MAX || (scanout_phys & 0xfffu))
        return -1;
    *(volatile uint32_t *)(G_dpu_present.pipe[0] + SSPP_SRC0_ADDR) =
        (uint32_t)scanout_phys;
    *(volatile uint32_t *)(G_dpu_present.pipe[1] + SSPP_SRC0_ADDR) =
        (uint32_t)scanout_phys;
    dpu_write_barrier();
    if (dpu_read32(G_dpu_present.pipe[0], SSPP_SRC0_ADDR) !=
            (uint32_t)scanout_phys ||
        dpu_read32(G_dpu_present.pipe[1], SSPP_SRC0_ADDR) !=
            (uint32_t)scanout_phys)
    {
        *(volatile uint32_t *)(G_dpu_present.pipe[0] + SSPP_SRC0_ADDR) =
            G_dpu_present.gop_address;
        *(volatile uint32_t *)(G_dpu_present.pipe[1] + SSPP_SRC0_ADDR) =
            G_dpu_present.gop_address;
        dpu_write_barrier();
        return -2;
    }
    /* VIG0 and VIG1 are flush bits 0 and 1. The active video-mode CTL
     * consumes this mask atomically at its next frame boundary; CTL_START is
     * intentionally untouched because firmware already started INTF5. */
    *(volatile uint32_t *)(G_dpu_present.ctl + CTL_FLUSH) = 0x3u;
    dpu_write_barrier();
    G_dpu_present.owned = 1u;
    return 0;
}

void qcom_dpu_present_restore_gop(void)
{
    if (!G_dpu_present.owned || !G_dpu_present.pipe[0] ||
        !G_dpu_present.pipe[1] || !G_dpu_present.ctl)
        return;
    *(volatile uint32_t *)(G_dpu_present.pipe[0] + SSPP_SRC0_ADDR) =
        G_dpu_present.gop_address;
    *(volatile uint32_t *)(G_dpu_present.pipe[1] + SSPP_SRC0_ADDR) =
        G_dpu_present.gop_address;
    dpu_write_barrier();
    *(volatile uint32_t *)(G_dpu_present.ctl + CTL_FLUSH) = 0x3u;
    dpu_write_barrier();
    G_dpu_present.owned = 0u;
}
