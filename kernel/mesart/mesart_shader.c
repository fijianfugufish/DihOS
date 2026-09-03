#include "mesart/mesart_shader.h"

_Static_assert(sizeof(mesart_ir3_blob_header) == MESART_IR3_BLOB_HEADER_BYTES,
               "MIR3 header layout must stay fixed across the host and kernel.");

int mesart_ir3_blob_validate(const void *blob, uint64_t blob_bytes,
                             uint64_t expected_chip_id,
                             mesart_ir3_blob_view *out_view)
{
    const mesart_ir3_blob_header *header =
        (const mesart_ir3_blob_header *)blob;
    uint64_t expected_bytes;

    if (out_view)
        *out_view = (mesart_ir3_blob_view){0};
    if (!header || !out_view ||
        blob_bytes < sizeof(mesart_ir3_blob_header) ||
        header->magic != MESART_IR3_BLOB_MAGIC ||
        header->version != MESART_IR3_BLOB_VERSION ||
        header->header_bytes != MESART_IR3_BLOB_HEADER_BYTES ||
        header->chip_id != expected_chip_id ||
        (header->stage != MESART_IR3_SHADER_VERTEX &&
         header->stage != MESART_IR3_SHADER_FRAGMENT) ||
        !header->code_bytes || !header->binary_bytes ||
        header->code_bytes > MESART_IR3_BLOB_MAX_CODE_BYTES ||
        header->binary_bytes > MESART_IR3_BLOB_MAX_CODE_BYTES ||
        header->binary_bytes < header->code_bytes ||
        (header->code_bytes & 7u) ||
        header->instruction_groups > header->code_bytes / 8u ||
        header->const_vec4s > MESART_IR3_BLOB_MAX_CONST_VECS ||
        header->sampler_count > MESART_IR3_BLOB_MAX_SAMPLERS ||
        header->app_ubo_count > MESART_IR3_BLOB_MAX_UBOS ||
        header->input_count > MESART_IR3_BLOB_MAX_STAGE_IO ||
        header->output_count > MESART_IR3_BLOB_MAX_STAGE_IO ||
        header->output_dwords > MESART_IR3_BLOB_MAX_OUTPUT_DWORDS ||
        (header->flags & ~MESART_IR3_FLAG_KNOWN) ||
        ((header->constant_data_bytes == 0u) &&
         (header->constant_data_offset != 0u ||
          header->constant_data_ubo_index != MESART_IR3_BLOB_NO_UBO)) ||
        ((header->constant_data_bytes != 0u) &&
         ((header->constant_data_offset & 15u) ||
          (header->constant_data_bytes & 15u) ||
          header->constant_data_offset < header->code_bytes ||
          header->constant_data_offset > header->binary_bytes ||
          header->constant_data_bytes > header->binary_bytes -
                                        header->constant_data_offset ||
          header->constant_data_ubo_index >= MESART_IR3_BLOB_MAX_UBOS)) ||
        (header->stage == MESART_IR3_SHADER_VERTEX &&
         !(header->flags & MESART_IR3_FLAG_WRITES_POSITION)) ||
        (header->stage == MESART_IR3_SHADER_FRAGMENT &&
         !(header->flags & MESART_IR3_FLAG_WRITES_COLOR0)))
        return -1;
    expected_bytes = (uint64_t)header->header_bytes + header->binary_bytes;
    if (expected_bytes != blob_bytes)
        return -2;
    *out_view = (mesart_ir3_blob_view){
        header,
        (const uint32_t *)((const uint8_t *)blob + header->header_bytes),
        header->code_bytes / sizeof(uint32_t),
        (const uint8_t *)blob + header->header_bytes,
        header->binary_bytes,
    };
    return 0;
}
