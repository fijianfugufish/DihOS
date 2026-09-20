#include "gpu/adreno_x1_85.h"
#include "asm/asm.h"
#include "terminal/terminal_api.h"

static const gpu_firmware_file g_adreno_x1_85_firmware[] = {
    {"upstream/gen70500_gmu.bin", GPU_FIRMWARE_GMU, 1u, 81312u, 0u},
    /* The SQE file has a four-byte container header.  Stage its payload at
     * the naturally aligned instruction-buffer base, as the upstream driver
     * does, rather than making CP fetch that header. */
    {"upstream/gen70500_sqe.fw", GPU_FIRMWARE_SQE, 1u, 77332u, 4u},
    /* This machine's Lenovo-signed zap, staged from its active GPU package.
     * The generic reference-board signature is not interchangeable with OEM
     * signing. Do not silently fall back to the generic blob. */
    {"oem/qcdxkmsuc8380.mbn", GPU_FIRMWARE_SECURE, 1u, 12088u, 0u},
};

static const gpu_firmware_manifest g_adreno_x1_85_manifest = {
    "adreno-x1-85",
    "0:/OS/Firmware/adreno-x1-85",
    g_adreno_x1_85_firmware,
    (uint32_t)(sizeof(g_adreno_x1_85_firmware) / sizeof(g_adreno_x1_85_firmware[0])),
};

/* X1E's published OPP set in Gen7 HFI index order: the mandatory OFF state,
 * followed by the performance states from lowest to highest.  The power-vote
 * numbers are RPMh regulator levels, not voltages.  HFI uses these indices in
 * subsequent GX/BW votes, so this order must also match the bandwidth table.
 * Keeping those device facts here lets the common Gen7 HFI layer remain
 * reusable by a future GPU driver. */
static const gpu_gmu_hfi_gen7_perf_table g_adreno_x1_85_perf_table = {
    10u, 3u,
    {
        {  0u, 0xffffffffu,       0u},
        { 56u, 0xffffffffu,  300000u},
        { 64u, 0xffffffffu,  390000u},
        {128u, 0xffffffffu,  550000u},
        {192u, 0xffffffffu,  687000u},
        {224u, 0xffffffffu,  744000u},
        {256u, 0xffffffffu,  800000u},
        {320u, 0xffffffffu,  925000u},
        {384u, 0xffffffffu, 1000000u},
        {416u, 0xffffffffu, 1100000u},
    },
    {
        {  0u,      0u},
        { 64u, 220000u},
        {128u, 550000u},
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

const gpu_cp_register_layout *adreno_x1_85_cp_layout(void)
{
    static const gpu_cp_register_layout layout = {
        ADRENO_X1_85_CP_RB_BASE,
        ADRENO_X1_85_CP_RB_CNTL,
        ADRENO_X1_85_CP_RB_RPTR_ADDR,
        ADRENO_X1_85_CP_BV_RB_RPTR_ADDR,
        ADRENO_X1_85_CP_ADDR_MODE_CNTL,
        ADRENO_X1_85_CP_APRIV_CNTL,
        ADRENO_X1_85_CP_BV_APRIV_CNTL,
        ADRENO_X1_85_CP_LPAC_APRIV_CNTL,
        ADRENO_X1_85_CP_RB_RPTR,
        ADRENO_X1_85_CP_RB_WPTR,
        ADRENO_X1_85_CP_SQE_CNTL,
        ADRENO_X1_85_CP_SQE_INSTR_BASE,
        ADRENO_X1_85_CP_HW_FAULT,
        ADRENO_X1_85_CP_PROTECT_STATUS,
    };
    return &layout;
}

static int adreno_x1_85_write_host_state(const gpu_mmio_window *gfx,
                                         uint32_t offset, uint32_t value)
{
    return gpu_mmio_try_write32(gfx, offset, value) == 0 ? 0 : -1;
}

static int adreno_x1_85_write_host_state64(const gpu_mmio_window *gfx,
                                           uint32_t offset, uint64_t value)
{
    if (adreno_x1_85_write_host_state(gfx, offset, (uint32_t)value) ||
        adreno_x1_85_write_host_state(gfx, offset + 4u,
                                      (uint32_t)(value >> 32)))
        return -1;
    return 0;
}

typedef struct adreno_x1_85_reg_value
{
    uint32_t offset;
    uint32_t value;
} adreno_x1_85_reg_value;

/* X1-85 uses the upstream A740 hardware clock-gating programme.  These are
 * byte offsets, transcribed from its named register table after resolving
 * the Gen7 register database.  In particular, CLOCK_MODE_CP is required
 * before the command processor can consume its first ring. */
static const adreno_x1_85_reg_value g_adreno_x1_85_hwcg[] = {
    {0x000b0u * 4u, 0x02222222u}, {0x000b4u * 4u, 0x22022222u},
    {0x000bcu * 4u, 0x003cf3cfu}, {0x000b8u * 4u, 0x00000080u},
    {0x000c0u * 4u, 0x22222220u}, {0x000c4u * 4u, 0x22222222u},
    {0x000c8u * 4u, 0x22222222u}, {0x000ccu * 4u, 0x00222222u},
    {0x000e0u * 4u, 0x77777777u}, {0x000e4u * 4u, 0x77777777u},
    {0x000e8u * 4u, 0x77777777u}, {0x000ecu * 4u, 0x00077777u},
    {0x000d0u * 4u, 0x11111111u}, {0x000d4u * 4u, 0x11111111u},
    {0x000d8u * 4u, 0x11111111u}, {0x000dcu * 4u, 0x00011111u},
    {0x0010bu * 4u, 0x22222222u}, {0x0010cu * 4u, 0x00222222u},
    {0x00110u * 4u, 0x00000444u}, {0x0010fu * 4u, 0x00000222u},
    {0x000f0u * 4u, 0x22222222u}, {0x000f4u * 4u, 0x01002222u},
    {0x000f8u * 4u, 0x00002220u}, {0x00100u * 4u, 0x44000f00u},
    {0x00104u * 4u, 0x25222022u}, {0x00105u * 4u, 0x00555555u},
    {0x00106u * 4u, 0x00000011u}, {0x00107u * 4u, 0x00440044u},
    {0x00108u * 4u, 0x04222222u}, {0x00286u * 4u, 0x00000222u},
    {0x00285u * 4u, 0x00222222u}, {0x00114u * 4u, 0x02222223u},
    {0x00111u * 4u, 0x00222222u}, {0x00288u * 4u, 0x00222222u},
    {0x00287u * 4u, 0x00002222u}, {0x0010au * 4u, 0x00000000u},
    {0x00116u * 4u, 0x04104004u}, {0x00113u * 4u, 0x00000000u},
    {0x00109u * 4u, 0x00000000u}, {0x00115u * 4u, 0x00000200u},
    {0x00112u * 4u, 0x00000000u}, {0x0011bu * 4u, 0x00002222u},
    {0x0011cu * 4u, 0x00000000u}, {0x0011du * 4u, 0x00000000u},
    {0x00284u * 4u, 0x55555552u}, {0x0012fu * 4u, 0x00000000u},
    {0x00260u * 4u, 0x00000222u}, {0x000aeu * 4u, 0x8aa8aa82u},
    {0x00533u * 4u, 0x00000182u}, {0x00044u * 4u, 0x00000000u},
    {0x00042u * 4u, 0x00000000u}, {0x00118u * 4u, 0x00000222u},
    {0x00119u * 4u, 0x00000111u}, {0x0011au * 4u, 0x00000555u},
};

static int adreno_x1_85_prepare_memory_layout(const gpu_mmio_window *gfx)
{
    /* Register encodings: Mesa a6xx.xml; X1-85 bank bit: Mesa's device
     * profile. The host (not an unprivileged shader stream) owns NC setup.
     * All memory clients must agree, even when the image itself is linear
     * and UBWC is disabled. Do not inherit the Windows/firmware values.
     * See Linux a6xx_set_ubwc_config for the corresponding HW sequence. */
    static const adreno_x1_85_reg_value layout[] = {
        {ADRENO_X1_85_RB_NC_MODE_CNTL, ADRENO_X1_85_RB_NC_MODE_BOOT},
        {ADRENO_X1_85_TPL1_NC_MODE_CNTL, ADRENO_X1_85_TPL1_NC_MODE_BOOT},
        {ADRENO_X1_85_SP_NC_MODE_CNTL, ADRENO_X1_85_SP_NC_MODE_BOOT},
        {ADRENO_X1_85_UCHE_MODE_CNTL, ADRENO_X1_85_UCHE_MODE_BOOT},
    };
    for (uint32_t i = 0u; i < sizeof(layout) / sizeof(layout[0]); ++i)
    {
        uint32_t before, after;
        if (gpu_mmio_try_read32(gfx, layout[i].offset, &before) != 0 ||
            adreno_x1_85_write_host_state(gfx, layout[i].offset, layout[i].value) ||
            gpu_mmio_try_read32(gfx, layout[i].offset, &after) != 0)
            return -1;
        terminal_print("[K:GPU] NC layout register/before/after/expected=");
        terminal_print_inline_hex64(layout[i].offset / 4u);
        terminal_print_inline_hex64(before);
        terminal_print_inline_hex64(after);
        terminal_print_inline_hex64(layout[i].value);
        terminal_flush_log();
    }
    /* GRAS has separate BR and BV copies. Always restore the host aperture,
     * including on a guarded MMIO failure. Never leave subsequent accesses
     * directed at only one rendering pipe. */
    for (uint32_t pipe = 1u; pipe <= 2u; ++pipe)
    {
        int rc = adreno_x1_85_write_host_state(
            gfx, ADRENO_X1_85_CP_APERTURE_CNTL_HOST, pipe << 12);
        if (!rc)
            rc = adreno_x1_85_write_host_state(gfx,
                ADRENO_X1_85_GRAS_NC_MODE_CNTL, ADRENO_X1_85_GRAS_NC_MODE_BOOT);
        if (rc)
        {
            adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_CP_APERTURE_CNTL_HOST, 0u);
            return -2;
        }
    }
    return adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_CP_APERTURE_CNTL_HOST, 0u);
}

int adreno_x1_85_prepare_cp_host(const gpu_mmio_window *gfx)
{
    uint32_t rb_cmp_dbg;
    uint32_t gbif_ack;
    const uint64_t uche_l2_bypass_base = 0x0001fffffffff000ull;
    static const uint32_t bicubic_weights[] = {
        0u, 0x3fe05ff4u, 0x3fa0ebeeu, 0x3f5193edu, 0x3f0243f0u,
    };

    /* CP protection values are declarative register spans from the upstream
     * X1E/A730 profile.  Install them before CP starts so any later packet
     * cannot rewrite host-owned GX setup. */
    static const uint32_t protect[48] = {
        0x13fc0000u, 0x0160050bu, 0x8000050eu, 0x80000510u,
        0x80000534u, 0x027405fbu, 0x87a40699u, 0x802008a0u,
        0x809008abu, 0x800408deu, 0x052c08e7u, 0x81340900u,
        0x82c8098du, 0x86f80a41u, 0x80040df0u, 0x80000e01u,
        0x80200e07u, 0x830c3c00u, 0x7ffc3cc4u, 0x873c8630u,
        0x80008e00u, 0x80008e08u, 0x807c8e50u, 0x8a008e80u,
        0x876c9624u, 0x80009e40u, 0x80349e64u, 0x861c9e78u,
        0x873ca630u, 0x8000ae02u, 0x803cae50u, 0x800cae66u,
        0x800cae6fu, 0x800cb604u, 0xbffcec00u,
        0x7ffcfc00u, 0x814c8400u, 0x00108454u, 0xfffc8459u,
        0xfffca459u, 0xfffcc459u, 0x910cf400u, 0x01ecf844u,
        0x8001f860u, 0x80a8f878u, 0u, 0u, 0x8001f8c0u,
    };

    if (!gfx)
        return -1;

    /* A previous GX transition can leave either side of the memory fabric
     * halted.  The read-backs and barrier are intentional: CP fetch must not
     * race ahead of the fabric unhalt on the first command submission. */
    if (adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_GBIF_HALT, 0u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_RBBM_GBIF_HALT, 0u))
        return -2;
    if (gpu_mmio_try_read32(gfx, ADRENO_X1_85_GBIF_HALT, &rb_cmp_dbg) != 0 ||
        gpu_mmio_try_read32(gfx, ADRENO_X1_85_RBBM_GBIF_HALT,
                            &rb_cmp_dbg) != 0)
        return -3;
    asm_mmio_barrier();

    /* A7xx's documented unhalt sequence ends with the clear writes above;
     * it does not wait for GBIF_HALT_ACK to become zero.  On this X1E the
     * readback can retain the prior client/arb halt state (0x3) after a valid
     * clear, so treating it as a CP-start veto deadlocks bring-up before the
     * first real submission.  Record it for diagnostics but follow the
     * hardware sequence and let the guarded CP/SMMU paths report any fault. */
    if (gpu_mmio_try_read32(gfx, ADRENO_X1_85_GBIF_HALT_ACK, &gbif_ack) != 0)
        return -4;
    if (gbif_ack != 0u)
    {
        terminal_print("[K:GPU] GBIF halt ACK retained after unhalt=");
        terminal_print_inline_hex64(gbif_ack);
        terminal_print("; continuing A7xx CP bring-up");
        terminal_flush_log();
    }

    for (uint32_t i = 0u;
         i < sizeof(g_adreno_x1_85_hwcg) / sizeof(g_adreno_x1_85_hwcg[0]);
         ++i)
    {
        if (adreno_x1_85_write_host_state(gfx, g_adreno_x1_85_hwcg[i].offset,
                                           g_adreno_x1_85_hwcg[i].value))
            return -4;
    }
    asm_mmio_barrier();

    /* These six registers form the X1-85-specific prefix of the upstream
     * IFPC restore list.  Program them before publishing that list to CP so
     * an IFPC transition cannot restore uninitialised values. */
    for (uint32_t i = 0u; i < sizeof(bicubic_weights) / sizeof(bicubic_weights[0]); ++i)
    {
        if (adreno_x1_85_write_host_state(gfx,
                                          ADRENO_X1_85_TPL1_BICUBIC_BASE + i * 4u,
                                          bicubic_weights[i]))
            return -5;
    }

    /* DihOS has no secure rendering path yet.  Explicitly remove any stale
     * trusted range so its reset-time address aperture cannot cover normal
     * GPU IOVAs. */
    if (adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_SECVID_TSB_CNTL, 0u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_SECVID_TSB_BASE, 0u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_SECVID_TSB_BASE + 4u,
                                      0u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_SECVID_TSB_SIZE, 0u))
        return -5;

    /* On A7xx these UCHE bases are not allocation addresses: both receive
     * the documented high-address sentinel which disables L2 bypass for the
     * whole usable GPU virtual-address range.  Pointing them at a small
     * DihOS buffer can prevent CP from fetching its first ring command. */
    if (adreno_x1_85_write_host_state64(gfx,
                                        ADRENO_X1_85_UCHE_WRITE_THRU_BASE,
                                        uche_l2_bypass_base) ||
        adreno_x1_85_write_host_state64(gfx, ADRENO_X1_85_UCHE_TRAP_BASE,
                                        uche_l2_bypass_base) ||
        adreno_x1_85_write_host_state64(gfx,
                                        ADRENO_X1_85_UCHE_GMEM_RANGE_MIN,
                                        0x01000000ull) ||
        adreno_x1_85_write_host_state64(gfx,
                                        ADRENO_X1_85_UCHE_GMEM_RANGE_MAX,
                                        0x012fffffull) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_GBIF_QSB_SIDE0,
                                      0x00071620u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_GBIF_QSB_SIDE1,
                                      0x00071620u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_GBIF_QSB_SIDE2,
                                      0x00071620u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_GBIF_QSB_SIDE3,
                                      0x00071620u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_RBBM_GBIF_QOS,
                                      0x02120212u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_UCHE_GBIF_GX_CONFIG,
                                      0x010240e0u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_UCHE_CACHE_WAYS,
                                      1u << 23) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_CP_AHB_CNTL, 1u) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_UCHE_CMDQ_CONFIG,
                                      0x0006690eu) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_UCHE_CLIENT_PF,
                                      0x00000081u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_PERFCTR_CNTL, 1u) ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RBBM_INTERFACE_HANG_CNTL,
                                      0x40cfffffu) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_RBBM_BUSY_MASK,
                                      0xffffffffu) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_RBBM_INT_CLEAR,
                                      0xffffffffu) ||
        adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_RBBM_INT_MASK,
                                      ADRENO_X1_85_RBBM_INT_MASK_BOOT))
        return -6;

    if (adreno_x1_85_prepare_memory_layout(gfx) != 0)
        return -12;

    if (adreno_x1_85_write_host_state(gfx, ADRENO_X1_85_CP_PROTECT_CNTL,
                                      0x0000000bu))
        return -7;
    for (uint32_t i = 0u; i < 47u; ++i)
    {
        if (i == 45u || i == 46u)
            continue;
        if (adreno_x1_85_write_host_state(gfx,
                                          ADRENO_X1_85_CP_PROTECT_BASE + i * 4u,
                                          protect[i]))
            return -8;
    }
    if (adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_CP_PROTECT_BASE + 47u * 4u,
                                      protect[47u]))
        return -9;

    if (gpu_mmio_try_read32(gfx, ADRENO_X1_85_RB_CMP_DBG_ECO_CNTL,
                            &rb_cmp_dbg) != 0 ||
        adreno_x1_85_write_host_state(gfx,
                                      ADRENO_X1_85_RB_CMP_DBG_ECO_CNTL,
                                      rb_cmp_dbg | (1u << 11)))
        return -10;
    /* Do not submit a ring if host-owned state did not stick.  These reads
     * are deliberately narrow: they validate the L2 fetch route and the CP
     * protection gate without touching firmware-owned registers. */
    {
        uint32_t trap_lo;
        uint32_t trap_hi;
        uint32_t protect_cntl;

        if (gpu_mmio_try_read32(gfx, ADRENO_X1_85_UCHE_TRAP_BASE,
                                &trap_lo) != 0 ||
            gpu_mmio_try_read32(gfx, ADRENO_X1_85_UCHE_TRAP_BASE + 4u,
                                &trap_hi) != 0 ||
            gpu_mmio_try_read32(gfx, ADRENO_X1_85_CP_PROTECT_CNTL,
                                &protect_cntl) != 0)
            return -11;
        terminal_print("[K:GPU] UCHE trap readback=");
        terminal_print_inline_hex64((uint64_t)trap_lo |
                                    ((uint64_t)trap_hi << 32));
        terminal_print(" CP protect control=");
        terminal_print_inline_hex64(protect_cntl);
        terminal_flush_log();
        /* The Gen7 UCHE base registers are write-only on this X1E firmware
         * revision: valid writes read back as zero while CP protection does
         * read back.  The MMIO write itself succeeded, so record the fact but
         * let the real CP/SMMU fault paths decide whether a fetch is valid. */
        if (trap_lo != (uint32_t)uche_l2_bypass_base ||
            trap_hi != (uint32_t)(uche_l2_bypass_base >> 32))
            terminal_warn("[K:GPU] UCHE trap base is write-only; continuing CP probe");
        /* LAST_SPAN_INF_RANGE is a latch/command bit on this CP revision
         * and may read back clear even though protection was accepted.  The
         * host write is still valid; do not prevent initial CP fetch based
         * on that non-sticky bit. */
        if ((protect_cntl & 0x3u) != 0x3u)
            terminal_warn("[K:GPU] CP protection control readback is partial; continuing guarded CP probe");
    }
    asm_mmio_barrier();
    return 0;
}

int adreno_x1_85_build_cp_pwrup_record(const gpu_mmio_window *gfx,
                                       gpu_buffer *record)
{
    uint32_t *words;
    uint32_t ifpc_pairs = 0u;
    uint32_t pwrup_pairs = 0u;
    static const uint32_t ifpc_readback_regs[] = {
        ADRENO_X1_85_RBBM_PERFCTR_CNTL,
        ADRENO_X1_85_TPL1_NC_MODE_CNTL,
        ADRENO_X1_85_SP_NC_MODE_CNTL,
        ADRENO_X1_85_CP_DBG_ECO_CNTL,
        ADRENO_X1_85_CP_PROTECT_CNTL,
    };
    static const uint32_t bicubic_weights[] = {
        0u, 0x3fe05ff4u, 0x3fa0ebeeu, 0x3f5193edu, 0x3f0243f0u,
    };

    /* struct cpu_gpu_lock starts with three request/turn words followed by
     * u8 ifpc_list_len, u8 preemption_list_len and u16 dynamic_list_len.
     * The lists themselves hold (register-index, value) pairs. */
    /* 4 header + 58 IFPC pairs + 14 power-up pairs + 2 pipe triplets. */
    if (!gfx || !record || !record->cpu || record->size_bytes < 616u)
        return -1;
    words = (uint32_t *)record->cpu;
    for (uint32_t i = 0u; i < record->size_bytes / sizeof(*words); ++i)
        words[i] = 0u;

    for (uint32_t i = 0u; i < sizeof(bicubic_weights) / sizeof(bicubic_weights[0]); ++i)
    {
        words[4u + ifpc_pairs * 2u] =
            (ADRENO_X1_85_TPL1_BICUBIC_BASE / 4u) + i;
        words[5u + ifpc_pairs * 2u] = bicubic_weights[i];
        ++ifpc_pairs;
    }
    /* The X1-85/A750 IFPC list also preserves these host-programmed
     * controls.  Capture their actual values, rather than relying on reset
     * defaults, because CP restores this list after a power collapse. */
    for (uint32_t i = 0u;
         i < sizeof(ifpc_readback_regs) / sizeof(ifpc_readback_regs[0]); ++i)
    {
        uint32_t value;

        if (gpu_mmio_try_read32(gfx, ifpc_readback_regs[i], &value) != 0)
            return -2;
        /* Restore our programmed layout, not a potentially masked host
         * readback. The readbacks are reported during host initialization. */
        if (ifpc_readback_regs[i] == ADRENO_X1_85_TPL1_NC_MODE_CNTL)
            value = ADRENO_X1_85_TPL1_NC_MODE_BOOT;
        if (ifpc_readback_regs[i] == ADRENO_X1_85_SP_NC_MODE_CNTL)
            value = ADRENO_X1_85_SP_NC_MODE_BOOT;
        words[4u + ifpc_pairs * 2u] = ifpc_readback_regs[i] / 4u;
        words[5u + ifpc_pairs * 2u] = value;
        ++ifpc_pairs;
    }

    /* These protection registers are the stable, profile-owned part of the
     * X1E IFPC list.  They give CP a real spinlock-backed restore list rather
     * than the all-zero placeholder used during early bring-up. */
    for (uint32_t i = 0u; i < 48u; ++i)
    {
        uint32_t value;

        if (gpu_mmio_try_read32(gfx,
                                ADRENO_X1_85_CP_PROTECT_BASE + i * 4u,
                                &value) != 0)
            return -3;
        words[4u + ifpc_pairs * 2u] =
            (ADRENO_X1_85_CP_PROTECT_BASE / 4u) + i;
        words[5u + ifpc_pairs * 2u] = value;
        ++ifpc_pairs;
    }
    /* CP_ME_INIT distinguishes its IFPC and preemption sections.  Keep the
     * host-owned power-up registers in the latter instead of describing the
     * complete record as IFPC-only; that is the A7xx lock-record format. */
    {
        static const uint32_t pwrup_regs[] = {
            ADRENO_X1_85_UCHE_TRAP_BASE,
            ADRENO_X1_85_UCHE_TRAP_BASE + 4u,
            ADRENO_X1_85_UCHE_WRITE_THRU_BASE,
            ADRENO_X1_85_UCHE_WRITE_THRU_BASE + 4u,
            ADRENO_X1_85_UCHE_GMEM_RANGE_MIN,
            ADRENO_X1_85_UCHE_GMEM_RANGE_MIN + 4u,
            ADRENO_X1_85_UCHE_GMEM_RANGE_MAX,
            ADRENO_X1_85_UCHE_GMEM_RANGE_MAX + 4u,
            ADRENO_X1_85_UCHE_CACHE_WAYS,
            ADRENO_X1_85_UCHE_MODE_CNTL,
            ADRENO_X1_85_RB_NC_MODE_CNTL,
            ADRENO_X1_85_RB_CMP_DBG_ECO_CNTL,
            ADRENO_X1_85_UCHE_GBIF_GX_CONFIG,
            ADRENO_X1_85_UCHE_CLIENT_PF,
        };

        for (uint32_t i = 0u;
             i < sizeof(pwrup_regs) / sizeof(pwrup_regs[0]); ++i)
        {
            uint32_t value;

            if (gpu_mmio_try_read32(gfx, pwrup_regs[i], &value) != 0)
                return -4;
            if (pwrup_regs[i] == ADRENO_X1_85_UCHE_MODE_CNTL)
                value = ADRENO_X1_85_UCHE_MODE_BOOT;
            if (pwrup_regs[i] == ADRENO_X1_85_RB_NC_MODE_CNTL)
                value = ADRENO_X1_85_RB_NC_MODE_BOOT;
            words[4u + (ifpc_pairs + pwrup_pairs) * 2u] =
                pwrup_regs[i] / 4u;
            words[5u + (ifpc_pairs + pwrup_pairs) * 2u] = value;
            ++pwrup_pairs;
        }
    }
    if (ifpc_pairs > 0xffu || pwrup_pairs > 0xffu)
        return -4;
    /* Dynamic entries carry (pipe aperture, register index, value), unlike
     * the static pairs above. Preserve GRAS bank layout on both pipes. */
    for (uint32_t pipe = 1u; pipe <= 2u; ++pipe)
    {
        const uint32_t base = 4u + (ifpc_pairs + pwrup_pairs) * 2u + (pipe - 1u) * 3u;
        words[base] = pipe << 12;
        words[base + 1u] = ADRENO_X1_85_GRAS_NC_MODE_CNTL / 4u;
        words[base + 2u] = ADRENO_X1_85_GRAS_NC_MODE_BOOT;
    }
    words[3] = ifpc_pairs | (pwrup_pairs << 8) | (2u << 16);
    gpu_buffer_prepare_for_device(record);
    return 0;
}

static uint32_t adreno_packet_parity(uint32_t value)
{
    value ^= value >> 4;
    value ^= value >> 8;
    value ^= value >> 16;
    return (0x9669u >> (value & 0x0fu)) & 1u;
}

static uint32_t adreno_pkt7(uint32_t opcode, uint32_t count)
{
    return 0x70000000u | count |
           (adreno_packet_parity(count) << 15) |
           ((opcode & 0x7fu) << 16) |
           (adreno_packet_parity(opcode) << 23);
}

static uint32_t adreno_pkt4(uint32_t register_dword, uint32_t count)
{
    return 0x40000000u | count |
           (adreno_packet_parity(count) << 7) |
           ((register_dword & 0x3ffffu) << 8) |
           (adreno_packet_parity(register_dword) << 27);
}

int adreno_x1_85_emit_minimal_cp_init(gpu_command_ring *ring,
                                      uint64_t pwrup_record_iova)
{
    /* CP_THREAD_CONTROL, then CP_ME_INIT.  On A7xx, bit 8 in the CP init
     * mask enables the register-init spinlock list, so it must be paired
     * with the mapped list address and bit 31 in the final parameter. */
    uint32_t words[10];

    if (!pwrup_record_iova)
        return -1;

    words[0] = adreno_pkt7(0x17u, 1u);
    words[1] = 0x08000000u;
    words[2] = adreno_pkt7(0x48u, 7u);
    words[3] = 0x0000014bu;
    words[4] = 0x00000003u;
    words[5] = 0x20000000u;
    words[6] = 0x00000002u;
    words[7] = (uint32_t)pwrup_record_iova;
    words[8] = (uint32_t)(pwrup_record_iova >> 32);
    words[9] = 0x80000000u;
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_nonsecure_transition(gpu_command_ring *ring)
{
    /* Standard Gen7 CP transition (adreno_pm4.xml). The firmware performs
     * the secure handoff; no direct write of the protected trust register.
     * WFI/WFM keeps subsequent diagnostics after its completion. */
    const uint32_t words[] = {
        adreno_pkt7(0x66u, 1u), 0u,
        adreno_pkt7(0x26u, 0u),
        adreno_pkt7(0x13u, 0u),
    };
    if (!ring)
        return -1;
    return gpu_ring_emit_many(ring, words, sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_cp_memory_probe(gpu_command_ring *ring,
                                      uint64_t destination_iova,
                                      uint32_t value)
{
    /* CP_MEM_WRITE has a 64-bit destination followed by one 32-bit word.
     * It is issued after CP_ME_INIT so this verifies normal SQE packet
     * execution and the render SMMU domain, without touching graphics state. */
    uint32_t words[4];

    if (!destination_iova || (destination_iova & 3u))
        return -1;
    words[0] = adreno_pkt7(0x3du, 3u); /* CP_MEM_WRITE */
    words[1] = (uint32_t)destination_iova;
    words[2] = (uint32_t)(destination_iova >> 32);
    words[3] = value;
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_cp_memory_copy_u32(gpu_command_ring *ring,
                                         uint64_t destination_iova,
                                         uint64_t source_iova)
{
    /* Mesa emits CP_MEM_TO_MEM (opcode 0x73) as control, 64-bit destination,
     * then 64-bit source.  A zero control word selects a plain 32-bit copy;
     * this executes after the preceding CP_WAIT_FOR_IDLE/cache clean in the
     * direct-sysmem finish path. */
    uint32_t words[6];

    if (!ring || !destination_iova || !source_iova ||
        (destination_iova & 3u) || (source_iova & 3u))
        return -1;
    words[0] = adreno_pkt7(0x73u, 5u); /* CP_MEM_TO_MEM */
    words[1] = 0u;
    words[2] = (uint32_t)destination_iova;
    words[3] = (uint32_t)(destination_iova >> 32);
    words[4] = (uint32_t)source_iova;
    words[5] = (uint32_t)(source_iova >> 32);
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_rb_done_fence(gpu_command_ring *ring,
                                    uint64_t destination_iova,
                                    uint32_t value)
{
    /* RB_DONE_TS writes this known value only after RB completes. CP's own
     * WAIT_REG_MEM stalls this early direct-sysmem path on X1-85, so the
     * kernel observes the timestamp with a bounded CPU-side poll after the
     * command ring itself has drained. */
    uint32_t words[5];

    if (!ring || !destination_iova || (destination_iova & 3u))
        return -1;

    words[0] = adreno_pkt7(0x46u, 4u); /* CP_EVENT_WRITE7 */
    words[1] = 0x08000016u;            /* RB_DONE_TS, user-32b, RAM write */
    words[2] = (uint32_t)destination_iova;
    words[3] = (uint32_t)(destination_iova >> 32);
    words[4] = value;
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_primitive_count_snapshot(gpu_command_ring *ring,
                                                uint64_t destination_iova,
                                                uint8_t final_snapshot)
{
    uint32_t words[8];
    uint32_t count = 5u;

    /* This is Mesa's A6XX/A7XX transform-feedback query mechanism: VPC's
     * counter event writes four stream slots beginning at this 32-byte
     * aligned base.  DihOS reads only stream zero's first pair (written,
     * generated), but reserves the whole 64-byte hardware result range. */
    if (!ring || !destination_iova || (destination_iova & 31u))
        return -1;
    words[0] = adreno_pkt4(0x9218u, 2u); /* VPC_SO_QUERY_BASE lo/hi */
    words[1] = (uint32_t)destination_iova;
    words[2] = (uint32_t)(destination_iova >> 32);
    words[3] = adreno_pkt7(0x46u, 1u);  /* CP_EVENT_WRITE7 */
    words[4] = 0x09u;                   /* WRITE_PRIMITIVE_COUNTS */
    if (final_snapshot)
    {
        /* Match Mesa's end-query ordering: make the VPC write complete, then
         * clean UCHE so an EL1 cache invalidate observes the DMA result. */
        words[5] = adreno_pkt7(0x26u, 0u); /* CP_WAIT_FOR_IDLE */
        words[6] = adreno_pkt7(0x46u, 1u); /* CP_EVENT_WRITE7 */
        words[7] = 0x31u;                 /* CACHE_CLEAN on A7xx */
        count = 8u;
    }
    return gpu_ring_emit_many(ring, words, count);
}

int adreno_x1_85_emit_register_snapshot(gpu_command_ring *ring,
                                        uint32_t register_dword_offset,
                                        uint32_t register_count,
                                        uint64_t destination_iova)
{
    uint32_t words[4];

    /* CP_REG_TO_MEM's first payload encodes a raw register offset in bits
     * 0..17 and the dword count in bits 18..29.  Mesa uses this packet on
     * A7xx for query snapshots; this narrow wrapper is deliberately limited
     * to a small, kernel-selected consecutive range. */
    if (!ring || !destination_iova || (destination_iova & 3u) ||
        register_dword_offset > 0x3ffffu || !register_count ||
        register_count > 0xfffu ||
        register_dword_offset > 0x3ffffu - (register_count - 1u))
        return -1;
    words[0] = adreno_pkt7(0x3eu, 3u); /* CP_REG_TO_MEM */
    words[1] = register_dword_offset | (register_count << 18);
    words[2] = (uint32_t)destination_iova;
    words[3] = (uint32_t)(destination_iova >> 32);
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}

int adreno_x1_85_emit_cp_scanout_triangle(gpu_command_ring *ring,
                                          uint64_t target_iova,
                                          uint32_t target_bytes,
                                          uint32_t width,
                                          uint32_t height,
                                          uint32_t pitch)
{
    enum { triangle_height = 64u, triangle_max_width = 127u };
    const uint32_t color = 0xffff00ffu; /* opaque magenta in BGRX memory */
    const uint32_t center_x = width / 2u;
    const uint32_t first_y = (height - triangle_height) / 2u;

    if (!ring || !target_iova || width < triangle_max_width ||
        height < triangle_height || pitch < width * 4u ||
        target_bytes < (uint64_t)pitch * height)
        return -1;

    /* Each row is one CP_MEM_WRITE packet: its two address dwords are
     * followed by the row's odd number of pixels.  The complete 64-row
     * triangle is only 4,289 ring dwords including the final write barrier. */
    for (uint32_t row = 0u; row < triangle_height; ++row)
    {
        uint32_t words[3u + triangle_max_width];
        const uint32_t pixels = row * 2u + 1u;
        const uint32_t x = center_x - row;
        const uint64_t offset = (uint64_t)(first_y + row) * pitch +
                                (uint64_t)x * 4u;
        const uint64_t address = target_iova + offset;

        words[0] = adreno_pkt7(0x3du, 2u + pixels); /* CP_MEM_WRITE */
        words[1] = (uint32_t)address;
        words[2] = (uint32_t)(address >> 32);
        for (uint32_t pixel = 0u; pixel < pixels; ++pixel)
            words[3u + pixel] = color;
        if (gpu_ring_emit_many(ring, words, 3u + pixels) != 0)
            return -2;
    }
    return gpu_ring_emit(ring, adreno_pkt7(0x12u, 0u)); /* CP_WAIT_MEM_WRITES */
}

int adreno_x1_85_emit_cp_constants(gpu_command_ring *ring,
                                   adreno_x1_85_constant_stage stage,
                                   uint32_t destination_vec4,
                                   const uint32_t *values,
                                   uint32_t vec4_count)
{
    uint32_t header;
    uint32_t opcode;
    uint32_t state_block;
    uint32_t payload_dwords;

    /* CP_LOAD_STATE6 encodes a 14-bit vec4 destination and a 10-bit unit
     * count. These values come from Mesa's adreno_pm4.xml description:
     * constants=1, direct=0, VS shader block=8, FS shader block=12. */
    if (!ring || !values || !vec4_count || destination_vec4 > 0x3fffu ||
        vec4_count > 0x3ffu || vec4_count > 0x3fffu - destination_vec4)
        return -1;
    if (stage == ADRENO_X1_85_CONSTANT_VERTEX)
    {
        opcode = 0x32u; /* CP_LOAD_STATE6_GEOM */
        state_block = 0x8u; /* SB6_VS_SHADER */
    }
    else if (stage == ADRENO_X1_85_CONSTANT_FRAGMENT)
    {
        opcode = 0x34u; /* CP_LOAD_STATE6_FRAG */
        state_block = 0xcu; /* SB6_FS_SHADER */
    }
    else
        return -2;

    /* Type-7 payload: state word, unused EXT_SRC_ADDR low/high for direct
     * data, then four dwords per vec4.  The caller has already copied any
     * float values into a kernel buffer; no CPU pointer is DMA-visible here. */
    payload_dwords = 3u + vec4_count * 4u;
    header = destination_vec4 | (1u << 14) | (state_block << 18) |
             (vec4_count << 22);
    if (gpu_ring_emit(ring, adreno_pkt7(opcode, payload_dwords)) != 0 ||
        gpu_ring_emit(ring, header) != 0 ||
        gpu_ring_emit(ring, 0u) != 0 || gpu_ring_emit(ring, 0u) != 0 ||
        gpu_ring_emit_many(ring, values, vec4_count * 4u) != 0)
        return -3;
    return 0;
}

int adreno_x1_85_emit_cp_constant_ubo(gpu_command_ring *ring,
                                      adreno_x1_85_constant_stage stage,
                                      uint32_t ubo_index,
                                      uint64_t source_gpu_va,
                                      uint32_t size_vec4s)
{
    uint32_t opcode;
    uint32_t state_block;
    uint32_t header;
    uint64_t descriptor;

    /* This mirrors Mesa Turnip's tu6_emit_xs(): CP_LOAD_STATE6 creates a
     * direct ST6_UBO descriptor with a five-dword payload.  DihOS keeps the
     * descriptor construction here, after manifest/MIR3 validation, instead
     * of accepting any descriptor or packet bytes from the renderer. */
    if (!ring || !source_gpu_va || (source_gpu_va & 15u) ||
        (source_gpu_va >> 32) != 0u ||
        ubo_index > 0x3fffu || !size_vec4s || size_vec4s > 0xffffu)
        return -1;
    if (stage == ADRENO_X1_85_CONSTANT_VERTEX)
    {
        opcode = 0x32u;      /* CP_LOAD_STATE6_GEOM */
        state_block = 0x8u;  /* SB6_VS_SHADER */
    }
    else if (stage == ADRENO_X1_85_CONSTANT_FRAGMENT)
    {
        opcode = 0x34u;      /* CP_LOAD_STATE6_FRAG */
        state_block = 0xcu;  /* SB6_FS_SHADER */
    }
    else
        return -2;

    header = ubo_index | (2u << 14) | (state_block << 18) | (1u << 22);
    /* A6XX_UBO_DESC is not simply { address, size }.  Its high dword is
     * BASE_HI in bits 32..48 and SIZE (in vec4 units) in bits 49..63.  The
     * former encoding put SIZE at bit 32, which selected an invalid high
     * address and a zero-length UBO.  That lets the CP retire the draw while
     * the shaders read no constant pool, collapsing the GLSL triangle before
     * rasterization.  The admitted arena remains below 4 GiB, but preserve
     * the real descriptor layout for future higher GPU VAs as well. */
    descriptor = source_gpu_va | ((uint64_t)size_vec4s << 49);
    if (gpu_ring_emit(ring, adreno_pkt7(opcode, 5u)) != 0 ||
        gpu_ring_emit(ring, header) != 0 || gpu_ring_emit(ring, 0u) != 0 ||
        gpu_ring_emit(ring, 0u) != 0 ||
        gpu_ring_emit(ring, (uint32_t)descriptor) != 0 ||
        gpu_ring_emit(ring, (uint32_t)(descriptor >> 32)) != 0)
        return -3;
    return 0;
}

int adreno_x1_85_emit_context_regs(gpu_command_ring *ring,
                                   const adreno_x1_85_context_reg *registers,
                                   uint32_t register_count)
{
    if (!ring || !registers || !register_count ||
        register_count > 0x3fffu / 2u)
        return -1;
    /* CP_CONTEXT_REG_BUNCH (0x5c) payload is exactly (register,value) pairs.
     * It lets the state builder keep Mesa's non-contiguous A7xx register
     * layout explicit, rather than relying on a loose raw packet stream. */
    if (gpu_ring_emit(ring, adreno_pkt7(0x5cu, register_count * 2u)) != 0)
        return -2;
    for (uint32_t i = 0u; i < register_count; ++i)
        if (gpu_ring_emit(ring, registers[i].dword_offset) != 0 ||
            gpu_ring_emit(ring, registers[i].value) != 0)
            return -3;
    return 0;
}

int adreno_x1_85_emit_non_context_regs(
    gpu_command_ring *ring, const adreno_x1_85_context_reg *registers,
    uint32_t register_count)
{
    if (!ring || !registers || !register_count ||
        register_count > (0x3fffu - 2u) / 2u)
        return -1;
    /* CP_NON_CONTEXT_REG_BUNCH (0x5d) is the A7xx form used by Mesa's
     * fd_ncrb builder. Its leading 1,0 pair is part of the packet contract;
     * it is not a register/value supplied by a caller. */
    if (gpu_ring_emit(ring, adreno_pkt7(0x5du, 2u + register_count * 2u)) != 0 ||
        gpu_ring_emit(ring, 1u) != 0 || gpu_ring_emit(ring, 0u) != 0)
        return -2;
    for (uint32_t i = 0u; i < register_count; ++i)
        if (gpu_ring_emit(ring, registers[i].dword_offset) != 0 ||
            gpu_ring_emit(ring, registers[i].value) != 0)
            return -3;
    return 0;
}

int adreno_x1_85_emit_auto_triangle_draw(gpu_command_ring *ring)
{
    uint32_t words[6];

    if (!ring)
        return -1;
    /* Mesa emits CP_DRAW_INDX_OFFSET with the auto-index source for an
     * input-free gl_VertexID triangle. DI_PT_TRILIST is 4 and AUTO_INDEX is
     * 2 (bits 6..7).  Direct-sysmem setup enables CP's visibility override,
     * so this initiator must select USE_VISIBILITY (bit 8), just like Mesa's
     * fd6_draw/Turnip direct draw path.  Selecting IGNORE_VISIBILITY here
     * leaves the A7xx render backend without the direct-pass visibility
     * contract even though CP consumes the packet. */
    /* CP state survives display firmware and prior contexts.  In particular,
     * a failing inherited draw predicate makes CP consume a draw and its
     * following RB_DONE event while launching no raster work.  Normal Mesa
     * contexts own this state; DihOS must establish it explicitly. */
    words[0] = adreno_pkt7(0x19u, 1u); /* CP_DRAW_PRED_ENABLE_GLOBAL */
    words[1] = 0u;                     /* predication disabled */
    words[2] = adreno_pkt7(0x38u, 3u); /* CP_DRAW_INDX_OFFSET */
    words[3] = 4u | (2u << 6) | (1u << 8);
                                           /* TRI list, auto index, use VSC */
    words[4] = 1u;
    words[5] = 3u;
    return gpu_ring_emit_many(ring, words,
                              sizeof(words) / sizeof(words[0]));
}
