#ifndef ACPI_PROBE_HIDI2C_READY_H
#define ACPI_PROBE_HIDI2C_READY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HIDI2C_ACPI_MAX_METHOD_BODY 128

    typedef struct
    {
        uint8_t have_eckb;
        uint8_t have_tcpd;

        uint8_t eckb_addr;
        uint8_t tcpd_addr;

        uint16_t eckb_desc_reg;
        uint16_t tcpd_desc_reg;

        uint8_t eckb_desc_trusted;
        uint8_t tcpd_desc_trusted;

        uint8_t eckb_gpio_valid;
        uint16_t eckb_gpio_pin;
        uint16_t eckb_gpio_flags;
        char eckb_gpio_source[32];

        uint8_t tcpd_gpio_valid;
        uint16_t tcpd_gpio_pin;
        uint16_t tcpd_gpio_flags;
        char tcpd_gpio_source[32];

        uint8_t tcpd_ps0_valid;
        uint16_t tcpd_ps0_len;
        uint8_t tcpd_ps0_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_ps3_valid;
        uint16_t tcpd_ps3_len;
        uint8_t tcpd_ps3_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_sta_valid;
        uint16_t tcpd_sta_len;
        uint8_t tcpd_sta_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_ini_valid;
        uint16_t tcpd_ini_len;
        uint8_t tcpd_ini_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_gio0_reg_valid;
        uint16_t tcpd_gio0_reg_len;
        uint8_t tcpd_gio0_reg_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_dsm_valid;
        uint16_t tcpd_dsm_len;
        uint8_t tcpd_dsm_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t tcpd_gio0_dsm_valid;
        uint16_t tcpd_gio0_dsm_len;
        uint8_t tcpd_gio0_dsm_body[HIDI2C_ACPI_MAX_METHOD_BODY];

        uint8_t eckb_gpio_conn_type;
        uint8_t eckb_gpio_pin_cfg;
        uint8_t eckb_gpio_pin_guessed;

        uint8_t tcpd_gpio_conn_type;
        uint8_t tcpd_gpio_pin_cfg;
        uint8_t tcpd_gpio_pin_guessed;
    } hidi2c_acpi_regs;

    int acpi_hidi2c_get_regs_from_rsdp(uint64_t rsdp_phys, hidi2c_acpi_regs *out);
    void acpi_probe_hidi2c_ready_from_rsdp(uint64_t rsdp_phys);

#ifdef __cplusplus
}
#endif

#endif