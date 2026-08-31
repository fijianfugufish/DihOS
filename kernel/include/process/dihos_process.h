#pragma once

#include <stdint.h>

/*
 * DihOS process supervision is separate from both SACX and Mesa.  It models
 * an isolated execution domain before architecture code supplies an EL0
 * address space and a runnable thread.  A process handle is never a pointer;
 * generation checks make stale handles harmless after teardown.
 */

#define DIHOS_PROCESS_MAX 16u
#define DIHOS_PROCESS_MIN_MEMORY_BYTES 4096ull

typedef enum dihos_process_kind
{
    /* Kernel-started service; Mesa/Freedreno is the first consumer. */
    DIHOS_PROCESS_KIND_RENDERER_SERVICE = 1,
    /* Reserved for later DihOS-native user services and applications. */
    DIHOS_PROCESS_KIND_NATIVE = 2,
} dihos_process_kind;

typedef enum dihos_process_state
{
    DIHOS_PROCESS_EMPTY = 0,
    DIHOS_PROCESS_LOADING,
    DIHOS_PROCESS_READY,
    DIHOS_PROCESS_RUNNING,
    DIHOS_PROCESS_EXITED,
    DIHOS_PROCESS_FAULTED,
} dihos_process_state;

typedef struct dihos_process_handle
{
    uint16_t slot;
    uint16_t generation;
} dihos_process_handle;

#define DIHOS_PROCESS_INVALID ((dihos_process_handle){0xffffu, 0u})

/* These are kernel-checked named capabilities, never raw syscall-number
 * access.  No capability grants MMIO, physical memory, or scanout ownership. */
#define DIHOS_PROCESS_CAP_BUNDLE_READ       (1ull << 0)
#define DIHOS_PROCESS_CAP_IPC               (1ull << 1)
#define DIHOS_PROCESS_CAP_CLOCK             (1ull << 2)
#define DIHOS_PROCESS_CAP_GFX_RENDERER      (1ull << 3)
#define DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE (1ull << 4)

#define DIHOS_PROCESS_CAP_KNOWN \
    (DIHOS_PROCESS_CAP_BUNDLE_READ | DIHOS_PROCESS_CAP_IPC | \
     DIHOS_PROCESS_CAP_CLOCK | DIHOS_PROCESS_CAP_GFX_RENDERER | \
     DIHOS_PROCESS_CAP_COMPOSITOR_SURFACE)

typedef struct dihos_process_desc
{
    dihos_process_kind kind;
    uint64_t memory_limit_bytes;
    uint64_t capabilities;
    /* Immutable, signed manifest identity supplied by the admitting loader. */
    uint8_t image_sha256[32];
} dihos_process_desc;

typedef struct dihos_process_info
{
    dihos_process_handle handle;
    dihos_process_kind kind;
    dihos_process_state state;
    uint64_t address_space_id;
    uint64_t memory_limit_bytes;
    uint64_t capabilities;
} dihos_process_info;

typedef struct dihos_process_slot
{
    dihos_process_desc desc;
    uint64_t address_space_id;
    uint16_t generation;
    uint8_t active;
    dihos_process_state state;
} dihos_process_slot;

typedef struct dihos_process_table
{
    dihos_process_slot slots[DIHOS_PROCESS_MAX];
    uint64_t next_address_space_id;
} dihos_process_table;

void dihos_process_init(dihos_process_table *table);
int dihos_process_create(dihos_process_table *table,
                         const dihos_process_desc *desc,
                         dihos_process_handle *out_handle);
int dihos_process_set_ready(dihos_process_table *table,
                            dihos_process_handle handle);
int dihos_process_set_running(dihos_process_table *table,
                              dihos_process_handle handle);
int dihos_process_exit(dihos_process_table *table,
                       dihos_process_handle handle);
/* Faulting a process revokes every capability before the scheduler or GPU
 * backend can reuse the slot. */
int dihos_process_fault(dihos_process_table *table,
                        dihos_process_handle handle);
/* The owner calls reap only after its address space, IPC endpoints, and GPU
 * work have been destroyed.  Reuse advances the slot generation. */
int dihos_process_reap(dihos_process_table *table,
                       dihos_process_handle handle);
int dihos_process_query(const dihos_process_table *table,
                        dihos_process_handle handle,
                        dihos_process_info *out_info);
