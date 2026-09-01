#pragma once

#include <stdint.h>

#include "asm/aa64_user.h"
#include "gpu/gpu_memory.h"
#include "gpu/gpu_ring.h"
#include "gpu/gpu_scheduler.h"
#include "mesart/mesart_bundle.h"
#include "mesart/mesart_elf.h"
#include "process/dihos_process.h"

/* This owns all resources belonging to one verified Mesa renderer service.
 * It is intentionally separate from both the SACX loader and its process ABI. */
#define MESART_RENDERER_STACK_PAGES 8ull
#define MESART_RENDERER_STACK_TOP   0x0000007fff000000ull
#define MESART_RENDERER_MAX_BUFFERS 16u
#define MESART_RENDERER_BOOTSTRAP_BUFFER_BYTES 4096ull
#define MESART_RENDERER_BUFFER_ARENA_BASE      0x0000002000000000ull
#define MESART_RENDERER_BUFFER_SLOT_BYTES      0x0000000001000000ull
#define MESART_RENDERER_COMMAND_BUFFER_BYTES   0x00010000u
#define MESART_RENDERER_COMMAND_BUFFER_VA      0x0000002020000000ull
#define MESART_RENDERER_MAX_BYTES_IN_FLIGHT    (16ull * 1024ull * 1024ull)
#define MESART_RENDERER_MAX_JOBS_IN_FLIGHT     8u

typedef struct mesart_renderer_buffer
{
    /* All backing memory remains owned by the kernel service supervisor. */
    gpu_buffer storage;
    uint64_t user_va;
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
    uint32_t handle;
    uint32_t packet_count;
    uint64_t queued_fence;
    uint64_t completed_fence;
    uint16_t generation;
    uint8_t active;
} mesart_renderer_command_buffer;

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
    mesart_renderer_command_buffer command_buffer;
    uint32_t last_syscall_operation;
    int32_t last_syscall_result;
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
