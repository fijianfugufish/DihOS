#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int touchpad_try_wake_from_acpi(uint64_t rsdp_phys);

#ifdef __cplusplus
}
#endif