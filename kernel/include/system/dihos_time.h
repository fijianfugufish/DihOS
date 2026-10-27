#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DIHOS_TIME_TICKS_PER_SECOND 60u

    uint64_t dihos_time_monotonic_ms(void);
    void dihos_time_init_utc(uint64_t boot_seconds, uint32_t flags);
    int dihos_time_utc(uint64_t *seconds);
    uint64_t dihos_time_ticks(void);
    uint64_t dihos_time_seconds(void);
    uint32_t dihos_time_fattime(void);

#ifdef __cplusplus
}
#endif
