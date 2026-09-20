/*
 * Host-only GLSL ES -> NIR -> Freedreno IR3 compiler bridge.
 *
 * This is intentionally a build tool, not a DihOS process.  It uses the
 * pinned Mesa snapshot in WSL to link a vertex/fragment program and emits two
 * small MIR3 envelopes that the DihOS kernel validates before it maps the
 * code into GPU-visible memory.  The kernel remains the sole producer of CP
 * packets and pipeline register state.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "compiler/glsl/standalone.h"
#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "freedreno/common/freedreno_dev_info.h"
#include "freedreno/ir3/ir3_compiler.h"
#include "freedreno/ir3/ir3_nir.h"
#include "freedreno/ir3/ir3_shader.h"
#include "main/mtypes.h"
#include "main/shader_types.h"
#include "mesart_pipeline_blob.h"
#include "mesart_shader_blob.h"
#include "util/ralloc.h"

static constexpr uint64_t kMesartX185ChipId = UINT64_C(0xffff43050c01);

static void usage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s <input.vert> <input.frag> <output.vert.mir3> "
                 "<output.frag.mir3> <output.mpip>\n",
                 program);
}

static bool ends_with(const char *text, const char *suffix)
{
    const size_t text_len = std::strlen(text);
    const size_t suffix_len = std::strlen(suffix);
    return text_len >= suffix_len &&
           std::memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

static uint32_t mesart_stage_for_glsl_path(const char *path)
{
    if (ends_with(path, ".vert"))
        return MESART_IR3_SHADER_VERTEX;
    if (ends_with(path, ".frag"))
        return MESART_IR3_SHADER_FRAGMENT;
    return 0u;
}

static unsigned mesart_uniform_vec4_slots(const glsl_type *type,
                                          bool bindless)
{
    return glsl_count_vec4_slots(type, false, bindless);
}

/* Mesa may represent an API colour-zero result as FRAG_RESULT_COLOR or as
 * DATA0 depending on the linked API path. Both are a valid single render
 * target output for DihOS' initial compositor profile. */
static uint32_t fragment_color0_regid(const ir3_shader_variant *variant)
{
    uint32_t color_register = ir3_find_output_regid(variant, FRAG_RESULT_COLOR);
    if (color_register == INVALID_REG)
        color_register = ir3_find_output_regid(variant, FRAG_RESULT_DATA0);
    return color_register;
}

/* Mesa's IR3 alias pass may fold a constant fragment result directly into
 * the shader's constant-data area.  In that case SP_PS_OUTPUT_CONST_MASK,
 * not merely SP_PS_OUTPUT[0], is required for RB to receive the colour. */
static uint32_t fragment_color0_alias_mask(const ir3_shader_variant *variant)
{
    if (!variant)
        return 0u;
    for (uint32_t i = 0u; i < variant->outputs_count; ++i)
    {
        const ir3_shader_output *output = &variant->outputs[i];

        if (output->slot == FRAG_RESULT_COLOR ||
            output->slot == FRAG_RESULT_DATA0)
            return output->aliased_components;
    }
    return 0u;
}

static bool write_blob(const char *path, uint32_t stage,
                       const ir3_shader_variant *variant,
                       uint32_t application_ubos)
{
    const uint64_t code_bytes = (uint64_t)variant->info.sizedwords * sizeof(uint32_t);
    const uint64_t binary_bytes = variant->info.size;
    const uint32_t constant_data_bytes = variant->constant_data_size;
    const uint32_t constant_data_offset = constant_data_bytes ?
        variant->info.constant_data_offset : 0u;
    const struct ir3_const_state *const_state = ir3_const_state(variant);
    const int32_t constant_data_ubo_index = const_state ?
        const_state->consts_ubo.idx : -1;
    uint32_t flags = 0u;
    uint32_t app_ubos = 0u;

    if (variant->writes_pos)
        flags |= MESART_IR3_FLAG_WRITES_POSITION;
    if (fragment_color0_regid(variant) != INVALID_REG)
        flags |= MESART_IR3_FLAG_WRITES_COLOR0;
    if (variant->need_pixlod)
        flags |= MESART_IR3_FLAG_NEEDS_PIXLOD;
    if (variant->has_kill)
        flags |= MESART_IR3_FLAG_HAS_KILL;
    app_ubos = application_ubos;
    const mesart_ir3_blob_header header = {
        MESART_IR3_BLOB_MAGIC,
        MESART_IR3_BLOB_VERSION,
        MESART_IR3_BLOB_HEADER_BYTES,
        kMesartX185ChipId,
        stage,
        (uint32_t)code_bytes,
        flags,
        variant->instrlen,
        variant->constlen,
        (uint16_t)variant->num_samp,
        (uint16_t)app_ubos,
        (uint16_t)variant->inputs_count,
        (uint16_t)variant->outputs_count,
        variant->output_size,
        (uint32_t)binary_bytes,
        constant_data_offset,
        constant_data_bytes,
        constant_data_ubo_index >= 0 ? (uint32_t)constant_data_ubo_index :
                                       MESART_IR3_BLOB_NO_UBO,
    };

    static_assert(sizeof(mesart_ir3_blob_header) == MESART_IR3_BLOB_HEADER_BYTES,
                  "The shared MIR3 header must remain a fixed 64-byte prefix.");

    if (!variant->bin || variant->info.sizedwords == 0u ||
        (variant->info.sizedwords & 1u) != 0u ||
        binary_bytes > MESART_IR3_BLOB_MAX_CODE_BYTES ||
        binary_bytes < code_bytes ||
        (constant_data_bytes == 0u && constant_data_ubo_index != -1) ||
        (constant_data_bytes != 0u &&
         (constant_data_ubo_index < 0 ||
          (uint32_t)constant_data_ubo_index >= MESART_IR3_BLOB_MAX_UBOS ||
          (constant_data_offset & 15u) || (constant_data_bytes & 15u) ||
          constant_data_offset < code_bytes ||
          constant_data_offset > binary_bytes ||
          constant_data_bytes > binary_bytes - constant_data_offset)) ||
        variant->num_samp < 0 ||
        (uint32_t)variant->num_samp > MESART_IR3_BLOB_MAX_SAMPLERS ||
        app_ubos > MESART_IR3_BLOB_MAX_UBOS ||
        variant->constlen > MESART_IR3_BLOB_MAX_CONST_VECS ||
        variant->inputs_count > MESART_IR3_BLOB_MAX_STAGE_IO ||
        variant->outputs_count > MESART_IR3_BLOB_MAX_STAGE_IO ||
        variant->output_size > MESART_IR3_BLOB_MAX_OUTPUT_DWORDS ||
        variant->instrlen > code_bytes / 8u ||
        (stage == MESART_IR3_SHADER_VERTEX &&
         !(flags & MESART_IR3_FLAG_WRITES_POSITION)) ||
        (stage == MESART_IR3_SHADER_FRAGMENT &&
         !(flags & MESART_IR3_FLAG_WRITES_COLOR0)))
        return false;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    output.write(reinterpret_cast<const char *>(variant->bin),
                 static_cast<std::streamsize>(binary_bytes));
    return output.good();
}

static uint8_t stage_flags(const ir3_shader_variant *variant)
{
    uint8_t flags = 0u;
    if (variant->mergedregs)
        flags |= MESART_PIPELINE_STAGE_FLAG_MERGED_REGS;
    if (variant->early_preamble || variant->info.early_preamble)
        flags |= MESART_PIPELINE_STAGE_FLAG_EARLY_PREAMBLE;
    if (variant->info.double_threadsize)
        flags |= MESART_PIPELINE_STAGE_FLAG_DOUBLE_THREAD;
    if (variant->need_full_quad)
        flags |= MESART_PIPELINE_STAGE_FLAG_NEEDS_FULL_QUAD;
    if (variant->reads_primid)
        flags |= MESART_PIPELINE_STAGE_FLAG_READS_PRIMID;
    if (variant->need_pixlod)
        flags |= MESART_PIPELINE_STAGE_FLAG_NEEDS_PIXLOD;
    return flags;
}

static mesart_pipeline_stage_state pipeline_stage(
    const ir3_shader_variant *variant, bool vertex_stage)
{
    const uint32_t vertex_id = vertex_stage ?
        ir3_find_sysval_regid(variant, SYSTEM_VALUE_VERTEX_ID) : INVALID_REG;
    const uint32_t instance_id = vertex_stage ?
        ir3_find_sysval_regid(variant, SYSTEM_VALUE_INSTANCE_ID) : INVALID_REG;
    const uint32_t primary_output = vertex_stage ?
        ir3_find_output_regid(variant, VARYING_SLOT_POS) : INVALID_REG;
    const uint32_t secondary_output = vertex_stage ?
        ir3_find_output_regid(variant, VARYING_SLOT_PSIZ) : INVALID_REG;
    return {
        (uint8_t)(variant->info.max_reg + 1),
        (uint8_t)(variant->info.max_half_reg + 1),
        (uint8_t)ir3_shader_branchstack_hw(variant),
        stage_flags(variant),
        (uint16_t)vertex_id,
        (uint16_t)instance_id,
        (uint16_t)primary_output,
        (uint16_t)secondary_output,
        (uint16_t)variant->total_in,
        (uint16_t)variant->output_size,
        0u,
    };
}

/* The first hardware profile is intentionally small: exactly one vec2
 * attribute at location zero, no clip planes, no private shader memory, and
 * an ordinary VS/FS pair.  The output is reflection only; it cannot carry a
 * packet opcode, a GPU address, a buffer stride, or a render target. */
static bool write_pipeline(const char *path, const ir3_shader_variant *vertex,
                           uint32_t vertex_application_ubos,
                           const ir3_shader_variant *fragment,
                           uint32_t fragment_application_ubos)
{
    ir3_shader_linkage linkage = {};
    mesart_pipeline_header header = {};
    const uint32_t position_regid = ir3_find_output_regid(vertex, VARYING_SLOT_POS);
    const uint32_t point_size_regid = ir3_find_output_regid(vertex, VARYING_SLOT_PSIZ);
    const uint32_t color0_regid = fragment_color0_regid(fragment);
    const uint32_t color0_alias_mask = fragment_color0_alias_mask(fragment);

    linkage.primid_loc = 0xffu;
    linkage.viewid_loc = 0xffu;
    linkage.clip0_loc = 0xffu;
    linkage.clip1_loc = 0xffu;
    if (!vertex || !fragment || !vertex->writes_pos || color0_regid == INVALID_REG ||
        (color0_alias_mask & ~0x0fu) ||
        vertex->attr_in != 1u || vertex->inputs_count < 1u ||
        /* Mesa's IR3 uses VERT_ATTRIB_GENERIC0 (slot 15) for GLSL
         * layout(location = 0), not the user-facing location number. */
        vertex->inputs[0].sysval || vertex->inputs[0].slot != 15u ||
        vertex->inputs[0].compmask != 0x3u || vertex->pvtmem_size != 0u ||
        fragment->pvtmem_size != 0u || vertex->shared_size != 0u ||
        fragment->shared_size != 0u || vertex->clip_mask || vertex->cull_mask ||
        fragment->reads_primid || position_regid == INVALID_REG ||
        point_size_regid == INVALID_REG || vertex->info.max_reg >= 64 ||
        vertex->info.max_half_reg >= 64 || fragment->info.max_reg >= 64 ||
        fragment->info.max_half_reg >= 64 ||
        vertex->branchstack > 255u || fragment->branchstack > 255u) {
        std::fprintf(stderr,
                     "[mesart-glslc] rejected first VBO profile: attr_in=%u "
                     "inputs=%u slot=%u mask=0x%x sysval=%u pos=%u psize=%u "
                     "color=%u\\n",
                     vertex ? vertex->attr_in : 0u,
                     vertex ? vertex->inputs_count : 0u,
                     vertex && vertex->inputs_count ? vertex->inputs[0].slot : 0u,
                     vertex && vertex->inputs_count ? vertex->inputs[0].compmask : 0u,
                     vertex && vertex->inputs_count ? vertex->inputs[0].sysval : 0u,
                     position_regid, point_size_regid, color0_regid);
        return false;
    }

    ir3_link_shaders(&linkage, vertex, fragment, true);
    if (linkage.cnt > MESART_PIPELINE_MAX_VARYINGS || linkage.max_loc >= 128u)
        return false;

    header.magic = MESART_PIPELINE_MAGIC;
    header.version = MESART_PIPELINE_VERSION;
    header.header_bytes = MESART_PIPELINE_HEADER_BYTES;
    header.chip_id = kMesartX185ChipId;
    header.vertex_code_dwords = vertex->info.sizedwords;
    header.fragment_code_dwords = fragment->info.sizedwords;
    header.vertex_instruction_groups = vertex->instrlen;
    header.fragment_instruction_groups = fragment->instrlen;
    header.vertex_const_vec4s = vertex->constlen;
    header.fragment_const_vec4s = fragment->constlen;
    header.vertex_sampler_count = (uint16_t)vertex->num_samp;
    header.fragment_sampler_count = (uint16_t)fragment->num_samp;
    if (vertex_application_ubos > MESART_IR3_BLOB_MAX_UBOS ||
        fragment_application_ubos > MESART_IR3_BLOB_MAX_UBOS)
        return false;
    header.vertex_app_ubo_count = (uint16_t)vertex_application_ubos;
    header.fragment_app_ubo_count = (uint16_t)fragment_application_ubos;
    header.vertex = pipeline_stage(vertex, true);
    header.fragment = pipeline_stage(fragment, false);
    header.fragment.primary_output_regid = (uint16_t)color0_regid;
    header.fragment.output_const_mask = color0_alias_mask;
    header.vertex_attribute_count = 1u;
    header.vertex_attribute0_slot = vertex->inputs[0].slot;
    header.vertex_attribute0_regid = vertex->inputs[0].regid;
    header.vertex_attribute0_compmask = vertex->inputs[0].compmask;

    /* Match fd6_program.cc's final VS VPC setup: normal FS-consumed
     * varyings first, then position and point size. */
    header.position_location = linkage.max_loc;
    ir3_link_add(&linkage, VARYING_SLOT_POS, position_regid, 0xfu, linkage.max_loc);
    header.point_size_location = linkage.max_loc;
    ir3_link_add(&linkage, VARYING_SLOT_PSIZ, point_size_regid, 0x1u, linkage.max_loc);
    if (linkage.cnt > MESART_PIPELINE_MAX_VARYINGS || linkage.max_loc >= 128u)
        return false;
    header.varying_count = linkage.cnt;
    header.varying_stride_dwords = linkage.max_loc;
    for (uint32_t i = 0u; i < 4u; ++i)
        header.varying_mask[i] = linkage.varmask[i];
    for (uint32_t i = 0u; i < linkage.cnt; ++i) {
        header.varyings[i].slot = linkage.var[i].slot;
        header.varyings[i].regid = linkage.var[i].regid;
        header.varyings[i].compmask = linkage.var[i].compmask;
        header.varyings[i].location = linkage.var[i].loc;
    }

    static_assert(sizeof(mesart_pipeline_header) == MESART_PIPELINE_HEADER_BYTES,
                  "The shared MPIP header must remain a fixed 256-byte prefix.");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    return output.good();
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }

    if (mesart_stage_for_glsl_path(argv[1]) != MESART_IR3_SHADER_VERTEX ||
        mesart_stage_for_glsl_path(argv[2]) != MESART_IR3_SHADER_FRAGMENT) {
        std::fprintf(stderr, "Mesart requires one .vert followed by one .frag source.\n");
        return 3;
    }

    gl_context context = {};
    standalone_options options = {};
    /* Mesa's standalone helper supplies a complete GLES 3.0 capability
     * table, including default-uniform limits.  Its 3.1 selector currently
     * leaves that table unset, rejecting even a single vec2 uniform.  The
     * first graphics profiles use only GLES 3.0 language features. */
    options.glsl_version = 300;
    options.do_link = 1;
    gl_shader_program *program =
        standalone_compile_shader(&options, 2u, &argv[1], &context);
    if (!program || program->NumShaders != 2u || !program->data->LinkStatus) {
        std::fprintf(stderr, "Mesa GLSL ES compilation failed.\n");
        if (program)
            standalone_compiler_cleanup(program, &context);
        return 4;
    }
    std::fprintf(stderr, "[mesart-glslc] GLSL ES source accepted\n");

    gl_linked_shader *vertex = program->_LinkedShaders[MESA_SHADER_VERTEX];
    gl_linked_shader *fragment = program->_LinkedShaders[MESA_SHADER_FRAGMENT];
    if (!vertex || !vertex->Program || !vertex->Program->nir ||
        !fragment || !fragment->Program || !fragment->Program->nir) {
        std::fprintf(stderr, "Mesa could not link GLSL ES into valid NIR stages.\n");
        standalone_compiler_cleanup(program, &context);
        return 5;
    }

    fd_dev_id device_id = {};
    device_id.chip_id = kMesartX185ChipId;
    const struct fd_dev_info *device_info = fd_dev_info_raw(&device_id);
    if (!device_info) {
        std::fprintf(stderr, "Pinned Mesa has no Adreno X1-85 device record.\n");
        standalone_compiler_cleanup(program, &context);
        return 6;
    }
    std::fprintf(stderr, "[mesart-glslc] X1-85 Mesa device record selected\n");

    ir3_compiler_options compiler_options = {};
    ir3_compiler *compiler = ir3_compiler_create(
        nullptr, &device_id, device_info, &compiler_options);
    if (!compiler) {
        std::fprintf(stderr, "Could not create the Mesa Freedreno compiler.\n");
        standalone_compiler_cleanup(program, &context);
        return 7;
    }
    std::fprintf(stderr, "[mesart-glslc] Freedreno IR3 compiler created\n");

    std::fprintf(stderr, "[mesart-glslc] GLSL program linked to NIR\n");

    gl_linked_shader *stages[] = { vertex, fragment };
    const uint32_t stage_types[] = {
        MESART_IR3_SHADER_VERTEX, MESART_IR3_SHADER_FRAGMENT
    };
    const char *output_paths[] = { argv[3], argv[4] };
    ir3_shader *shaders[2] = {};
    ir3_shader_variant *variants[2] = {};
    uint32_t application_ubos[2] = {};
    bool written = true;
    for (uint32_t index = 0; index < 2u; ++index) {
        /* The standalone link path performs Mesa's mandatory cross-stage and
         * dereference lowering.  Detach the NIR before program cleanup. */
        nir_shader *nir = stages[index]->Program->nir;
        stages[index]->Program->nir = nullptr;
        /* standalone_compile_shader used generic GLSL compiler options.  The
         * linked NIR must advertise the capabilities of the exact Adreno
         * compiler that will run IR3's device-specific lowering passes. */
        nir->options = ir3_get_compiler_options(compiler);

        ir3_shader_options ir3_options = {};
        ir3_options.api_wavesize = IR3_SINGLE_ONLY;
        ir3_options.real_wavesize = IR3_SINGLE_ONLY;

        /* GLSL standalone leaves built-ins such as gl_VertexID as deref
         * variables.  The state tracker lowers those before Freedreno sees
         * them; our host bridge must perform that same boundary pass. */
        nir_lower_system_values(nir);
        /* Default GLSL uniforms are not IR3 variables.  Mesa's normal state
         * tracker rewrites them into a bounded UBO before driver lowering;
         * without this, a `load_deref` uniform reaches Freedreno and trips
         * NIR validation.  The generated MIR3/MPIP records its resulting
         * application-UBO count for DihOS to validate later. */
        nir_lower_io(nir, nir_var_uniform, mesart_uniform_vec4_slots,
                     (nir_lower_io_options)0);
        nir_lower_uniforms_to_ubo(nir, false, false);
        if (!nir->info.io_lowered) {
            ir3_nir_lower_io_vars_to_temporaries(nir);
            ir3_nir_lower_io(nir);
        }
        ir3_finalize_nir(compiler, &ir3_options.nir_options, nir);
        nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
        /* Capture the API-visible UBO count before IR3 post-finalization.
         * That later pass adds an internal `$consts` UBO and Mesa's
         * const_state also clamps its app count to one. Neither is a GLSL
         * application resource DihOS must expose. */
        application_ubos[index] = nir->info.num_ubos;
        std::fprintf(stderr, "[mesart-glslc] NIR finalized for IR3 (%s)\n",
                     index == 0u ? "vertex" : "fragment");
        ir3_shader *shader = ir3_shader_from_nir(compiler, nir, &ir3_options);
        ir3_shader_key key = {};
        ir3_shader_variant *variant =
            shader ? ir3_shader_create_variant(shader, &key, false) : nullptr;
        if (!variant) {
            std::fprintf(stderr, "Mesa Freedreno could not compile the %s NIR stage to IR3.\n",
                         index == 0u ? "vertex" : "fragment");
            if (shader)
                ir3_shader_destroy(shader);
            else
                ralloc_free(nir);
            written = false;
            break;
        }
        std::fprintf(stderr, "[mesart-glslc] IR3 machine code generated (%s)\n",
                     index == 0u ? "vertex" : "fragment");
        shaders[index] = shader;
        variants[index] = variant;
    }
    if (written) {
        for (uint32_t index = 0u; index < 2u; ++index) {
            if (!write_blob(output_paths[index], stage_types[index],
                            variants[index], application_ubos[index])) {
                std::fprintf(stderr, "Could not write the %s MIR3 shader artifact.\n",
                             index == 0u ? "vertex" : "fragment");
                written = false;
                break;
            }
            std::printf("Wrote %s MIR3: %u dwords (%u bytes) for Adreno X1-85.\n",
                        index == 0u ? "vertex" : "fragment",
                        variants[index]->info.sizedwords,
                        variants[index]->info.sizedwords * (uint32_t)sizeof(uint32_t));
        }
    }
    if (written && !write_pipeline(argv[5], variants[0], application_ubos[0],
                                   variants[1], application_ubos[1])) {
        std::fprintf(stderr, "Could not write an initial-profile MPIP pipeline artifact.\n");
        written = false;
    } else if (written) {
        std::printf("Wrote %s signed-pipeline input for Adreno X1-85.\n", argv[5]);
    }
    for (uint32_t index = 0u; index < 2u; ++index)
        if (shaders[index])
            ir3_shader_destroy(shaders[index]);
    ir3_compiler_destroy(compiler);
    std::fprintf(stderr, "[mesart-glslc] compiler released\n");
    standalone_compiler_cleanup(program, &context);
    return written ? 0 : 10;
}
