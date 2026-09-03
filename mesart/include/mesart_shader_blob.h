#pragma once

#include <stdint.h>

/*
 * Signed Mesart shader artifact.
 *
 * The payload is compiler-produced A7xx IR3 instruction data.  It is not an
 * ELF and is never executable by the CPU.  The enclosing Mesart manifest
 * authenticates this whole file; this header lets the kernel additionally
 * reject an artifact for the wrong GPU or pipeline stage before it is copied
 * to any GPU-visible allocation.
 */
#define MESART_IR3_BLOB_MAGIC   0x3352494du /* "MIR3" little-endian */
#define MESART_IR3_BLOB_VERSION 3u
#define MESART_IR3_BLOB_HEADER_BYTES 64u
#define MESART_IR3_BLOB_MAX_CODE_BYTES (4u * 1024u * 1024u)
#define MESART_IR3_BLOB_MAX_CONST_VECS 4096u
#define MESART_IR3_BLOB_MAX_SAMPLERS   16u
#define MESART_IR3_BLOB_MAX_UBOS       32u
#define MESART_IR3_BLOB_MAX_STAGE_IO   34u
#define MESART_IR3_BLOB_MAX_OUTPUT_DWORDS 4096u
#define MESART_IR3_BLOB_NO_UBO          0xffffffffu

#define MESART_IR3_FLAG_WRITES_POSITION (1u << 0)
#define MESART_IR3_FLAG_WRITES_COLOR0    (1u << 1)
#define MESART_IR3_FLAG_NEEDS_PIXLOD     (1u << 2)
#define MESART_IR3_FLAG_HAS_KILL         (1u << 3)
#define MESART_IR3_FLAG_KNOWN            \
    (MESART_IR3_FLAG_WRITES_POSITION | MESART_IR3_FLAG_WRITES_COLOR0 | \
     MESART_IR3_FLAG_NEEDS_PIXLOD | MESART_IR3_FLAG_HAS_KILL)

typedef enum mesart_ir3_shader_stage
{
    MESART_IR3_SHADER_VERTEX = 1u,
    MESART_IR3_SHADER_FRAGMENT = 2u,
} mesart_ir3_shader_stage;

typedef struct __attribute__((packed)) mesart_ir3_blob_header
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint64_t chip_id;
    uint32_t stage;
    uint32_t code_bytes;
    uint32_t flags;
    /* Compiler-authenticated interface summary.  These are not driver
     * commands: the kernel uses them to bound future uniform, texture and
     * varying allocations before it emits any A7xx state. */
    uint32_t instruction_groups;
    uint32_t const_vec4s;
    uint16_t sampler_count;
    uint16_t app_ubo_count;
    uint16_t input_count;
    uint16_t output_count;
    uint32_t output_dwords;
    /* `code_bytes` describes executable IR3 instructions at the start of
     * the following binary.  Mesa may append an authenticated $consts UBO
     * after those instructions.  A7xx reads that data through a kernel-made
     * UBO descriptor; it is never interpreted as CPU code or an EL0 packet. */
    uint32_t binary_bytes;
    uint32_t constant_data_offset;
    uint32_t constant_data_bytes;
    uint32_t constant_data_ubo_index;
} mesart_ir3_blob_header;

typedef struct mesart_ir3_blob_view
{
    const mesart_ir3_blob_header *header;
    const uint32_t *code;
    uint32_t code_dwords;
    const uint8_t *binary;
    uint32_t binary_bytes;
} mesart_ir3_blob_view;
