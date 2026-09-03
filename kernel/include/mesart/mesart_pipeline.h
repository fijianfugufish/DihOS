#pragma once

#include <stdint.h>

#include "mesart_pipeline_blob.h"
#include "mesart_shader_blob.h"

/* A minimal shader summary copied from the two already validated, signed MIR3
 * assets.  Keeping it value-only makes MPIP validation independent of GPU
 * addresses and renderer-service internals. */
typedef struct mesart_pipeline_shader_info
{
    uint32_t code_dwords;
    uint32_t instruction_groups;
    uint32_t const_vec4s;
    uint32_t flags;
    uint32_t output_dwords;
    uint16_t sampler_count;
    uint16_t app_ubo_count;
} mesart_pipeline_shader_info;

/* Validates compiler reflection against the exact two admitted MIR3 assets.
 * It accepts no packet stream, register address, memory address, or hardware
 * resource; those remain the responsibility of the kernel A7xx broker. */
int mesart_pipeline_validate(const void *bytes, uint64_t byte_count,
                             uint64_t expected_chip_id,
                             const mesart_pipeline_shader_info *vertex,
                             const mesart_pipeline_shader_info *fragment,
                             mesart_pipeline_header *out_header);
