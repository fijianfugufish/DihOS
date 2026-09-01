#pragma once

#include <stdint.h>

/*
 * Mesart is the deliberately small ABI between the verified Mesa/Freedreno
 * service and DihOS.  It is neither DRM, KGSL, nor a general-purpose Unix
 * syscall surface.  Arguments/results live in x0..x5; x8 names the syscall.
 */
#define MESART_ABI_VERSION       1u
#define MESART_EL0_SYSCALL       1u

typedef enum mesart_operation
{
    /* x0 <- ABI version; x1 <- GPU model identifier; x2 <- feature bits;
     * x3..x5 describe the kernel-owned bootstrap buffer. */
    MESART_OPERATION_QUERY_DEVICE = 1u,
    /* x1 = opaque buffer handle; x0 <- status; x1 <- EL0 VA; x2 <- bytes. */
    MESART_OPERATION_QUERY_BUFFER = 2u,
    /* x0 <- status; x1 <- opaque command handle; x2 <- EL0 source VA;
     * x3 <- maximum dwords. */
    MESART_OPERATION_QUERY_COMMAND_BUFFER = 3u,
    /* x1 = command handle; x2 = dwords to snapshot; x0 <- status.
     * This captures a kernel-only sealed copy; it never submits to CP. */
    MESART_OPERATION_CAPTURE_COMMAND_BUFFER = 4u,
    /* x1 = captured command handle; x0 <- status; x1 <- opaque scheduler
     * fence.  Queueing is not hardware submission. */
    MESART_OPERATION_QUEUE_CAPTURED_COMMAND = 5u,
    /* x0 <- status; x1 <- the fixed EL0 runtime heap VA; x2 <- heap bytes;
     * x3 <- Mesart runtime ABI version.  The heap exists before EL0 starts,
     * so this operation cannot alter a live address space. */
    MESART_OPERATION_QUERY_RUNTIME = 6u,
    /* x0 <- status; x1 <- opaque arena handle; x2 <- EL0 VA; x3 <- bytes;
     * x4 <- paired renderer GPU VA.  The arena is fixed at admission and is
     * the only initial source of Mesa GPU resource allocations. */
    MESART_OPERATION_QUERY_GPU_ARENA = 7u,
    /* x0 <- status; x1 <- upstream Freedreno chip ID.  This is separate from
     * the compact DihOS model identifier so Mesa can select its real A7xx
     * device quirks without a DRM/KGSL query. */
    MESART_OPERATION_QUERY_CHIP_ID = 8u,
} mesart_operation;

#define MESART_DEVICE_ADRENO_X1_85 0x07050001u
#define MESART_FREEDRENO_CHIP_ID_ADRENO_X1_85 0xffff43050c01ull

/* Feature discovery is explicit because the renderer is admitted against a
 * fixed kernel ABI.  A feature bit is only advertised after its backing
 * memory and final CP validation boundary exist in the kernel. */
#define MESART_FEATURE_QUERY_DEVICE (1ull << 0)
#define MESART_FEATURE_BOOTSTRAP_BUFFER (1ull << 1)
#define MESART_FEATURE_COMMAND_SNAPSHOT (1ull << 2)
#define MESART_FEATURE_COMMAND_VALIDATION (1ull << 3)
#define MESART_FEATURE_SCHEDULER_QUEUE (1ull << 4)
#define MESART_FEATURE_RUNTIME_HEAP    (1ull << 5)
#define MESART_FEATURE_GPU_BUFFER_VA   (1ull << 6)
#define MESART_FEATURE_GPU_RESOURCE_ARENA (1ull << 7)
#define MESART_FEATURE_FREEDRENO_CHIP_ID (1ull << 8)
#define MESART_FEATURE_COMMAND_SYNC      (1ull << 9)
#define MESART_RUNTIME_ABI_VERSION     1u
#define MESART_RESOURCE_WRITE_TEST_MAGIC 0x4d455341u /* "MESA" */
/* Buffer handles are opaque values supplied by the kernel.  A verified
 * renderer may additionally receive a bounded GPU VA for its own buffer;
 * neither value is a physical address or a general file descriptor. */
