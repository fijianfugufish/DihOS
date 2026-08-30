#include "gpu/adreno_x1_85.h"
#include "asm/asm.h"
#include "terminal/terminal_api.h"

static const gpu_firmware_file g_adreno_x1_85_firmware[] = {
    {"gen70500_gmu.bin", GPU_FIRMWARE_GMU, 1u, 81312u},
    {"gen70500_sqe.fw", GPU_FIRMWARE_SQE, 1u, 77332u},
    {"gen70500_zap.mbn", GPU_FIRMWARE_SECURE, 1u, 12088u},
};

static const gpu_firmware_manifest g_adreno_x1_85_manifest = {
    "adreno-x1-85",
    "0:/OS/Firmware/adreno-x1-85/upstream",
    g_adreno_x1_85_firmware,
    (uint32_t)(sizeof(g_adreno_x1_85_firmware) / sizeof(g_adreno_x1_85_firmware[0])),
};

/* X1E's published OPP set, from highest GX level to the conservative
 * 300 MHz level.  The numbers are RPMh regulator levels, not voltages.
 * Keeping these facts in the device profile lets the common Gen7 HFI layer
 * stay reusable by a future GPU driver. */
static const gpu_gmu_hfi_gen7_perf_table g_adreno_x1_85_perf_table = {
    9u, 2u,
    {
        {416u, 0xffffffffu, 1100000u},
        {384u, 0xffffffffu, 1000000u},
        {320u, 0xffffffffu,  925000u},
        {256u, 0xffffffffu,  800000u},
        {224u, 0xffffffffu,  744000u},
        {192u, 0xffffffffu,  687000u},
        {128u, 0xffffffffu,  550000u},
        { 64u, 0xffffffffu,  390000u},
        { 56u, 0xffffffffu,  300000u},
    },
    {
        {128u, 550000u},
        { 64u, 220000u},
    },
};

/* The first element is synthesized as the GMU's mandatory "off" bandwidth
 * level.  These entries correspond to the conservative 1.1 GHz-and-below
 * X1E OPP profile above, in ascending-performance order. */
static const uint32_t g_adreno_x1_85_bandwidth_kbps[] = {
    2136719u, 3000000u, 3000000u, 6074219u, 8171875u,
    10687500u, 12449219u, 14398438u, 14398438u,
};

typedef struct adreno_x1_85_bcm_policy
{
    const char *resource_id;
    uint32_t bus_width;
    uint32_t fixed_performance_mode;
    uint32_t fixed_performance_threshold_kbps;
} adreno_x1_85_bcm_policy;

static const adreno_x1_85_bcm_policy g_adreno_x1_85_bcms[] = {
    {"SH0", 16u, 0u, 0u},
    {"MC0", 4u, 0u, 0u},
    {"ACV", 0u, 1u << 3, 16500000u},
};

#define ADRENO_X1_85_BCM_AUX_BYTES 8u
#define ADRENO_X1_85_BCM_COMMIT    (1u << 30)
#define ADRENO_X1_85_BCM_VALID     (1u << 29)
#define ADRENO_X1_85_BCM_VOTE_MASK 0x3fffu

static uint16_t adreno_x1_85_read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t adreno_x1_85_read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t adreno_x1_85_bcm_command(uint32_t commit, uint32_t valid,
                                         uint32_t vote_x, uint32_t vote_y)
{
    return (commit ? ADRENO_X1_85_BCM_COMMIT : 0u) |
           (valid ? ADRENO_X1_85_BCM_VALID : 0u) |
           ((vote_x & ADRENO_X1_85_BCM_VOTE_MASK) << 14) |
           (vote_y & ADRENO_X1_85_BCM_VOTE_MASK);
}

static int adreno_x1_85_arc_vote(const rpmh_cmd_db *cmd_db,
                                 const char *primary_id,
                                 const char *secondary_id,
                                 uint32_t level, uint32_t *out_vote)
{
    rpmh_cmd_db_resource primary = {0};
    rpmh_cmd_db_resource secondary = {0};
    uint32_t primary_count;
    uint32_t secondary_count;
    uint32_t primary_index;
    uint32_t secondary_index = 0u;

    if (!cmd_db || !primary_id || !secondary_id || !out_vote ||
        rpmh_cmd_db_find(cmd_db, primary_id, &primary) != 0 ||
        rpmh_cmd_db_find(cmd_db, secondary_id, &secondary) != 0 ||
        !primary.aux_data || !secondary.aux_data ||
        (primary.aux_size & 1u) || (secondary.aux_size & 1u))
        return -1;
    primary_count = primary.aux_size / 2u;
    secondary_count = secondary.aux_size / 2u;
    if (!primary_count || !secondary_count)
        return -2;
    for (primary_index = 0u; primary_index < primary_count; ++primary_index)
        if (adreno_x1_85_read_le16(primary.aux_data + primary_index * 2u) >= level)
            break;
    if (primary_index == primary_count)
        return -3;
    for (uint32_t i = 0u; i < secondary_count; ++i)
    {
        uint16_t secondary_level = adreno_x1_85_read_le16(
            secondary.aux_data + i * 2u);

        if (secondary_level >= level)
        {
            secondary_index = i;
            break;
        }
        if (secondary_level)
            secondary_index = i;
    }
    *out_vote = ((uint32_t)adreno_x1_85_read_le16(
                     primary.aux_data + primary_index * 2u) << 16) |
                (secondary_index << 8) | primary_index;
    return 0;
}

/* This is a description, not an executable script.  The platform vote and
 * SMMU attach actions remain unavailable until their native DihOS services
 * exist; generic code validates that they stay ahead of GPUCC/GMU access. */
static const gpu_bringup_op g_adreno_x1_85_bringup_ops[] = {
    {GPU_BRINGUP_PLATFORM_VOTE, GPU_BRINGUP_TARGET_GPUCC, 0u, 0u},
    /* GPU CX domain: remove SW collapse and preserve the fabric state. */
    {GPU_BRINGUP_RMW_CLEAR, GPU_BRINGUP_TARGET_GPUCC, 0x9108u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9108u, 1u << 11},
    /* Always-on GMU path: XO, AHB, sleep, GMU, CX hub and memory fabric. */
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9004u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x911cu, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9134u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x913cu, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9144u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9148u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9150u, 1u << 0},
    /* HLOS vote gate for the GPU-local SMMU context-bank pages. */
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x7000u, 1u << 0},
    {GPU_BRINGUP_RMW_SET, GPU_BRINGUP_TARGET_GPUCC, 0x900cu, 1u << 0},
    {GPU_BRINGUP_POLL_SET, GPU_BRINGUP_TARGET_GPUCC, 0x9108u, 1u << 31},
    {GPU_BRINGUP_SMMU_ATTACH, GPU_BRINGUP_TARGET_GPUCC, 0u, 0u},
    {GPU_BRINGUP_FIRMWARE_UPLOAD, GPU_BRINGUP_TARGET_GMU, 0u, 0u},
    {GPU_BRINGUP_START_GMU, GPU_BRINGUP_TARGET_GMU, 0u, 0u},
};

static const gpu_bringup_plan g_adreno_x1_85_bringup_plan = {
    "adreno-x1-85",
    g_adreno_x1_85_bringup_ops,
    (uint32_t)(sizeof(g_adreno_x1_85_bringup_ops) /
               sizeof(g_adreno_x1_85_bringup_ops[0])),
};

static void print_hex(const char *label, uint64_t value)
{
    terminal_print(label);
    terminal_print_inline_hex64(value);
}

static int bytes_equal(const uint8_t *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] != (uint8_t)b[i]) return 0;
    return 1;
}

static int text_has_gpu0(const uint8_t *text, uint32_t max)
{
    for (uint32_t i = 0; i + 4u <= max && text[i]; ++i)
        if (bytes_equal(text + i, "GPU0", 4u)) return 1;
    return 0;
}

static int text_has_token(const uint8_t *text, uint32_t max,
                          const char *token)
{
    uint32_t token_len = 0u;

    while (token[token_len])
        ++token_len;
    for (uint32_t i = 0u; i + token_len <= max && text[i]; ++i)
        if (bytes_equal(text + i, token, token_len))
            return 1;
    return 0;
}

static void log_iort_gpu_candidate(const uint8_t *name, uint32_t max)
{
    char text[48];
    uint32_t count = 0u;

    if (!text_has_token(name, max, "GPU") &&
        !text_has_token(name, max, "GFX") &&
        !text_has_token(name, max, "QCOM0C36"))
        return;
    while (count + 1u < sizeof(text) && count < max && name[count])
    {
        text[count] = (char)name[count];
        ++count;
    }
    text[count] = '\0';
    terminal_print("[K:GPU] IORT named candidate=");
    terminal_print(text);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4u) << 32);
}

static int range_inside(uint64_t outer_base, uint64_t outer_size,
                        uint64_t inner_base, uint64_t inner_size)
{
    uint64_t outer_end = outer_base + outer_size;
    uint64_t inner_end = inner_base + inner_size;

    if (!outer_size || !inner_size || outer_end < outer_base ||
        inner_end < inner_base)
        return 0;
    return inner_base >= outer_base && inner_end <= outer_end;
}

static const uint8_t *find_iort(uint64_t rsdp_phys)
{
    const uint8_t *rsdp = (const uint8_t *)(uintptr_t)rsdp_phys;
    const uint8_t *root;
    uint64_t root_phys;
    uint32_t root_len;
    uint32_t entry_size;

    if (!rsdp || !bytes_equal(rsdp, "RSD PTR ", 8u)) return 0;
    root_phys = rsdp[15] >= 2u ? le64(rsdp + 24u) : le32(rsdp + 16u);
    root = (const uint8_t *)(uintptr_t)root_phys;
    if (!root || (!bytes_equal(root, "XSDT", 4u) && !bytes_equal(root, "RSDT", 4u))) return 0;
    root_len = le32(root + 4u);
    entry_size = bytes_equal(root, "XSDT", 4u) ? 8u : 4u;
    if (root_len < 36u) return 0;
    for (uint32_t at = 36u; at + entry_size <= root_len; at += entry_size)
    {
        const uint8_t *table = (const uint8_t *)(uintptr_t)(entry_size == 8u ? le64(root + at) : le32(root + at));
        if (table && bytes_equal(table, "IORT", 4u) && le32(table + 4u) >= 48u)
            return table;
    }
    return 0;
}

/* Resolve one IORT ID mapping all the way to a terminal SMMU node.  IORT
 * permits intermediate nodes, so GPU0 is not required to point at an SMMU
 * directly.  Type 3 has the SMMUv1/v2 layout; type 4 is SMMUv3. */
static int iort_map_to_smmu(const uint8_t *iort, uint32_t table_len,
                            const uint8_t *map, uint32_t input_id,
                            uint32_t *stream_id, uint64_t *smmu_base,
                            uint32_t *smmu_type)
{
    const uint8_t *current_map = map;
    uint32_t current_id = input_id;

    for (uint32_t depth = 0u; depth < 8u; ++depth)
    {
        uint32_t flags = le32(current_map + 16u);
        uint32_t input_base = le32(current_map);
        uint32_t input_count = le32(current_map + 4u);
        uint32_t output_base = le32(current_map + 8u);
        uint32_t ref = le32(current_map + 12u);
        const uint8_t *node;
        uint32_t node_len;

        if (!(flags & 1u))
        {
            if (current_id < input_base || current_id - input_base > input_count)
                return -1;
            current_id = output_base + (current_id - input_base);
        }
        else
            current_id = output_base;
        if (ref > table_len || table_len - ref < 16u)
            return -2;
        node = iort + ref;
        node_len = (uint32_t)node[1] | ((uint32_t)node[2] << 8);
        if (node_len < 16u || node_len > table_len - ref)
            return -3;
        if (node[0] == GPU_IOMMU_ARCH_SMMU_V1V2 ||
            node[0] == GPU_IOMMU_ARCH_SMMU_V3)
        {
            if (node_len < 24u)
                return -4;
            *stream_id = current_id;
            *smmu_base = le64(node + 16u);
            *smmu_type = node[0];
            return 0;
        }

        {
            uint32_t count = le32(node + 8u);
            uint32_t offset = le32(node + 12u);
            const uint8_t *next = 0;

            if (!offset || offset > node_len || count > (node_len - offset) / 20u)
                return -5;
            for (uint32_t i = 0u; i < count; ++i)
            {
                const uint8_t *candidate = node + offset + i * 20u;
                uint32_t candidate_flags = le32(candidate + 16u);
                uint32_t candidate_input = le32(candidate);
                uint32_t candidate_count = le32(candidate + 4u);

                if ((candidate_flags & 1u) ||
                    (current_id >= candidate_input &&
                     current_id - candidate_input <= candidate_count))
                {
                    next = candidate;
                    break;
                }
            }
            if (!next)
                return -6;
            current_map = next;
        }
    }
    return -7;
}

static gpu_iommu_endpoint *iommu_endpoint_for(gpu_iommu_topology *binding,
                                               uint64_t base, uint32_t type)
{
    for (uint32_t i = 0u; i < binding->endpoint_count; ++i)
        if (binding->endpoints[i].base == base &&
            binding->endpoints[i].architecture == type)
            return &binding->endpoints[i];
    if (binding->endpoint_count >= GPU_IOMMU_MAX_ENDPOINTS)
        return 0;
    binding->endpoints[binding->endpoint_count].base = base;
    binding->endpoints[binding->endpoint_count].architecture = type;
    return &binding->endpoints[binding->endpoint_count++];
}

int adreno_x1_85_resolve_iommu(uint64_t rsdp_phys, gpu_iommu_topology *out)
{
    const uint8_t *iort = find_iort(rsdp_phys);
    uint32_t table_len;
    uint32_t node_count;
    uint32_t node_off;

    if (!out) return -1;
    *out = (gpu_iommu_topology){0};
    if (!iort) return -2;
    table_len = le32(iort + 4u);
    node_count = le32(iort + 36u);
    node_off = le32(iort + 40u);
    if (node_off >= table_len) return -3;

    for (uint32_t seen = 0u, off = node_off; seen < node_count && off + 16u <= table_len; ++seen)
    {
        const uint8_t *node = iort + off;
        uint32_t node_len = (uint32_t)node[1] | ((uint32_t)node[2] << 8);
        uint32_t map_count = le32(node + 8u);
        uint32_t map_off = le32(node + 12u);
        if (node_len < 16u || off + node_len > table_len) return -4;
        /* Type 1 is an IORT Named Component.  Its fixed data ends with the
         * one-byte DMA address-size limit at offset 28; the namespace path
         * begins at offset 29. */
        if (node[0] == 1u && node_len > 29u)
        {
            const uint8_t *name = node + 29u;
            uint32_t name_max = node_len - 29u;

            log_iort_gpu_candidate(name, name_max);
            if (!text_has_gpu0(name, name_max))
            {
                off += node_len;
                continue;
            }
            if (!map_off || map_off > node_len || map_count > (node_len - map_off) / 20u) return -5;
            for (uint32_t m = 0u; m < map_count; ++m)
            {
                const uint8_t *map = node + map_off + m * 20u;
                uint32_t ids = le32(map + 4u);
                uint32_t input_base = le32(map);

                if (ids == 0xffffffffu)
                    return -6;
                ++ids;
                for (uint32_t id = 0u; id < ids &&
                                       id < GPU_IOMMU_MAX_STREAM_IDS; ++id)
                {
                    uint32_t stream_id;
                    uint32_t smmu_type;
                    uint64_t smmu_base;
                    gpu_iommu_endpoint *endpoint;

                    {
                        int map_rc = iort_map_to_smmu(iort, table_len, map,
                                                      input_base + id,
                                                      &stream_id, &smmu_base,
                                                      &smmu_type);
                        if (map_rc != 0)
                            /* Keep the inner mapping failure observable in
                             * the boot log: 0x101..0x107. */
                            return -(0x100 + (uint32_t)-map_rc);
                    }
                    endpoint = iommu_endpoint_for(out, smmu_base, smmu_type);
                    if (!endpoint || endpoint->stream_id_count >=
                                         GPU_IOMMU_MAX_STREAM_IDS)
                        return -8;
                    endpoint->stream_ids[endpoint->stream_id_count++] = stream_id;
                }
            }
            return out->endpoint_count ? 0 : -9;
        }
        off += node_len;
    }
    return -9;
}

int adreno_x1_85_probe_profile(uint64_t rsdp_phys, adreno_x1_85_profile *out)
{
    adreno_x1_85_profile profile = {
        ADRENO_X1_85_GFX_REGS_BASE, ADRENO_X1_85_GFX_REGS_SIZE,
        ADRENO_X1_85_PDC_REGS_BASE, ADRENO_X1_85_PDC_REGS_SIZE,
        ADRENO_X1_85_GPU_SMMU_BASE, ADRENO_X1_85_SYSTEM_SMMU_BASE,
        ADRENO_X1_85_GFX_SPI, ADRENO_X1_85_GMU_HOST_SPI,
        ADRENO_X1_85_LPAC_SPI, rsdp_phys ? 1u : 0u};

    if (out)
        *out = profile;
    if (!rsdp_phys)
    {
        terminal_warn("[K:GPU] X1-85 profile skipped: no ACPI RSDP");
        return -1;
    }

    terminal_print("[K:GPU] ACPI GPU0 profile: QCOM0C36 / Adreno X1-85");
    print_hex("[K:GPU] GFX_REGS base=", profile.gfx_regs_base);
    print_hex("[K:GPU] GFX_REGS size=", profile.gfx_regs_size);
    print_hex("[K:GPU] GPU_PDC base=", profile.pdc_regs_base);
    print_hex("[K:GPU] GPU SMMUv2 base=", profile.gpu_smmu_base);
    print_hex("[K:GPU] system SMMUv2 base=", profile.system_smmu_base);
    print_hex("[K:GPU] IRQ gfx=", profile.gfx_spi);
    print_hex("[K:GPU] IRQ gmu-host=", profile.gmu_host_spi);
    print_hex("[K:GPU] IRQ lpac=", profile.lpac_spi);
    terminal_print("[K:GPU] ACPI discovery complete; continuing with guarded bring-up");
    return 0;
}

int adreno_x1_85_resolve_blocks(const adreno_x1_85_profile *profile,
                                adreno_x1_85_block_map *out)
{
    adreno_x1_85_block_map blocks = {
        ADRENO_X1_85_RSCC_BASE, ADRENO_X1_85_RSCC_SIZE,
        ADRENO_X1_85_GMU_BASE, ADRENO_X1_85_GMU_SIZE,
        ADRENO_X1_85_GPUCC_BASE, ADRENO_X1_85_GPUCC_SIZE,
    };

    if (!profile || !out)
        return -1;
    if (!range_inside(profile->gfx_regs_base, profile->gfx_regs_size,
                      blocks.rscc_base, blocks.rscc_size) ||
        !range_inside(profile->gfx_regs_base, profile->gfx_regs_size,
                      blocks.gmu_base, blocks.gmu_size) ||
        !range_inside(profile->gfx_regs_base, profile->gfx_regs_size,
                      blocks.gpucc_base, blocks.gpucc_size))
        return -2;
    *out = blocks;
    return 0;
}

const gpu_firmware_manifest *adreno_x1_85_firmware_manifest(void)
{
    return &g_adreno_x1_85_manifest;
}

const gpu_gmu_hfi_gen7_perf_table *adreno_x1_85_hfi_perf_table(void)
{
    return &g_adreno_x1_85_perf_table;
}

int adreno_x1_85_build_hfi_perf_table(const rpmh_cmd_db *cmd_db,
                                      gpu_gmu_hfi_gen7_perf_table *out)
{
    rpmh_cmd_db_resource gmxc = {0};
    const char *gx_secondary = "mx.lvl";

    if (!cmd_db || !out)
        return -1;
    /* GMxC is the secondary GX rail when the firmware supplies it. */
    if (rpmh_cmd_db_find(cmd_db, "gmxc.lvl", &gmxc) == 0 &&
        gmxc.aux_data && gmxc.aux_size >= 2u)
        gx_secondary = "gmxc.lvl";
    *out = g_adreno_x1_85_perf_table;
    for (uint32_t i = 0u; i < out->gx_level_count; ++i)
        if (adreno_x1_85_arc_vote(cmd_db, "gfx.lvl", gx_secondary,
                                  g_adreno_x1_85_perf_table.gx[i].power_vote,
                                  &out->gx[i].power_vote) != 0)
            return -2;
    for (uint32_t i = 0u; i < out->cx_level_count; ++i)
        if (adreno_x1_85_arc_vote(cmd_db, "cx.lvl", "mx.lvl",
                                  g_adreno_x1_85_perf_table.cx[i].power_vote,
                                  &out->cx[i].power_vote) != 0)
            return -3;
    return 0;
}

int adreno_x1_85_build_hfi_bw_table(const rpmh_cmd_db *cmd_db,
                                    gpu_gmu_hfi_gen7_bw_table *out)
{
    rpmh_cmd_db_resource ddr[sizeof(g_adreno_x1_85_bcms) /
                             sizeof(g_adreno_x1_85_bcms[0])] = {{0}};
    rpmh_cmd_db_resource cnoc = {0};
    const uint32_t ddr_count = (uint32_t)(sizeof(ddr) / sizeof(ddr[0]));
    const uint32_t bandwidth_count = (uint32_t)(sizeof(g_adreno_x1_85_bandwidth_kbps) /
                                                sizeof(g_adreno_x1_85_bandwidth_kbps[0]));

    if (!cmd_db || !out || bandwidth_count + 1u > GPU_GMU_HFI_GEN7_MAX_BW_LEVELS)
        return -1;
    *out = (gpu_gmu_hfi_gen7_bw_table){0};
    for (uint32_t i = 0u; i < ddr_count; ++i)
    {
        if (rpmh_cmd_db_find(cmd_db, g_adreno_x1_85_bcms[i].resource_id,
                             &ddr[i]) != 0 || !ddr[i].address ||
            !ddr[i].aux_data || ddr[i].aux_size < ADRENO_X1_85_BCM_AUX_BYTES)
            return -2;
        out->ddr_addresses[i] = ddr[i].address;
    }
    if (rpmh_cmd_db_find(cmd_db, "CN0", &cnoc) != 0 || !cnoc.address)
        return -3;

    out->level_count = bandwidth_count + 1u;
    out->ddr_command_count = ddr_count;
    out->cnoc_command_count = 1u;
    out->cnoc_addresses[0] = cnoc.address;
    /* CN0 is a binary CX path: table row 0 disables it and row 1 enables it. */
    out->cnoc_data[0][0] = adreno_x1_85_bcm_command(1u, 0u, 0u, 0u);
    out->cnoc_data[1][0] = adreno_x1_85_bcm_command(1u, 1u, 0u, 1u);
    out->cnoc_wait_mask = 1u;

    for (uint32_t level = 0u; level < out->level_count; ++level)
    {
        uint32_t bandwidth_kbps = level ?
            g_adreno_x1_85_bandwidth_kbps[level - 1u] : 0u;

        for (uint32_t i = 0u; i < ddr_count; ++i)
        {
            const adreno_x1_85_bcm_policy *policy = &g_adreno_x1_85_bcms[i];
            const uint8_t *aux = ddr[i].aux_data;
            uint32_t commit = i == ddr_count - 1u ||
                aux[6] != ddr[i + 1u].aux_data[6];
            uint32_t vote = 0u;

            if (bandwidth_kbps && policy->fixed_performance_mode)
            {
                if (bandwidth_kbps >= policy->fixed_performance_threshold_kbps)
                    vote = policy->fixed_performance_mode;
            }
            else if (bandwidth_kbps)
            {
                uint64_t peak = (uint64_t)bandwidth_kbps *
                                adreno_x1_85_read_le16(aux + 4u);
                uint32_t unit = adreno_x1_85_read_le32(aux);

                if (!unit || !policy->bus_width)
                    return -4;
                peak /= policy->bus_width;
                peak = (peak * 1000u) / unit;
                vote = peak > ADRENO_X1_85_BCM_VOTE_MASK ?
                    ADRENO_X1_85_BCM_VOTE_MASK : (uint32_t)peak;
                if (!vote)
                    vote = 1u;
            }
            out->ddr_data[level][i] = adreno_x1_85_bcm_command(
                commit, bandwidth_kbps != 0u, vote, vote);
            if (level == 0u && commit)
                out->ddr_wait_mask |= 1u << i;
        }
    }
    return 0;
}

int adreno_x1_85_prepare_gmu_cold_boot(const gpu_mmio_window *gfx,
                                       const gpu_mmio_window *gmu,
                                       const gpu_mmio_window *rscc,
                                       const gpu_mmio_window *pdc)
{
    /* These are byte offsets in the ACPI GPU0 aperture.  CX-MISC remains
     * accessible with GX off; it holds the A7xx TCM retention control. */
    static const uint32_t gbif_qsb_offsets[] = {
        0x0000f00cu, 0x0000f010u, 0x0000f014u, 0x0000f018u,
    };

    /* The X1-85 is a Gen7.2 part, so it uses the A740-family RSCC layout.
     * These are byte offsets, converted from the upstream register indices.
     * The sequence prepares the RSCC<->PDC sleep/wake handshake before the
     * GMU is released from reset. */
    static const uint32_t rscc_seq_words[] = {
        0xeaaae5a0u, 0xe1a1ebabu, 0xa2e0a581u, 0xecac82e2u, 0x0020edadu,
    };
    const uint32_t rscc_status0 = 0x0010u;
    const uint32_t rscc_pdc_seq_start = 0x0020u;
    const uint32_t rscc_pdc_match_lo = 0x0024u;
    const uint32_t rscc_pdc_match_hi = 0x0028u;
    const uint32_t rscc_pdc_slave = 0x002cu;
    const uint32_t rscc_hidden_addr = 0x0034u;
    const uint32_t rscc_hidden_data = 0x0038u;
    const uint32_t rscc_override_start = 0x0400u;
    const uint32_t rscc_x185_seq_mem = 0x0550u;
    const uint32_t pdc_enable = 0x4500u;
    const uint32_t pdc_seq_start = 0x4520u;

    if (!gfx || !gmu || !rscc || !pdc)
        return -1;
    for (uint32_t i = 0u;
         i < sizeof(gbif_qsb_offsets) / sizeof(gbif_qsb_offsets[0]); ++i)
    {
        if (gpu_mmio_try_write32(gfx, gbif_qsb_offsets[i], 0x00071620u) != 0)
            return -2;
    }
    /* REG_A7XX_CX_MISC_TCM_RET_CNTL: CX-MISC base 0x3d9e000, register 0x39. */
    if (gpu_mmio_try_write32(gfx, 0x0009e0e4u, 1u) != 0)
        return -3;
    /* REG_A6XX_GPU_GMU_CX_GMU_CX_FAL{,NEXT}_INTF.  The GMU owns the
     * following power vote while firmware is coming out of reset. */
    if (gpu_mmio_try_write32(gmu, 0x000143c0u, 1u) != 0 ||
        gpu_mmio_try_write32(gmu, 0x000143c4u, 1u) != 0)
        return -4;

    /* Disable RSCC SDE clock-gating, then establish the A740-family hidden
     * TCS handshake.  X1-85 is catalogued by upstream as Gen7.2 and uses the
     * second-spin sequencer-memory location at register index 0x154. */
    if (gpu_mmio_try_write32(rscc, rscc_status0, 1u << 24) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_pdc_slave, 1u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_data, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_addr, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_data + 8u, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_addr + 8u, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_data + 16u,
                             0x80000021u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_hidden_addr + 16u, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_override_start, 0u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_pdc_seq_start, pdc_seq_start) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_pdc_match_lo, 0x4510u) != 0 ||
        gpu_mmio_try_write32(rscc, rscc_pdc_match_hi, 0x4514u) != 0)
        return -5;

    for (uint32_t i = 0u;
         i < sizeof(rscc_seq_words) / sizeof(rscc_seq_words[0]); ++i)
    {
        if (gpu_mmio_try_write32(rscc, rscc_x185_seq_mem + i * 4u,
                                 rscc_seq_words[i]) != 0)
            return -6;
    }

    /* On A7xx the PDC sequencer lives in AOP, so the host must only point
     * the PDC at it and enable the controller.  Complete every RSCC/PDC
     * write before reset release can observe the boot state. */
    if (gpu_mmio_try_write32(pdc, pdc_seq_start, 0u) != 0 ||
        gpu_mmio_try_write32(pdc, pdc_enable, 0x80000001u) != 0)
        return -7;
    asm_mmio_barrier();
    return 0;
}

const gpu_bringup_plan *adreno_x1_85_bringup_plan(void)
{
    return &g_adreno_x1_85_bringup_plan;
}
