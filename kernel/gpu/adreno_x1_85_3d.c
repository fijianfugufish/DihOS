#include "gpu/adreno_x1_85_3d.h"

#include "gpu/adreno_x1_85.h"
#include "mesart/mesart_shader.h"

/*
 * Small, source-attributed subset of Mesa's generated a6xx.xml definitions.
 * These are dword register offsets, not MMIO byte offsets.  Keeping this
 * subset here makes the privileged state the reviewable kernel source instead
 * of copying a generated header into an EL0 bundle.
 */
#define A7XX_GRAS_CL_CNTL             0x8000u
#define A7XX_GRAS_SC_SCREEN_SCISSOR   0x80b0u
#define A7XX_GRAS_SC_VIEWPORT_SCISSOR 0x80d0u
#define A7XX_GRAS_SC_BIN_CNTL          0x80a1u
#define A7XX_GRAS_MODE_CNTL           0x8110u
#define A7XX_GRAS_SU_CNTL              0x8090u
#define A7XX_GRAS_SU_RENDER_CNTL       0x8116u
#define A7XX_RB_CNTL                   0x8800u
#define A7XX_RB_RENDER_CNTL           0x8801u
#define A7XX_RB_PS_OUTPUT_CNTL         0x880bu
#define A7XX_RB_PS_MRT_CNTL            0x880cu
#define A7XX_RB_PS_OUTPUT_MASK         0x880du
#define A7XX_RB_SRGB_CNTL              0x880fu
#define A7XX_RB_BUFFER_CNTL           0x8812u
#define A7XX_RB_CLEAR_TARGET          0x88e4u
#define A7XX_RB_MRT_CONTROL           0x8820u
#define A7XX_RB_MRT_BUF_INFO           0x8822u
#define A7XX_RB_MRT_PITCH              0x8823u
#define A7XX_RB_MRT_ARRAY_PITCH        0x8824u
#define A7XX_RB_MRT_BASE               0x8825u
#define A7XX_RB_MRT_BASE_GMEM          0x8827u
#define A7XX_VPC_VS_CLIP_CULL_CNTL     0x9101u
#define A7XX_VPC_VS_CLIP_CULL_CNTL_V2  0x9311u
#define A7XX_VPC_VS_SIV_CNTL           0x9104u
#define A7XX_VPC_VS_SIV_CNTL_V2        0x9314u
#define A7XX_VPC_VARYING_LM_TRANSFER   0x9212u
#define A7XX_VPC_RAST_CNTL              0x9108u
#define A7XX_VPC_PC_CNTL                0x9109u
#define A7XX_VPC_VS_CNTL               0x9301u
#define A7XX_VPC_PS_CNTL               0x9304u
#define A7XX_VPC_SO_OVERRIDE           0x9306u
#define A7XX_VPC_PS_RAST_CNTL           0x9307u
#define A7XX_PC_RESTART_INDEX           0x9803u
#define A7XX_PC_DGEN_RAST_CNTL          0x9809u
#define A7XX_PC_DGEN_SU_CONSERVATIVE_RAS_CNTL 0x980au
#define A7XX_PC_CNTL                    0x9b00u
#define A7XX_PC_VS_CNTL                0x9b01u
#define A7XX_PC_PS_CNTL                0x9806u
#define A7XX_GRAS_SU_VS_SIV_CNTL       0x809bu
#define A7XX_VFD_CNTL_0                0xa000u
#define A7XX_VFD_CNTL_1                0xa001u
#define A7XX_VFD_CNTL_2                0xa002u
#define A7XX_VFD_CNTL_3                0xa003u
#define A7XX_VFD_CNTL_4                0xa004u
#define A7XX_VFD_CNTL_5                0xa005u
#define A7XX_VFD_CNTL_6                0xa006u
#define A7XX_VFD_RENDER_MODE           0xa007u
#define A7XX_VFD_MODE_CNTL             0xa009u
#define A7XX_SP_VS_CNTL_0              0xa800u
#define A7XX_SP_VS_OUTPUT_CNTL         0xa802u
#define A7XX_SP_VS_OUTPUT_REG          0xa803u
#define A7XX_SP_VS_VPC_DEST_REG        0xa813u
#define A7XX_SP_VS_BOOLEAN_CF_MASK      0xa801u
#define A7XX_SP_VS_PROGRAM_COUNTER      0xa81bu
#define A7XX_SP_VS_BASE                0xa81cu
#define A7XX_SP_VS_PVT_MEM_PARAM        0xa81eu
#define A7XX_SP_VS_PVT_MEM_BASE         0xa81fu
#define A7XX_SP_VS_PVT_MEM_SIZE         0xa821u
#define A7XX_SP_VS_TSIZE                0xa822u
#define A7XX_SP_VS_CONST_CONFIG        0xa827u
#define A7XX_SP_VS_PVT_MEM_STACK_OFFSET 0xa825u
#define A7XX_SP_VS_VGS_CNTL             0xa82du
#define A7XX_SP_HS_CONFIG               0xa83bu
#define A7XX_SP_HS_CONST_CONFIG         0xa83fu
#define A7XX_SP_DS_CONFIG               0xa863u
#define A7XX_SP_DS_CONST_CONFIG         0xa867u
#define A7XX_SP_GS_CONFIG               0xa894u
#define A7XX_SP_GS_CONST_CONFIG         0xa898u
#define A7XX_SP_PS_CNTL_0              0xa980u
#define A7XX_SP_PS_BOOLEAN_CF_MASK      0xa981u
#define A7XX_SP_PS_PROGRAM_COUNTER      0xa982u
#define A7XX_SP_PS_OUTPUT_MASK         0xa98bu
#define A7XX_SP_PS_OUTPUT_CNTL          0xa98cu
#define A7XX_SP_SRGB_CNTL              0xa98au
#define A7XX_SP_PS_MRT_CNTL            0xa98du
#define A7XX_SP_PS_OUTPUT_REG          0xa98eu
#define A7XX_SP_PS_MRT_REG             0xa996u
#define A7XX_SP_PS_BASE                0xa983u
#define A7XX_SP_PS_PVT_MEM_PARAM        0xa985u
#define A7XX_SP_PS_PVT_MEM_BASE         0xa986u
#define A7XX_SP_PS_PVT_MEM_SIZE         0xa988u
#define A7XX_SP_BLEND_CNTL              0xa989u
#define A7XX_SP_PS_TSIZE                0xa9a7u
#define A7XX_SP_PS_PVT_MEM_STACK_OFFSET 0xa9a9u
#define A7XX_SP_PS_VGS_CNTL             0xaa01u
#define A7XX_SP_PS_OUTPUT_CONST_CNTL     0xaa02u
#define A7XX_SP_PS_OUTPUT_CONST_MASK     0xaa03u
#define A7XX_SP_PS_INITIAL_TEX_LOAD_CNTL 0xa99eu
#define A7XX_SP_PS_CNTL_1                0xa9aeu
#define A7XX_SP_PS_WAVE_CNTL             0xa9c6u
#define A7XX_SP_LB_PARAM_LIMIT           0xa9c7u
#define A7XX_SP_REG_PROG_ID_0            0xa9c8u
#define A7XX_SP_REG_PROG_ID_1            0xa9c9u
#define A7XX_SP_REG_PROG_ID_2            0xa9cau
#define A7XX_SP_REG_PROG_ID_3            0xa9cbu
#define A7XX_SP_PS_CONST_CONFIG        0xab03u
#define A7XX_SP_PS_CONFIG              0xab04u
#define A7XX_SP_PS_INSTR_SIZE          0xab05u
#define A7XX_SP_MODE_CNTL              0xab00u
#define A7XX_SP_RENDER_CNTL            0xa9aau
#define A7XX_SP_UPDATE_CNTL             0xab1fu
#define A7XX_SP_GFX_UAV_BASE             0xab1au
#define A7XX_SP_GFX_USIZE                0xab20u
#define A7XX_SP_VS_CONFIG               0xa823u
#define A7XX_SP_VS_INSTR_SIZE           0xa824u
#define A7XX_TPL1_MODE_CNTL             0xb309u

#define A7XX_FMT_8_8_8_8_UNORM         0x30u
#define A7XX_SWAP_WXYZ                  0x1u
#define A7XX_TILE_LINEAR                0u
#define A7XX_RENDERING_PASS             0u

#define A7XX_SP_XS_ENABLED              0x00000100u
#define A7XX_SP_PS_INOUT_REG_OVERLAP    0x01000000u
#define A7XX_SP_UPDATE_ALL_STATE         0x000000ffu
#define A7XX_TPL1_MODE_GL                0x000000a2u
#define A7XX_CONST_ENABLED               0x00000100u
#define A7XX_RB_ALL_BUFFERS_SYSMEM      0x000003ffu
#define A7XX_RENDER_BUFFERS_SYSMEM      (3u << 22)
#define A7XX_RB_MRT_COMPONENT_RGBA      (0xfu << 7)
#define A7XX_VPC_SO_ENABLE               0x00000000u
#define A7XX_GRAS_CL_Z_CLAMP             0x00000020u
#define A7XX_GRAS_CL_VP_CLIP_IGNORE      0x00000080u
#define A7XX_VPC_INVALID_LOCATION         0x0000ffffu
#define A7XX_VPC_INVALID_SIV_LOCATION     0x00ffffffu
#define A7XX_INVALID_REGID                 0xfcu

static uint32_t a7xx_odd_parity(uint32_t value)
{
    value ^= value >> 4;
    value ^= value >> 8;
    value ^= value >> 16;
    return (0x9669u >> (value & 0x0fu)) & 1u;
}

static uint32_t a7xx_pkt7(uint32_t opcode, uint32_t count)
{
    return 0x70000000u | count |
           (a7xx_odd_parity(count) << 15) |
           ((opcode & 0x7fu) << 16) |
           (a7xx_odd_parity(opcode) << 23);
}

static int a7xx_emit_event(gpu_command_ring *ring, uint32_t event)
{
    /* Gen7 CP_EVENT_WRITE7 events without timestamps have a one-dword
     * payload. The event values are from Mesa's adreno_pm4.xml: CCU
     * invalidate depth/color (24/25), LRZ invalidate (40), cache invalidate
     * (51), and colour clean (33). */
    if (!ring || event > 0xffu)
        return -1;
    if (gpu_ring_emit(ring, a7xx_pkt7(0x46u, 1u)) != 0 ||
        gpu_ring_emit(ring, event) != 0)
        return -2;
    return 0;
}

static int a7xx_emit_wait_for_idle(gpu_command_ring *ring)
{
    return ring ? gpu_ring_emit(ring, a7xx_pkt7(0x26u, 0u)) : -1;
}

int adreno_x1_85_3d_emit_x1e_baseline(gpu_command_ring *ring)
{
    /* Exact `a740_raw_magic_regs` values from Mesa's
     * freedreno_devices.py X1-85 entry (chip 0xffff43050c01).  They are a
     * driver-owned hardware profile, never a Mesa renderer input. */
    static const adreno_x1_85_context_reg baseline[] = {
        /* A7xx's static CCU setup: X1-85 has six CCUs, 256 KiB depth cache
         * per CCU and 64 KiB colour cache per CCU in sysmem mode. */
        {0x8e07u, 0x00000001u}, {0x88e5u, 0xc0000000u},
        {0x0e17u, 0x00040004u}, {0xb600u, 0x11100000u},
        {0xb602u, 0x00040724u}, {0xae03u, 0x10001400u},
        {0xae08u, 0x00400400u}, {0xae09u, 0x00430800u},
        {0xae0au, 0x00000000u}, {0x0e10u, 0x00000000u},
        {0x0e11u, 0x00000000u}, {0xae6cu, 0x00000000u},
        {0xae00u, 0x10000000u}, {0x9804u, 0x00001f1fu},
        {0x9e00u, 0x00100000u}, {0x9e24u, 0x21585600u},
        {0xa600u, 0x00008000u}, {0xae06u, 0x00000000u},
        {0xae6au, 0x00000000u}, {0xae6bu, 0x00000080u},
        {0xae73u, 0x00000000u}, {0xab02u, 0x00000000u},
        {0xab01u, 0x00000000u}, {0xab22u, 0x00000000u},
        {0xb310u, 0x00000000u}, {0x0ce2u, 0x00000000u},
        {0x0ce3u, 0x00000000u}, {0x0ce4u, 0x00000000u},
        {0x0ce5u, 0x00000000u}, {0x0ce6u, 0x00000000u},
        {0x0ce7u, 0x00000000u}, {0x80a7u, 0x00000000u},
        {0x8600u, 0x00004800u}, {0x8e79u, 0x00000000u},
        {0x8899u, 0x00000000u}, {0x8e06u, 0x02080000u},
        {0x9600u, 0x02000000u}, {0x0e12u, 0x00000000u},
    };

    return adreno_x1_85_emit_non_context_regs(
        ring, baseline, sizeof(baseline) / sizeof(baseline[0]));
}

int adreno_x1_85_3d_emit_sysmem_prologue(gpu_command_ring *ring)
{
    /* Clearing all four words of every VFD vertex-buffer descriptor takes
     * 128 writes.  The previous one-word loop cleared only SIZE, leaving a
     * firmware-owned BASE/STRIDE live below a zero fetch count.  An
     * input-free draw must not inherit any vertex-DMA address state. */
    adreno_x1_85_context_reg regs[160];
    uint32_t count = 0u;

    if (!ring)
        return -1;
    /* Matches the Gen7 direct-render restore ordering in fd6_emit_restore
     * and fd6_emit_sysmem_prep, without a generic draw-state or descriptor
     * interface. */
    if (gpu_ring_emit(ring, a7xx_pkt7(0x17u, 1u)) != 0 ||
        gpu_ring_emit(ring, 0x08000001u) != 0 ||
        a7xx_emit_event(ring, 25u) != 0 ||
        a7xx_emit_event(ring, 24u) != 0 ||
        a7xx_emit_event(ring, 40u) != 0 ||
        a7xx_emit_event(ring, 51u) != 0 ||
        a7xx_emit_wait_for_idle(ring) != 0 ||
        gpu_ring_emit(ring, a7xx_pkt7(0x63u, 1u)) != 0 ||
        gpu_ring_emit(ring, 0u) != 0 ||
        /* Mesa marks the stream as direct sysmem rendering before a draw.
         * On A7xx this is CP state, not a VFD register; without it CP can
         * consume a draw packet while RB never launches the render pass. */
        gpu_ring_emit(ring, a7xx_pkt7(0x65u, 1u)) != 0 ||
        gpu_ring_emit(ring, 1u) != 0 || /* RM6_DIRECT_RENDER */
        gpu_ring_emit(ring, a7xx_pkt7(0x1du, 1u)) != 0 ||
        gpu_ring_emit(ring, 0u) != 0 ||
        gpu_ring_emit(ring, a7xx_pkt7(0x23u, 1u)) != 0 ||
        gpu_ring_emit(ring, 1u) != 0 ||
        gpu_ring_emit(ring, a7xx_pkt7(0x64u, 1u)) != 0 ||
        gpu_ring_emit(ring, 1u) != 0)
        return -2;

#define STATIC_REG(offset, value) do { regs[count++] = (adreno_x1_85_context_reg){ (offset), (value) }; } while (0)
    /* Mesa fd6_emit_static_context_regs, pruned only for functionality the
     * signed first profile cannot use (tessellation, streamout, descriptors,
     * depth/stencil). Explicitly clearing VFD buffers matters: it prevents a
     * stale prior context from fetching an inherited GPU address. */
    STATIC_REG(0xa009u, 0x3u);       /* VFD_MODE_CNTL: vertex + instance */
    STATIC_REG(0x8811u, 0x10u);      /* RB_MODE_CNTL */
    STATIC_REG(0x8101u, 0u);         /* GRAS_LRZ_PS_INPUT_CNTL */
    STATIC_REG(0x8109u, 0u);         /* GRAS_LRZ_PS_SAMPLEFREQ_CNTL */
    STATIC_REG(A7XX_GRAS_MODE_CNTL, 0x2u);
    STATIC_REG(0x8818u, 0u);         /* RB_UNKNOWN_8818 */
    STATIC_REG(0x9236u, 0u);         /* VPC_REPLACE_MODE_CNTL */
    STATIC_REG(0x9300u, 0u);         /* VPC_ROTATION_CNTL */
    /* fd6_emit_sysmem_prep enables stream-out for its one direct-render
     * pass.  A value of one is explicitly the VPC_SO_OVERRIDE.DISABLE bit. */
    STATIC_REG(A7XX_VPC_SO_OVERRIDE, A7XX_VPC_SO_ENABLE);
    STATIC_REG(0x9107u, 0u);         /* VPC_RAST_STREAM_CNTL */
    STATIC_REG(0x9317u, 0u);         /* VPC_RAST_STREAM_CNTL_V2 */
    STATIC_REG(0x9b07u, 0u);         /* PC_STEREO_RENDERING_CNTL */
    STATIC_REG(0xb183u, 0u);         /* TPL1_PS_SWIZZLE_CNTL */
    STATIC_REG(0x8099u, 0u);         /* GRAS_SU_CONSERVATIVE_RAS_CNTL */
    STATIC_REG(0x809bu, 0u);         /* GRAS_SU_VS_SIV_CNTL */
    STATIC_REG(0x80a0u, 0x2u);       /* GRAS_SC_CNTL, CCU cache line 2 */
    /* These two controls are the render-pass-level sysmem selection. They
     * are distinct from RB_BUFFER_CNTL's per-target RT0 bit below; omitting
     * them leaves A7xx configured for GMEM even with a sysmem MRT address. */
    STATIC_REG(A7XX_GRAS_SC_BIN_CNTL, A7XX_RENDER_BUFFERS_SYSMEM);
    STATIC_REG(A7XX_RB_CNTL, A7XX_RENDER_BUFFERS_SYSMEM);
    STATIC_REG(0x8007u, 0u);         /* GRAS_LRZ_CB_CNTL (no CB) */
    STATIC_REG(0xb986u, 0xfcfcu);    /* SP_REG_PROG_ID_3 invalid regids */
    STATIC_REG(A7XX_VFD_RENDER_MODE, A7XX_RENDERING_PASS);
    STATIC_REG(0xa008u, 0u);         /* VFD_STEREO_RENDERING_CNTL */
    STATIC_REG(0x9305u, 0u);         /* VPC_SO_CNTL */
    STATIC_REG(0x8100u, 0u);         /* GRAS_LRZ_CNTL */
    STATIC_REG(0x810bu, 0u);         /* GRAS_LRZ_CNTL2 */
    STATIC_REG(0x8898u, 0u);         /* RB_LRZ_CNTL */
    STATIC_REG(0x8870u, 0u);         /* RB_DEPTH_PLANE_CNTL */
    STATIC_REG(0x8094u, 0u);         /* GRAS_SU_DEPTH_PLANE_CNTL */
    STATIC_REG(0x930au, 0u);         /* VPC_UNKNOWN_CNTL */
    for (uint32_t i = 0u; i < 32u; ++i)
    {
        const uint32_t base = 0xa010u + i * 4u;

        STATIC_REG(base, 0u);        /* VFD_VERTEX_BUFFER[i].BASE lo */
        STATIC_REG(base + 1u, 0u);   /* VFD_VERTEX_BUFFER[i].BASE hi */
        STATIC_REG(base + 2u, 0u);   /* VFD_VERTEX_BUFFER[i].SIZE */
        STATIC_REG(base + 3u, 0u);   /* VFD_VERTEX_BUFFER[i].STRIDE */
    }
#undef STATIC_REG
    return adreno_x1_85_emit_context_regs(ring, regs, count);
}

static uint32_t a7xx_pack_footprint(const mesart_pipeline_stage_state *stage,
                                    uint8_t fragment)
{
    uint32_t value;

    value = ((uint32_t)stage->half_reg_footprint << 1) |
            ((uint32_t)stage->full_reg_footprint << 7) |
            ((uint32_t)stage->branchstack << 13);
    if (stage->flags & MESART_PIPELINE_STAGE_FLAG_MERGED_REGS)
        value |= fragment ? 0x80000000u : 0x00100000u;
    if (stage->flags & MESART_PIPELINE_STAGE_FLAG_EARLY_PREAMBLE)
        value |= fragment ? 0x10000000u : 0x00200000u;
    if (fragment)
    {
        /* The first profile has no derivatives yet, but overlap is part of
         * Mesa's normal fragment-program state and is safe for this fixed
         * no-kill profile. */
        value |= A7XX_SP_PS_INOUT_REG_OVERLAP;
        if (stage->flags & MESART_PIPELINE_STAGE_FLAG_DOUBLE_THREAD)
            value |= 0x00100000u;
        if (stage->flags & MESART_PIPELINE_STAGE_FLAG_NEEDS_FULL_QUAD)
            value |= 0x00800000u;
        if (stage->flags & MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD)
            value |= 0x04000000u;
    }
    return value;
}

static uint32_t a7xx_pack_output(const mesart_pipeline_varying *varyings,
                                 uint32_t first, uint32_t count)
{
    uint32_t value = 0u;

    for (uint32_t i = 0u; i < count; ++i)
    {
        const mesart_pipeline_varying *varying = &varyings[first + i];
        value |= (uint32_t)varying->regid << (i * 16u);
        value |= (uint32_t)varying->compmask << (i * 16u + 8u);
    }
    return value;
}

static uint32_t a7xx_pack_vpc_dest(const mesart_pipeline_varying *varyings,
                                   uint32_t first, uint32_t count)
{
    uint32_t value = 0u;

    for (uint32_t i = 0u; i < count; ++i)
        value |= (uint32_t)varyings[first + i].location << (i * 8u);
    return value;
}

static uint32_t a7xx_pack_scissor(uint32_t x, uint32_t y)
{
    return (x & 0xffffu) | ((y & 0xffffu) << 16);
}

static uint32_t a7xx_float_bits(float value)
{
    union { float value; uint32_t bits; } convert = {value};
    return convert.bits;
}

static uint32_t a7xx_pack_const_config(uint32_t const_vec4s)
{
    /* a6xx.xml: CONSTLEN lives in bits 2..9 and ENABLED is bit 8.  The
     * compiler's count is in vec4s, while hardware encodes the same count
     * shifted by two. */
    return (const_vec4s << 2) | A7XX_CONST_ENABLED;
}

static int a7xx_validate_static_constant_data(uint64_t gpu_va,
                                              uint32_t bytes,
                                              uint32_t ubo_index)
{
    if (bytes == 0u)
        return (gpu_va == 0u && ubo_index == MESART_IR3_BLOB_NO_UBO) ? 0 : -1;
    /* The authenticated MIR3 parser already establishes range and alignment
     * inside its immutable kernel allocation.  These checks bind that record
     * to the tighter A7xx direct-UBO packet fields. */
    if (!gpu_va || (gpu_va & 15u) || (gpu_va >> 32) != 0u ||
        (bytes & 15u) || ubo_index >= MESART_IR3_BLOB_MAX_UBOS ||
        bytes / 16u > 0xffffu)
        return -1;
    return 0;
}

int adreno_x1_85_3d_validate_draw(const adreno_x1_85_3d_draw *draw)
{
    const mesart_renderer_graphics_pipeline *pipeline;
    const mesart_pipeline_header *reflection;
    uint64_t minimum_bytes;

    if (!draw || !draw->pipeline || !draw->target || !draw->target_gpu_va)
        return -1;
    pipeline = draw->pipeline;
    reflection = &pipeline->reflection;
    if (!pipeline->ready || !pipeline->pipeline_reflection_ready ||
        !pipeline->vertex_code_gpu_va || !pipeline->fragment_code_gpu_va ||
        reflection->magic != MESART_PIPELINE_MAGIC ||
        reflection->version != MESART_PIPELINE_VERSION ||
        !reflection->vertex_code_dwords || !reflection->fragment_code_dwords ||
        reflection->vertex_code_dwords > 0x3fffu ||
        reflection->fragment_code_dwords > 0x3fffu ||
        !reflection->vertex_instruction_groups ||
        !reflection->fragment_instruction_groups ||
        reflection->vertex_instruction_groups >
            reflection->vertex_code_dwords / 2u ||
        reflection->fragment_instruction_groups >
            reflection->fragment_code_dwords / 2u ||
        reflection->vertex_const_vec4s > 63u ||
        reflection->fragment_const_vec4s > 63u ||
        reflection->vertex_sampler_count || reflection->fragment_sampler_count ||
        reflection->vertex_app_ubo_count || reflection->fragment_app_ubo_count ||
        pipeline->vertex_texture_count || pipeline->fragment_texture_count ||
        pipeline->vertex_uniform_vec4s || pipeline->fragment_uniform_vec4s)
        return -2;
    /* VFD consumes literal eight-bit IR3 register encodings.  The pipeline
     * validator has already checked the required vertex-id sentinel case;
     * keep the privileged packet construction explicit about the hardware
     * field width as well. */
    if (reflection->vertex.vertex_id_regid > 0xffu ||
        reflection->vertex.instance_id_regid > 0xffu)
        return -2;
    if (a7xx_validate_static_constant_data(
            pipeline->vertex_constant_data_gpu_va,
            pipeline->vertex_constant_data_bytes,
            pipeline->vertex_constant_data_ubo_index) != 0 ||
        a7xx_validate_static_constant_data(
            pipeline->fragment_constant_data_gpu_va,
            pipeline->fragment_constant_data_bytes,
            pipeline->fragment_constant_data_ubo_index) != 0)
        return -2;
    if (draw->target->format != GPU_RENDER_FORMAT_BGRX8888 ||
        !draw->target->width || !draw->target->height ||
        draw->target->width > 0xffffu || draw->target->height > 0xffffu ||
        draw->target->stride_bytes < draw->target->width * 4u ||
        (draw->target->stride_bytes & 63u))
        return -3;
    minimum_bytes = (uint64_t)draw->target->stride_bytes * draw->target->height;
    if (minimum_bytes < draw->target->stride_bytes ||
        draw->target->cpu_bytes < minimum_bytes ||
        reflection->varying_count < 2u || reflection->varying_count > 32u ||
        reflection->varying_stride_dwords > 127u ||
        reflection->position_location >= reflection->varying_stride_dwords ||
        reflection->point_size_location >= reflection->varying_stride_dwords)
        return -4;
    return 0;
}

int adreno_x1_85_3d_emit_draw_state(gpu_command_ring *ring,
                                    const adreno_x1_85_3d_draw *draw)
{
    const mesart_renderer_graphics_pipeline *pipeline;
    const mesart_pipeline_header *reflection;
    /* The fixed initial profile has fewer than 128 context writes even with
     * all admitted output/varying slots.  Keep this capacity local and
     * bounded rather than silently truncating a signed pipeline. */
    adreno_x1_85_context_reg regs[160];
    uint32_t reg_count = 0u;
    uint32_t output_registers;
    uint32_t destination_registers;
    int validation_rc;

    if (!ring)
        return -1;
    validation_rc = adreno_x1_85_3d_validate_draw(draw);
    if (validation_rc != 0)
        /* Preserve the validator category for the admission preflight:
         * -11 = malformed pipeline, -12 = unsupported first-profile
         * resource, -13 = render-target contract, -14 = stage reflection.
         * This remains distinct from packet-emission failures below. */
        return -10 + validation_rc;
    pipeline = draw->pipeline;
    reflection = &pipeline->reflection;
    output_registers = (reflection->varying_count + 1u) / 2u;
    destination_registers = (reflection->varying_count + 3u) / 4u;
    if (output_registers > 16u || destination_registers > 8u)
        return -2;

#define REG(offset, value) do { regs[reg_count++] = (adreno_x1_85_context_reg){ (offset), (value) }; } while (0)
    /* Source: Mesa fd6_program.cc and a6xx.xml.  The first profile admits no
     * private/shared memory, attributes, application UBOs or kill; Mesa's
     * immutable embedded $consts UBO is bound separately below. */
    /* Mesa's GLSL path selects ISAMMODE_GL (2 << 1) as well as constant
     * demotion.  Leaving ISAMMODE at zero means an undefined instruction
     * fetch mode, which can consume a draw without ever retiring RB_DONE. */
    REG(A7XX_SP_MODE_CNTL, 0x5u);
    /* TPL1 has its own instruction-fetch mode register.  Mesa programs this
     * alongside SP_MODE_CNTL; it is not inherited from SP.  A zero TPL1
     * mode selects neither GL ISAM behavior nor the required data-type
     * override, which is enough for a shader draw to wedge after CP accepts
     * it.  0xa2 is ISAMMODE_GL + Mesa's nearest-mip round mode + destination
     * data-type override.
     */
    REG(A7XX_TPL1_MODE_CNTL, A7XX_TPL1_MODE_GL);
    /* SP_UPDATE_CNTL clears queued CP_LOAD_STATE6 state. Mesa clears all
     * stage/UAV classes before replacing graphics state; clearing only the
     * graphics-stage subset leaks firmware or prior-context CS/UAV state. */
    REG(A7XX_SP_UPDATE_CNTL, A7XX_SP_UPDATE_ALL_STATE);
    REG(A7XX_SP_VS_CONST_CONFIG,
        a7xx_pack_const_config(reflection->vertex_const_vec4s));
    REG(A7XX_SP_HS_CONST_CONFIG, 0u);
    REG(A7XX_SP_DS_CONST_CONFIG, 0u);
    REG(A7XX_SP_GS_CONST_CONFIG, 0u);
    REG(A7XX_SP_PS_CONST_CONFIG,
        a7xx_pack_const_config(reflection->fragment_const_vec4s));
    REG(A7XX_SP_VS_CONFIG, A7XX_SP_XS_ENABLED);
    REG(A7XX_SP_HS_CONFIG, 0u);
    REG(A7XX_SP_DS_CONFIG, 0u);
    REG(A7XX_SP_GS_CONFIG, 0u);
    REG(A7XX_SP_PS_CONFIG, A7XX_SP_XS_ENABLED);
    REG(A7XX_SP_GFX_UAV_BASE, 0u);
    REG(A7XX_SP_GFX_UAV_BASE + 1u, 0u);
    REG(A7XX_SP_GFX_USIZE, 0u);
    REG(A7XX_SP_VS_CNTL_0, a7xx_pack_footprint(&reflection->vertex, 0u));
    REG(A7XX_SP_VS_INSTR_SIZE, reflection->vertex_instruction_groups);
    REG(A7XX_SP_VS_BOOLEAN_CF_MASK, 0u);
    REG(A7XX_SP_VS_PROGRAM_COUNTER, 0u);
    REG(A7XX_SP_VS_BASE, (uint32_t)pipeline->vertex_code_gpu_va);
    REG(A7XX_SP_VS_BASE + 1u, (uint32_t)(pipeline->vertex_code_gpu_va >> 32));
    REG(A7XX_SP_VS_PVT_MEM_PARAM, 0u);
    REG(A7XX_SP_VS_PVT_MEM_BASE, 0u);
    REG(A7XX_SP_VS_PVT_MEM_BASE + 1u, 0u);
    REG(A7XX_SP_VS_PVT_MEM_SIZE, 0u);
    REG(A7XX_SP_VS_TSIZE, 0u);
    REG(A7XX_SP_VS_PVT_MEM_STACK_OFFSET, 0u);
    REG(A7XX_SP_VS_VGS_CNTL, 0u);
    REG(A7XX_SP_PS_CNTL_0, a7xx_pack_footprint(&reflection->fragment, 1u));
    REG(A7XX_SP_PS_INSTR_SIZE, reflection->fragment_instruction_groups);
    REG(A7XX_SP_PS_BOOLEAN_CF_MASK, 0u);
    REG(A7XX_SP_PS_PROGRAM_COUNTER, 0u);
    REG(A7XX_SP_PS_BASE, (uint32_t)pipeline->fragment_code_gpu_va);
    REG(A7XX_SP_PS_BASE + 1u, (uint32_t)(pipeline->fragment_code_gpu_va >> 32));
    REG(A7XX_SP_PS_PVT_MEM_PARAM, 0u);
    REG(A7XX_SP_PS_PVT_MEM_BASE, 0u);
    REG(A7XX_SP_PS_PVT_MEM_BASE + 1u, 0u);
    REG(A7XX_SP_PS_PVT_MEM_SIZE, 0u);
    REG(A7XX_SP_PS_TSIZE, 0u);
    REG(A7XX_SP_PS_PVT_MEM_STACK_OFFSET, 0u);
    REG(A7XX_SP_PS_VGS_CNTL, 0u);
    /* This profile's FS has no system values, varyings, or texture
     * prefetches.  These fields are still stateful on A7xx, so establish
     * Mesa's no-input values instead of inheriting display-firmware state. */
    REG(A7XX_SP_PS_INITIAL_TEX_LOAD_CNTL, 0x01ff7fc8u);
    REG(A7XX_SP_PS_CNTL_1, 0x00000300u);
    REG(A7XX_SP_PS_WAVE_CNTL, 0u);
    REG(A7XX_SP_LB_PARAM_LIMIT, 0x7u);
    REG(A7XX_SP_REG_PROG_ID_0, 0xfcfcfcfcu);
    REG(A7XX_SP_REG_PROG_ID_1, 0xfcfcfcfcu);
    REG(A7XX_SP_REG_PROG_ID_2, 0xfcfcfcfcu);
    REG(A7XX_SP_REG_PROG_ID_3, 0x0000fcfcu);
    REG(A7XX_SP_VS_OUTPUT_CNTL, reflection->varying_count);
    for (uint32_t i = 0u; i < output_registers; ++i)
    {
        uint32_t remaining = reflection->varying_count - i * 2u;
        REG(A7XX_SP_VS_OUTPUT_REG + i,
            a7xx_pack_output(reflection->varyings, i * 2u,
                             remaining > 2u ? 2u : remaining));
    }
    for (uint32_t i = 0u; i < destination_registers; ++i)
    {
        uint32_t remaining = reflection->varying_count - i * 4u;
        REG(A7XX_SP_VS_VPC_DEST_REG + i,
            a7xx_pack_vpc_dest(reflection->varyings, i * 4u,
                               remaining > 4u ? 4u : remaining));
    }
    /* The linker locations are fields where zero is a valid location, so
     * absent values must be written as A7xx's explicit 0xff sentinel rather
     * than left at zero.  This matches fd6_program's no-layer/no-view path. */
    REG(A7XX_VPC_VS_CNTL, reflection->varying_stride_dwords |
        ((uint32_t)reflection->position_location << 8) |
        ((uint32_t)reflection->point_size_location << 16));
    /* These are Mesa's ordinary triangle-raster defaults.  They must be
     * written rather than inherited: the system firmware legitimately uses
     * the same state for non-triangle paths before the kernel takes over. */
    REG(A7XX_PC_CNTL, 0u);             /* no restart, GL vertex ordering */
    REG(A7XX_VPC_PC_CNTL, 0u);
    REG(A7XX_PC_RESTART_INDEX, 0u);
    REG(A7XX_GRAS_SU_CNTL, 0u);
    REG(A7XX_VPC_RAST_CNTL, 0u);       /* POLYMODE6_TRIANGLES */
    REG(A7XX_VPC_PS_RAST_CNTL, 0u);
    REG(A7XX_PC_DGEN_RAST_CNTL, 0u);   /* POLYMODE6_TRIANGLES */
    REG(A7XX_PC_DGEN_SU_CONSERVATIVE_RAS_CNTL, 0u);
    REG(A7XX_PC_VS_CNTL, reflection->varying_stride_dwords | 0x100u);
    REG(A7XX_PC_PS_CNTL, 0u);
    /* VPC_PS_CNTL's low byte is NUMNONPOSVAR, not an invalid-location
     * field.  The previous value accidentally set it to 255 for a fragment
     * shader with zero inputs, making the VPC wait for nonexistent data.
     * PRIMIDLOC and VIEWIDLOC are separately invalid (0xff). */
    REG(A7XX_VPC_PS_CNTL,
        (uint32_t)reflection->fragment.total_varying_components |
        ((uint32_t)A7XX_INVALID_REGID << 8) |
        (reflection->fragment.total_varying_components ? 0x00010000u : 0u) |
        ((uint32_t)A7XX_INVALID_REGID << 24));
    REG(A7XX_VPC_VS_CLIP_CULL_CNTL, 0x00ffff00u);
    REG(A7XX_VPC_VS_CLIP_CULL_CNTL_V2, 0x00ffff00u);
    REG(A7XX_VPC_VS_SIV_CNTL, A7XX_VPC_INVALID_SIV_LOCATION);
    REG(A7XX_VPC_VS_SIV_CNTL_V2, A7XX_VPC_INVALID_SIV_LOCATION);
    REG(A7XX_GRAS_SU_VS_SIV_CNTL, 0u);
    for (uint32_t word = 0u; word < 4u; ++word)
        REG(A7XX_VPC_VARYING_LM_TRANSFER + word,
            ~reflection->varying_mask[word]);
    REG(A7XX_VPC_SO_OVERRIDE, A7XX_VPC_SO_ENABLE);
    REG(A7XX_GRAS_CL_CNTL, A7XX_GRAS_CL_Z_CLAMP |
        A7XX_GRAS_CL_VP_CLIP_IGNORE);
    REG(A7XX_GRAS_MODE_CNTL, 0x2u);
    /* First profile: one GL-style full-target viewport, no depth buffer. */
    REG(0x8010u, a7xx_float_bits((float)draw->target->width * 0.5f));
    REG(0x8011u, a7xx_float_bits((float)draw->target->width * 0.5f));
    REG(0x8012u, a7xx_float_bits((float)draw->target->height * 0.5f));
    REG(0x8013u, a7xx_float_bits((float)draw->target->height * 0.5f));
    REG(0x8014u, a7xx_float_bits(0.5f));
    REG(0x8015u, a7xx_float_bits(0.5f));
    REG(A7XX_GRAS_SC_SCREEN_SCISSOR, a7xx_pack_scissor(0u, 0u));
    REG(A7XX_GRAS_SC_SCREEN_SCISSOR + 1u,
        a7xx_pack_scissor(draw->target->width - 1u, draw->target->height - 1u));
    REG(A7XX_GRAS_SC_VIEWPORT_SCISSOR, a7xx_pack_scissor(0u, 0u));
    REG(A7XX_GRAS_SC_VIEWPORT_SCISSOR + 1u,
        a7xx_pack_scissor(draw->target->width - 1u, draw->target->height - 1u));
    /* The compiler assigns gl_VertexID to a real IR3 register, but only VFD
     * can populate it for an auto-indexed draw.  Until this is programmed
     * the shader executes with an undefined index and A7xx can consume the
     * packet without ever reaching the RB completion event. */
    REG(A7XX_VFD_CNTL_0, 0u); /* no memory vertex fetches or decodes */
    REG(A7XX_VFD_CNTL_1,
        (uint32_t)reflection->vertex.vertex_id_regid |
        ((uint32_t)reflection->vertex.instance_id_regid << 8) |
        ((uint32_t)MESART_PIPELINE_INVALID_REGID << 16) |
        ((uint32_t)MESART_PIPELINE_INVALID_REGID << 24));
    REG(A7XX_VFD_CNTL_2, 0x0000fcfcu);
    REG(A7XX_VFD_CNTL_3, 0xfcfcfcfcu);
    REG(A7XX_VFD_CNTL_4, 0x000000fcu);
    REG(A7XX_VFD_CNTL_5, 0x0000fcfcu);
    REG(A7XX_VFD_CNTL_6, 0u);
    REG(A7XX_VFD_MODE_CNTL, 0x3u);
    REG(A7XX_VFD_RENDER_MODE, A7XX_RENDERING_PASS);
    REG(0xa00eu, 0u);                /* VFD_INDEX_OFFSET */
    REG(0xa00fu, 0u);                /* VFD_INSTANCE_START_OFFSET */
    REG(A7XX_RB_RENDER_CNTL, 0u);
    REG(A7XX_GRAS_SU_RENDER_CNTL, 0u);
    REG(A7XX_SP_RENDER_CNTL, 0u);
    /* The target is one-sample. Mesa programs all seven MSAA controls even
     * for that case, rather than trusting a previous GPU context. */
    REG(0xb300u, 0u);
    REG(0xb301u, 0x4u);
    REG(0x80a2u, 0u);
    REG(0x80a3u, 0x4u);
    REG(0x8802u, 0u);
    REG(0x8803u, 0x4u);
    REG(0x88d5u, 0u);
    /* The Mesa direct-sysmem begin sequence deliberately marks every
     * attachment class as sysmem, even when this profile binds only RT0.
     * That prevents stale GMEM/depth routing from a previous context. */
    REG(A7XX_RB_BUFFER_CNTL, A7XX_RB_ALL_BUFFERS_SYSMEM);
    /* A7xx's sysmem render-begin sequence explicitly selects the sysmem
     * clear-target mode, even though this first pipeline issues no clear. */
    REG(A7XX_RB_CLEAR_TARGET, 0u);
    REG(A7XX_RB_MRT_CONTROL, A7XX_RB_MRT_COMPONENT_RGBA);
    REG(A7XX_RB_MRT_BUF_INFO, A7XX_FMT_8_8_8_8_UNORM |
        (A7XX_TILE_LINEAR << 8) | (A7XX_SWAP_WXYZ << 13));
    /* RB_MRT_PITCH and RB_MRT_ARRAY_PITCH are byte values.  Unlike the
     * LRZ/flag-buffer pitch fields they do not carry an implicit shift. */
    REG(A7XX_RB_MRT_PITCH, draw->target->stride_bytes);
    REG(A7XX_RB_MRT_ARRAY_PITCH,
        (uint32_t)((uint64_t)draw->target->stride_bytes * draw->target->height));
    REG(A7XX_RB_MRT_BASE, (uint32_t)draw->target_gpu_va);
    REG(A7XX_RB_MRT_BASE + 1u, (uint32_t)(draw->target_gpu_va >> 32));
    REG(A7XX_RB_MRT_BASE_GMEM, 0u);
    /* Disable stale UBWC flag-buffer state and state the one linear MRT's
     * exact output contract, as fd6_emit_mrt/build_prog_fb_rast do. */
    REG(0x8903u, 0u);                /* RB_COLOR_FLAG_BUFFER_ADDR(0) lo */
    REG(0x8904u, 0u);                /* RB_COLOR_FLAG_BUFFER_ADDR(0) hi */
    REG(0x8905u, 0u);                /* RB_COLOR_FLAG_BUFFER_PITCH(0) */
    REG(0x8102u, A7XX_FMT_8_8_8_8_UNORM); /* GRAS_LRZ_MRT_BUFFER_INFO_0 */
    REG(0x8004u, 0u);                /* GRAS_CL_ARRAY_SIZE */
    REG(A7XX_RB_SRGB_CNTL, 0u);
    REG(A7XX_SP_SRGB_CNTL, 0u);
    REG(A7XX_RB_PS_OUTPUT_CNTL, 0u);
    REG(A7XX_RB_PS_MRT_CNTL, 1u);
    REG(A7XX_RB_PS_OUTPUT_MASK, 0xfu);
    REG(A7XX_SP_BLEND_CNTL, 0u);
    REG(A7XX_SP_PS_OUTPUT_MASK, 0xfu);
    /* Depth/sample-mask/stencil outputs are absent.  A7xx uses r63.x
     * (0xfc), not zero, as the disabled register sentinel. */
    REG(A7XX_SP_PS_OUTPUT_CNTL, 0xfcfc00fcu);
    REG(A7XX_SP_PS_MRT_CNTL, 1u);
    REG(A7XX_SP_PS_OUTPUT_REG, reflection->fragment.primary_output_regid);
    REG(A7XX_SP_PS_MRT_REG, A7XX_FMT_8_8_8_8_UNORM);
    REG(A7XX_SP_PS_OUTPUT_CONST_CNTL, 0u);
    REG(A7XX_SP_PS_OUTPUT_CONST_MASK, 0u);
#undef REG
    if (adreno_x1_85_emit_context_regs(ring, regs, reg_count) != 0)
        return -3;
    /* Mesa's NIR constant pool is appended to each MIR3 binary.  Match
     * Turnip's tu6_emit_xs path: bind it by compiler-selected UBO index
     * before the draw, while addresses and packet contents remain wholly
     * kernel-generated. */
    if (pipeline->vertex_constant_data_bytes &&
        adreno_x1_85_emit_cp_constant_ubo(
            ring, ADRENO_X1_85_CONSTANT_VERTEX,
            pipeline->vertex_constant_data_ubo_index,
            pipeline->vertex_constant_data_gpu_va,
            pipeline->vertex_constant_data_bytes / 16u) != 0)
        return -4;
    if (pipeline->fragment_constant_data_bytes &&
        adreno_x1_85_emit_cp_constant_ubo(
            ring, ADRENO_X1_85_CONSTANT_FRAGMENT,
            pipeline->fragment_constant_data_ubo_index,
            pipeline->fragment_constant_data_gpu_va,
            pipeline->fragment_constant_data_bytes / 16u) != 0)
        return -5;
    return 0;
}
