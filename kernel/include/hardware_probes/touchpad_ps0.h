#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int touchpad_run_ps0(uint64_t rsdp_phys);

#ifdef __cplusplus
}
#endif