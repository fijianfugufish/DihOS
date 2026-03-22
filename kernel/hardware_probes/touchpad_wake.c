#include "hardware_probes/touchpad_wake.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "gpio/gpio.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

static void delay_loops(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; ++i)
        __asm__ volatile("");
}

static void mdelay(uint32_t ms)
{
    while (ms--)
        delay_loops(200000);
}

static int pulse_gpio_active_low(uint32_t pin)
{
    if (gpio_set_output(pin) != 0)
        return -1;

    /* deassert first */
    if (gpio_write(pin, GPIO_VALUE_HIGH) != 0)
        return -1;
    mdelay(2);

    /* assert */
    if (gpio_write(pin, GPIO_VALUE_LOW) != 0)
        return -1;
    mdelay(12);

    /* release */
    if (gpio_write(pin, GPIO_VALUE_HIGH) != 0)
        return -1;
    mdelay(50);

    return 0;
}

int touchpad_try_wake_from_acpi(uint64_t rsdp_phys)
{
    hidi2c_acpi_regs r;

    terminal_set_quiet();

    if (acpi_hidi2c_get_regs_from_rsdp(rsdp_phys, &r) != 0)
    {
        terminal_set_loud();
        terminal_error("touchpad: ACPI parse failed\n");
        return -1;
    }

    terminal_set_loud();

    if (!r.have_tcpd)
    {
        terminal_warn("touchpad: TCPD not found\n");
        return -2;
    }

    terminal_print("touchpad: TCPD addr=");
    terminal_print_hex8(r.tcpd_addr);
    terminal_print(" gpioValid=");
    terminal_print_hex8(r.tcpd_gpio_valid);
    terminal_print(" gpioPin=");
    terminal_print_hex32(r.tcpd_gpio_pin);
    terminal_print("\n");

    if (!r.tcpd_gpio_valid)
    {
        terminal_warn("touchpad: no ACPI GPIO to pulse\n");
        return -3;
    }

    /*
     * First attempt:
     * treat the exported TCPD GPIO as a reset/enable-style line.
     * This is only a first probe, not guaranteed correct.
     */
    if (pulse_gpio_active_low((uint32_t)r.tcpd_gpio_pin) != 0)
    {
        terminal_error("touchpad: gpio pulse failed\n");
        return -4;
    }

    terminal_success("touchpad: gpio pulse sent\n");
    return 0;
}