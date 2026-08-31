#pragma once

#include <stdint.h>

#include "asm/aa64_user.h"
#include "mesart/mesart_bundle.h"
#include "mesart/mesart_elf.h"
#include "process/dihos_process.h"

/* This owns all resources belonging to one verified Mesa renderer service.
 * It is intentionally separate from both the SACX loader and its process ABI. */
#define MESART_RENDERER_STACK_PAGES 8ull
#define MESART_RENDERER_STACK_TOP   0x0000007fff000000ull

typedef struct mesart_renderer_service
{
    mesart_verified_bundle bundle;
    mesart_loaded_image image;
    aarch64_user_vm vm;
    void *stack_memory;
    uint64_t stack_pages;
    uint64_t stack_top_va;
    dihos_process_handle process;
} mesart_renderer_service;

/* Admission verifies the manifest with the kernel-selected root, re-hashes
 * the renderer ELF at load time, maps it into a fresh EL0 VM, allocates its
 * private stack, and registers a READY renderer-service process.  It never
 * enters EL0, issues GPU work, or grants raw device/physical-memory access. */
int mesart_renderer_admit(dihos_process_table *processes,
                          const char *bundle_root,
                          mesart_renderer_service *out_service);

/* Must run after the process is stopped/faulted.  It releases the VM before
 * its mapped image and stack backing memory. */
void mesart_renderer_release(mesart_renderer_service *service);

/* Broker for the Mesart-only EL0 syscall number.  It never forwards Linux,
 * KGSL, DRM, MMIO or physical-memory operations to the service. */
int mesart_renderer_syscall(aa64_el0_frame *frame, void *context);
