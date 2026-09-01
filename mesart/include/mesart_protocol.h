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
} mesart_operation;

#define MESART_DEVICE_ADRENO_X1_85 0x07050001u

/* This query operation is the only exposed feature today.  Its bootstrap
 * buffer is a pre-mapped, renderer-private CPU-visible allocation that proves
 * safe user-memory ownership.  It has no GPU IOVA and cannot be submitted.
 * Command validation, GPU-VA mapping, submission and fences are added only
 * after their kernel brokers are fully implemented. */
#define MESART_FEATURE_QUERY_DEVICE (1ull << 0)
#define MESART_FEATURE_BOOTSTRAP_BUFFER (1ull << 1)
#define MESART_FEATURE_COMMAND_SNAPSHOT (1ull << 2)
#define MESART_FEATURE_COMMAND_VALIDATION (1ull << 3)
#define MESART_FEATURE_SCHEDULER_QUEUE (1ull << 4)
/* Buffer handles are opaque values supplied by the kernel.  They are not
 * addresses, GPU VAs, physical page numbers, or file descriptors. */
