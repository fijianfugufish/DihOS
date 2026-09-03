#pragma once

#include <stdint.h>

#include "asm/aa64_user.h"
#include "gpu/gpu_memory.h"
#include "gpu/gpu_ring.h"
#include "gpu/gpu_scheduler.h"
#include "mesart/mesart_bundle.h"
#include "mesart/mesart_elf.h"
#include "mesart/mesart_pipeline.h"
#include "process/dihos_process.h"

/* This owns all resources belonging to one verified Mesa renderer service.
 * It is intentionally separate from both the SACX loader and its process ABI. */
#define MESART_RENDERER_STACK_PAGES 8ull
#define MESART_RENDERER_STACK_TOP   0x0000007fff000000ull
#define MESART_RENDERER_MAX_BUFFERS 16u
#define MESART_RENDERER_MAX_SHADER_ASSETS 2u
#define MESART_RENDERER_BOOTSTRAP_BUFFER_BYTES 4096ull
#define MESART_RENDERER_BUFFER_ARENA_BASE      0x0000002000000000ull
#define MESART_RENDERER_BUFFER_SLOT_BYTES      0x0000000001000000ull
#define MESART_RENDERER_GPU_BUFFER_ARENA_BASE  0x0000000040000000ull
#define MESART_RENDERER_COMMAND_BUFFER_BYTES   0x00010000u
#define MESART_RENDERER_COMMAND_BUFFER_VA      0x0000002020000000ull
#define MESART_RENDERER_COMMAND_BUFFER_GPU_VA  0x0000000050000000ull
#define MESART_RENDERER_RUNTIME_HEAP_BYTES      0x00800000u
#define MESART_RENDERER_RUNTIME_HEAP_VA         0x0000002030000000ull
#define MESART_RENDERER_MAX_BYTES_IN_FLIGHT    (16ull * 1024ull * 1024ull)
#define MESART_RENDERER_MAX_JOBS_IN_FLIGHT     8u

typedef struct mesart_renderer_buffer
{
    /* All backing memory remains owned by the kernel service supervisor. */
    gpu_buffer storage;
    uint64_t user_va;
    uint64_t gpu_va;
    uint32_t handle;
    uint16_t generation;
    uint8_t active;
} mesart_renderer_buffer;

typedef struct mesart_renderer_command_buffer
{
    /* source is mapped writable at EL0; snapshot is never mapped at EL0 and
     * is the only representation a future validator may schedule. */
    gpu_buffer source;
    gpu_command_ring snapshot;
    uint64_t user_va;
    uint64_t gpu_va;
    uint32_t handle;
    uint32_t packet_count;
    uint64_t queued_fence;
    uint64_t completed_fence;
    uint16_t generation;
    uint8_t active;
} mesart_renderer_command_buffer;

typedef struct mesart_renderer_shader_asset
{
    /* The blob header remains in a kernel-owned allocation.  Its authenticated
     * IR3 binary is copied into a private 128-byte-aligned slot; only that
     * code_gpu_va may ever be used by the later A7xx pipeline builder. */
    uint32_t buffer_handle;
    uint32_t stage;
    uint32_t code_dwords;
    uint32_t instruction_groups;
    uint32_t const_vec4s;
    uint32_t flags;
    uint16_t sampler_count;
    uint16_t app_ubo_count;
    uint16_t input_count;
    uint16_t output_count;
    uint32_t output_dwords;
    uint64_t code_gpu_va;
    /* Static NIR data appended by Mesa to the authenticated binary.  The
     * render broker alone converts this into an A7xx UBO descriptor. */
    uint64_t constant_data_gpu_va;
    uint32_t constant_data_bytes;
    uint32_t constant_data_ubo_index;
} mesart_renderer_shader_asset;

/* Kernel-only draw request.  Counts are requested by a trusted kernel
 * compositor/client, but are still bounded against compiler-certified MIR3
 * metadata.  Neither this structure nor the resulting pipeline grants EL0 a
 * GPU address, CP register, descriptor address, or MMIO capability. */
typedef struct mesart_renderer_graphics_request
{
    uint32_t vertex_uniform_vec4s;
    uint32_t fragment_uniform_vec4s;
    uint16_t vertex_texture_count;
    uint16_t fragment_texture_count;
} mesart_renderer_graphics_request;

/* Immutable result of resource-count validation.  A later A7xx emitter uses
 * these GPU code addresses and counts to create all PM4 itself. */
typedef struct mesart_renderer_graphics_pipeline
{
    uint64_t vertex_code_gpu_va;
    uint64_t fragment_code_gpu_va;
    uint64_t vertex_constant_data_gpu_va;
    uint64_t fragment_constant_data_gpu_va;
    uint32_t vertex_constant_data_bytes;
    uint32_t fragment_constant_data_bytes;
    uint32_t vertex_constant_data_ubo_index;
    uint32_t fragment_constant_data_ubo_index;
    uint32_t vertex_uniform_vec4s;
    uint32_t fragment_uniform_vec4s;
    uint16_t vertex_texture_count;
    uint16_t fragment_texture_count;
    /* Value copy of the authenticated MPIP reflection.  It contains only
     * compiler facts (register footprints, stage flags and VPC linkage), not
     * a packet stream, a register address, or a GPU memory address.  The
     * future Adreno encoder consumes this rather than guessing state from an
     * IR3 binary. */
    mesart_pipeline_header reflection;
    uint8_t pipeline_reflection_ready;
    uint8_t ready;
} mesart_renderer_graphics_pipeline;

typedef struct mesart_renderer_service
{
    mesart_verified_bundle bundle;
    mesart_loaded_image image;
    aarch64_user_vm vm;
    void *stack_memory;
    uint64_t stack_pages;
    uint64_t stack_top_va;
    /* Bounded EL0 resource slots.  Their handles are opaque and every page
     * is mapped before EL0 begins; runtime allocation remains deliberately
     * unavailable until active-VM TLB maintenance is fully brokered. */
    mesart_renderer_buffer buffers[MESART_RENDERER_MAX_BUFFERS];
    uint64_t buffer_bytes;
    uint32_t bootstrap_buffer_handle;
    uint32_t resource_arena_handle;
    mesart_renderer_shader_asset shaders[MESART_RENDERER_MAX_SHADER_ASSETS];
    uint32_t shader_count;
    /* Authenticated Mesa-derived VS/FS reflection.  It has no GPU address,
     * PM4 packet, or EL0 mapping; a later internal A7xx broker consumes it. */
    mesart_pipeline_header graphics_pipeline_reflection;
    uint8_t graphics_pipeline_reflection_loaded;
    /* This fixed, private allocation is Mesart's first freestanding runtime
     * heap.  It is deliberately distinct from GPU buffers and SACX memory. */
    gpu_buffer runtime_heap;
    mesart_renderer_command_buffer command_buffer;
    uint32_t last_syscall_operation;
    int32_t last_syscall_result;
    /* Kernel-only backend result for the hardware self-test.  It is never
     * supplied by EL0 and exists solely to make a failed GX/CP handoff
     * diagnosable from the shell log. */
    int32_t last_backend_result;
    dihos_process_handle process;
    /* Scheduler identity is bound to the DihOS process, never supplied by
     * EL0.  It is reserved at admission even before submission exists. */
    gpu_scheduler_client scheduler_client;
    uint8_t scheduler_registered;
} mesart_renderer_service;

/* Admission verifies the manifest with the kernel-selected root, re-hashes
 * the renderer ELF at load time, maps it into a fresh EL0 VM, allocates its
 * private stack, and registers a READY renderer-service process.  It never
 * enters EL0, issues GPU work, or grants raw device/physical-memory access. */
int mesart_renderer_admit(dihos_process_table *processes,
                          const char *bundle_root,
                          mesart_renderer_service *out_service);

/* Must run after the process is stopped/faulted.  It cancels any future
 * scheduler work, releases the scheduler identity, then tears down the VM
 * before its mapped image and backing memory. */
void mesart_renderer_release(mesart_renderer_service *service);

/* Broker for the Mesart-only EL0 syscall number.  It never forwards Linux,
 * KGSL, DRM, MMIO or physical-memory operations to the service. */
int mesart_renderer_syscall(aa64_el0_frame *frame, void *context);

/* Test harness only: a real GPU backend, not EL0, will eventually complete a
 * queued fence after hardware reports it. */
int mesart_renderer_test_complete_queued(mesart_renderer_service *service);

/* Validates a fixed graphics pipeline against signed compiler metadata.  It
 * does not allocate resources or submit GPU work; that stays in the later
 * kernel A7xx state/draw broker. */
int mesart_renderer_prepare_graphics_pipeline(
    const mesart_renderer_service *service,
    const mesart_renderer_graphics_request *request,
    mesart_renderer_graphics_pipeline *out_pipeline);
