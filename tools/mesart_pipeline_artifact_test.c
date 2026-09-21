#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mesart/mesart_pipeline.h"
#include "mesart/mesart_shader.h"
#include "mesart_protocol.h"

static void *read_file(const char *path, uint64_t *bytes)
{
    FILE *stream = fopen(path, "rb");
    long size;
    void *data;
    if (!stream || fseek(stream, 0, SEEK_END) ||
        (size = ftell(stream)) <= 0 || fseek(stream, 0, SEEK_SET))
        return 0;
    data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, stream) != (size_t)size) {
        free(data);
        fclose(stream);
        return 0;
    }
    fclose(stream);
    *bytes = (uint64_t)size;
    return data;
}

static mesart_pipeline_shader_info info(const mesart_ir3_blob_view *view)
{
    const mesart_ir3_blob_header *h = view->header;
    return (mesart_pipeline_shader_info){h->code_bytes / 4u,
        h->instruction_groups, h->const_vec4s, h->flags, h->output_dwords,
        h->sampler_count, h->app_ubo_count};
}

int main(int argc, char **argv)
{
    uint64_t vb = 0, fb = 0, pb = 0;
    void *v, *f, *p;
    mesart_ir3_blob_view vv, fv;
    mesart_pipeline_header reflection;
    if (argc != 4 || !(v = read_file(argv[1], &vb)) ||
        !(f = read_file(argv[2], &fb)) || !(p = read_file(argv[3], &pb)))
        return 2;
    if (mesart_ir3_blob_validate(v, vb, MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85, &vv) ||
        mesart_ir3_blob_validate(f, fb, MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85, &fv))
        return 3;
    mesart_pipeline_shader_info vi = info(&vv), fi = info(&fv);
    if (mesart_pipeline_validate(p, pb, MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85,
                                 &vi, &fi, &reflection))
        return 4;
    if (reflection.vertex_app_ubo_count != 0 ||
        reflection.fragment_app_ubo_count != 1 ||
        reflection.vertex_attribute_count != 1 ||
        reflection.vertex_attribute0_slot != 15 ||
        reflection.vertex_attribute0_compmask != 15 ||
        reflection.vertex_ubo_push_vec4_count != 0 ||
        reflection.fragment_ubo_push_vec4_count != 4 ||
        reflection.vertex_ubo_push_src_vec4 != 0 ||
        reflection.fragment_ubo_push_src_vec4 != 0 ||
        reflection.vertex_sampler_count || reflection.fragment_sampler_count != 1) {
        fprintf(stderr,
            "profile mismatch: ubos=%u/%u attr=%u slot=%u mask=0x%x "
            "push=%u/%u src=%u/%u samplers=%u/%u\n",
            reflection.vertex_app_ubo_count,
            reflection.fragment_app_ubo_count,
            reflection.vertex_attribute_count,
            reflection.vertex_attribute0_slot,
            reflection.vertex_attribute0_compmask,
            reflection.vertex_ubo_push_vec4_count,
            reflection.fragment_ubo_push_vec4_count,
            reflection.vertex_ubo_push_src_vec4,
            reflection.fragment_ubo_push_src_vec4,
            reflection.vertex_sampler_count,
            reflection.fragment_sampler_count);
        return 5;
    }
    puts("PASS: signed-profile inputs validate as textured VBO+sampler pipeline");
    free(v); free(f); free(p);
    return 0;
}
