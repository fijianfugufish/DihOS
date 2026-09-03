#include "mesart/mesart_pipeline.h"

_Static_assert(sizeof(mesart_pipeline_header) == MESART_PIPELINE_HEADER_BYTES,
               "MPIP header layout must stay fixed across host and kernel.");

static int pipeline_stage_valid(const mesart_pipeline_stage_state *stage)
{
    if (!stage || stage->full_reg_footprint > 64u ||
        stage->half_reg_footprint > 64u || stage->branchstack > 64u ||
        (stage->flags & ~MESART_PIPELINE_STAGE_FLAG_KNOWN) ||
        stage->total_varying_components > 128u || stage->reserved)
        return 0;
    return 1;
}

static uint8_t last_component(uint8_t compmask)
{
    uint8_t component = 0u;
    while (compmask >>= 1u)
        ++component;
    return component;
}

int mesart_pipeline_validate(const void *bytes, uint64_t byte_count,
                             uint64_t expected_chip_id,
                             const mesart_pipeline_shader_info *vertex,
                             const mesart_pipeline_shader_info *fragment,
                             mesart_pipeline_header *out_header)
{
    const mesart_pipeline_header *header = (const mesart_pipeline_header *)bytes;

    if (out_header)
        *out_header = (mesart_pipeline_header){0};
    if (!header || !vertex || !fragment || !out_header ||
        byte_count != MESART_PIPELINE_HEADER_BYTES ||
        header->magic != MESART_PIPELINE_MAGIC ||
        header->version != MESART_PIPELINE_VERSION ||
        header->header_bytes != MESART_PIPELINE_HEADER_BYTES ||
        header->chip_id != expected_chip_id || header->flags ||
        header->vertex_code_dwords != vertex->code_dwords ||
        header->fragment_code_dwords != fragment->code_dwords ||
        !header->vertex_instruction_groups ||
        !header->fragment_instruction_groups ||
        header->vertex_instruction_groups != vertex->instruction_groups ||
        header->fragment_instruction_groups != fragment->instruction_groups ||
        header->vertex_instruction_groups > header->vertex_code_dwords / 2u ||
        header->fragment_instruction_groups > header->fragment_code_dwords / 2u ||
        header->vertex_const_vec4s != vertex->const_vec4s ||
        header->fragment_const_vec4s != fragment->const_vec4s ||
        header->vertex_sampler_count != vertex->sampler_count ||
        header->fragment_sampler_count != fragment->sampler_count ||
        header->vertex_app_ubo_count != vertex->app_ubo_count ||
        header->fragment_app_ubo_count != fragment->app_ubo_count ||
        header->vertex.output_dwords != vertex->output_dwords ||
        header->fragment.output_dwords != fragment->output_dwords ||
        !pipeline_stage_valid(&header->vertex) ||
        !pipeline_stage_valid(&header->fragment) ||
        header->vertex.vertex_id_regid == MESART_PIPELINE_INVALID_REGID ||
        header->vertex.primary_output_regid == MESART_PIPELINE_INVALID_REGID ||
        header->vertex.secondary_output_regid == MESART_PIPELINE_INVALID_REGID ||
        header->fragment.vertex_id_regid != MESART_PIPELINE_INVALID_REGID ||
        header->fragment.instance_id_regid != MESART_PIPELINE_INVALID_REGID ||
        header->fragment.primary_output_regid == MESART_PIPELINE_INVALID_REGID ||
        header->fragment.secondary_output_regid != MESART_PIPELINE_INVALID_REGID ||
        header->varying_count < 2u ||
        header->varying_count > MESART_PIPELINE_MAX_VARYINGS ||
        !header->varying_stride_dwords || header->varying_stride_dwords >= 128u ||
        header->position_location >= header->varying_stride_dwords ||
        header->point_size_location >= header->varying_stride_dwords ||
        (vertex->flags & MESART_IR3_FLAG_NEEDS_PIXLOD) !=
            ((header->vertex.flags & MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD) ?
                 MESART_IR3_FLAG_NEEDS_PIXLOD : 0u) ||
        (fragment->flags & MESART_IR3_FLAG_NEEDS_PIXLOD) !=
            ((header->fragment.flags & MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD) ?
                 MESART_IR3_FLAG_NEEDS_PIXLOD : 0u))
        return -1;
    for (uint32_t i = 0u; i < sizeof(header->reserved); ++i)
        if (header->reserved[i])
            return -2;
    for (uint32_t i = 0u; i < header->varying_count; ++i)
    {
        const mesart_pipeline_varying *varying = &header->varyings[i];
        if (!varying->compmask || varying->location >= header->varying_stride_dwords ||
            (uint32_t)varying->location + last_component(varying->compmask) >=
                header->varying_stride_dwords)
            return -3;
    }
    for (uint32_t word = 0u; word < 4u; ++word)
    {
        uint32_t expected_mask = 0u;
        for (uint32_t i = 0u; i < header->varying_count; ++i)
        {
            const mesart_pipeline_varying *varying = &header->varyings[i];
            for (uint32_t component = 0u; component < 4u; ++component)
                if (varying->compmask & (1u << component))
                {
                    uint32_t location = varying->location + component;
                    if (location / 32u == word)
                        expected_mask |= 1u << (location % 32u);
                }
        }
        if (header->varying_mask[word] != expected_mask)
            return -4;
    }
    for (uint32_t i = header->varying_count; i < MESART_PIPELINE_MAX_VARYINGS;
         ++i)
        if (header->varyings[i].slot || header->varyings[i].regid ||
            header->varyings[i].compmask || header->varyings[i].location)
            return -5;
    *out_header = *header;
    return 0;
}
