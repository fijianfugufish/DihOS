#pragma once

#include <stdint.h>

/*
 * Signed host-compiled pipeline description.  MPIP deliberately contains
 * compiler reflection, not PM4 packets or register/value pairs.  DihOS uses
 * the information to construct the small A7xx state sequence itself and can
 * therefore keep resource addresses, render targets and synchronization
 * kernel-owned.
 */
#define MESART_PIPELINE_MAGIC          0x5049504du /* "MPIP" little-endian */
#define MESART_PIPELINE_VERSION        2u
#define MESART_PIPELINE_HEADER_BYTES   256u
#define MESART_PIPELINE_MAX_VARYINGS   32u
/* Mesa/Freedreno encodes r63.x as (63 << 2), its INVALID_REG sentinel.  MPIP
 * stores the actual IR3 register encoding so this must not be a made-up
 * all-ones value. */
#define MESART_PIPELINE_INVALID_REGID  252u

#define MESART_PIPELINE_STAGE_FLAG_MERGED_REGS   (1u << 0)
#define MESART_PIPELINE_STAGE_FLAG_EARLY_PREAMBLE (1u << 1)
#define MESART_PIPELINE_STAGE_FLAG_DOUBLE_THREAD  (1u << 2)
#define MESART_PIPELINE_STAGE_FLAG_NEEDS_FULL_QUAD (1u << 3)
#define MESART_PIPELINE_STAGE_FLAG_READS_PRIMID   (1u << 4)
#define MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD   (1u << 5)
#define MESART_PIPELINE_STAGE_FLAG_KNOWN \
    (MESART_PIPELINE_STAGE_FLAG_MERGED_REGS | \
     MESART_PIPELINE_STAGE_FLAG_EARLY_PREAMBLE | \
     MESART_PIPELINE_STAGE_FLAG_DOUBLE_THREAD | \
     MESART_PIPELINE_STAGE_FLAG_NEEDS_FULL_QUAD | \
     MESART_PIPELINE_STAGE_FLAG_READS_PRIMID | \
     MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD)

typedef struct __attribute__((packed)) mesart_pipeline_stage_state
{
    /* Hardware footprints, already converted from Mesa's highest-register
     * representation to the register counts consumed by SP_xS_CNTL_0. */
    uint8_t full_reg_footprint;
    uint8_t half_reg_footprint;
    uint8_t branchstack;
    uint8_t flags;
    /* Only the input/system and output registers needed by DihOS' initial
     * auto-indexed triangle profile are admitted. */
    uint16_t vertex_id_regid;
    uint16_t instance_id_regid;
    /* VS: position then point-size. FS: primary colour output then invalid. */
    uint16_t primary_output_regid;
    uint16_t secondary_output_regid;
    uint16_t total_varying_components;
    uint16_t output_dwords;
    uint32_t reserved;
} mesart_pipeline_stage_state;

typedef struct __attribute__((packed)) mesart_pipeline_varying
{
    uint8_t slot;
    uint8_t regid;
    uint8_t compmask;
    uint8_t location;
} mesart_pipeline_varying;

typedef struct __attribute__((packed)) mesart_pipeline_header
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint64_t chip_id;
    uint32_t flags;
    /* These must match the two independently admitted MIR3 blobs exactly. */
    uint32_t vertex_code_dwords;
    uint32_t fragment_code_dwords;
    /* A7xx SP_xS_INSTR_SIZE consumes IR3 instruction groups (eight-byte
     * units), not the byte/dword size of the complete uploaded code image. */
    uint32_t vertex_instruction_groups;
    uint32_t fragment_instruction_groups;
    uint32_t vertex_const_vec4s;
    uint32_t fragment_const_vec4s;
    uint16_t vertex_sampler_count;
    uint16_t fragment_sampler_count;
    uint16_t vertex_app_ubo_count;
    uint16_t fragment_app_ubo_count;
    mesart_pipeline_stage_state vertex;
    mesart_pipeline_stage_state fragment;
    uint8_t varying_count;
    uint8_t varying_stride_dwords;
    uint8_t position_location;
    uint8_t point_size_location;
    uint32_t varying_mask[4];
    mesart_pipeline_varying varyings[MESART_PIPELINE_MAX_VARYINGS];
    uint8_t reserved[16];
} mesart_pipeline_header;
