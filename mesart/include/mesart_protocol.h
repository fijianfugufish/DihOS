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
    /* x0 <- ABI version; x1 <- GPU model identifier; x2 <- feature bits. */
    MESART_OPERATION_QUERY_DEVICE = 1u,
} mesart_operation;

#define MESART_DEVICE_ADRENO_X1_85 0x07050001u

/* This query operation is the only exposed feature today.  Buffer ownership,
 * command validation, GPU-VA mapping, submission and fences are added only
 * after their kernel brokers are fully implemented. */
#define MESART_FEATURE_QUERY_DEVICE (1ull << 0)
