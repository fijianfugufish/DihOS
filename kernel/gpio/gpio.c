#include "gpio/gpio.h"
#include "gpio/qcom_tlmm.h"
#include "terminal/terminal_api.h"
#include "bootinfo.h"
#include <stdint.h>

extern const boot_info *k_bootinfo_ptr;

static uint8_t g_gpio_init_done = 0;
static uint8_t g_tlmm_ready = 0;
static qcom_tlmm_t g_tlmm;

/* Temporary fallback for X1E80100 bring-up */
#define TLMM_FALLBACK_BASE 0x0F100000ull
#define TLMM_FALLBACK_SIZE 0x00F00000u

static int gpio_backend_init_once(void)
{
    uintptr_t base = 0;
    uint32_t size = 0;

    if (g_tlmm_ready)
        return 0;

    if (k_bootinfo_ptr && k_bootinfo_ptr->tlmm_mmio_base && k_bootinfo_ptr->tlmm_mmio_size)
    {
        base = (uintptr_t)k_bootinfo_ptr->tlmm_mmio_base;
        size = (uint32_t)k_bootinfo_ptr->tlmm_mmio_size;

        terminal_print("TLMM source: bootinfo\n");
    }
    else
    {
        base = (uintptr_t)TLMM_FALLBACK_BASE;
        size = TLMM_FALLBACK_SIZE;

        terminal_print("TLMM source: fallback\n");
    }

    if (!base || !size)
        return -1;

    tlmm_init(&g_tlmm, base, size);
    g_tlmm_ready = 1;

    terminal_print("TLMM base:");
    terminal_print_inline_hex64((uint64_t)base);

    terminal_print("TLMM size:");
    terminal_print_inline_hex32(size);

    return 0;
}

int gpio_init(void)
{
    if (g_gpio_init_done)
        return 0;

    if (gpio_backend_init_once() != 0)
    {
        terminal_error("GPIO backend init failed\n");
        return -1;
    }

    g_gpio_init_done = 1;
    terminal_print("GPIO init ok\n");
    return 0;
}

int gpio_set_direction(uint32_t pin, gpio_direction dir)
{
    if (!g_gpio_init_done && gpio_init() != 0)
        return -1;

    if (!tlmm_valid_gpio(&g_tlmm, pin))
        return -2;

    if (dir == GPIO_DIR_OUTPUT)
        tlmm_gpio_config_output(&g_tlmm, pin, false, TLMM_PULL_NONE, TLMM_DRV_3);
    else
        tlmm_gpio_config_input(&g_tlmm, pin, TLMM_PULL_NONE);

    return 0;
}

int gpio_write(uint32_t pin, gpio_value value)
{
    if (!g_gpio_init_done && gpio_init() != 0)
        return -1;

    if (!tlmm_valid_gpio(&g_tlmm, pin))
        return -2;

    tlmm_gpio_write(&g_tlmm, pin, value == GPIO_VALUE_HIGH);
    return 0;
}

int gpio_read(uint32_t pin, uint32_t *out_value)
{
    if (!out_value)
        return -1;

    if (!g_gpio_init_done && gpio_init() != 0)
        return -1;

    if (!tlmm_valid_gpio(&g_tlmm, pin))
        return -2;

    *out_value = tlmm_gpio_read(&g_tlmm, pin) ? 1u : 0u;
    return 0;
}