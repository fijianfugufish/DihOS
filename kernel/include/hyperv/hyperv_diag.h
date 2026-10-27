#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void hyperv_trace_enable(int enabled);
void hyperv_trace(const char *event, int32_t a, int32_t b, int32_t c, int32_t d);
void hyperv_diag_print(void);
void hyperv_timing_record(uint32_t stage, uint64_t started_ms);
#ifdef __cplusplus
}
#endif
