#pragma once
#include <stdint.h>

#define DIHOS_NET_HINT_QCOM  (1u << 0)
#define DIHOS_NET_HINT_WWAN  (1u << 1)
#define DIHOS_NET_HINT_MHI   (1u << 2)
#define DIHOS_NET_HINT_MBIM  (1u << 3)
#define DIHOS_NET_HINT_QMI   (1u << 4)
#define DIHOS_NET_HINT_USB   (1u << 5)
#define DIHOS_NET_HINT_WLAN  (1u << 6)
#define DIHOS_NET_HINT_WIFI  (1u << 7)
#define DIHOS_NET_HINT_WCN   (1u << 8)
#define DIHOS_NET_HINT_SDIO  (1u << 9)

typedef struct
{
    uint32_t kind;      /* 1=dword-range, 2=word-range, 3=fixed-mem32, 4=mem32, 5=qword-range */
    uint32_t rtype;     /* ACPI resource type field */
    uint64_t min_addr;
    uint64_t max_addr;
    uint64_t span_len;
    char dev_name[5];
    char hid_name[17];
} acpi_net_resource_window;

typedef struct
{
    uint32_t conn_type; /* ACPI GPIO connection type: 0=interrupt, 1=I/O */
    uint32_t flags;
    uint32_t pin_config;
    uint32_t pin;
    char dev_name[5];
    char hid_name[17];
} acpi_net_gpio_hint;

#define ACPI_NET_RESOURCE_MAX 32u
#define ACPI_NET_GPIO_MAX 16u

uint32_t acpi_probe_net_candidates_from_rsdp(uint64_t rsdp_phys);
uint32_t acpi_probe_net_resource_count(void);
const acpi_net_resource_window *acpi_probe_net_resources(void);
uint32_t acpi_probe_net_gpio_count(void);
const acpi_net_gpio_hint *acpi_probe_net_gpios(void);
int acpi_probe_net_exec_device_method(uint64_t rsdp_phys, const char dev_name4[4], const char method_name4[4], uint64_t *ret_out);
int acpi_probe_net_exec_device_method_args(uint64_t rsdp_phys,
                                           const char dev_name4[4],
                                           const char method_name4[4],
                                           uint8_t arg_count,
                                           const uint64_t *args,
                                           uint64_t *ret_out);
void acpi_probe_net_exec_context_reset(void);
