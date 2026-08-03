#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct hyperv_info
    {
        uint32_t present;
        uint32_t hypervisor_bit;
        uint32_t max_leaf;
        uint32_t interface_id;
        uint32_t discovery;
        char vendor[13];
    } hyperv_info;

    int hyperv_detect(hyperv_info *out, uint64_t acpi_rsdp);
    void hyperv_log_detection(const hyperv_info *info);
    int hyperv_core_init(const hyperv_info *info);
    int hyperv_core_ready(void);
    uint64_t hyperv_hypercall(uint64_t control,
                             uint64_t input_gpa,
                             uint64_t output_gpa);
    int hyperv_set_vpreg(uint32_t reg, uint64_t value);

#ifdef __cplusplus
}
#endif
