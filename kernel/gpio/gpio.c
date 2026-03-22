#include "gpio/gpio.h"
#include "terminal/terminal_api.h"
#include <stdint.h>

/*
  IMPORTANT:
  This file is a generic GPIO layer scaffold.

  Right now it does NOT drive real hardware yet.
  It exists so the rest of the kernel can call a stable GPIO API.

  To make it real, replace the stub sections below with your SoC-specific
  GPIO/TLMM register accesses.
*/

static uint8_t g_gpio_init_done = 0;

/*
  Optional shadow state so calls have predictable behaviour even before
  hardware backing exists.
*/
#define GPIO_SHADOW_MAX_PINS 256u

static uint8_t g_gpio_dir_shadow[GPIO_SHADOW_MAX_PINS];
static uint8_t g_gpio_val_shadow[GPIO_SHADOW_MAX_PINS];

int gpio_init(void)
{
    uint32_t i;

    if (g_gpio_init_done)
        return 0;

    for (i = 0; i < GPIO_SHADOW_MAX_PINS; ++i)
    {
        g_gpio_dir_shadow[i] = 0u;
        g_gpio_val_shadow[i] = 0u;
    }

    g_gpio_init_done = 1u;
    terminal_print("GPIO init ok\n");
    return 0;
}

int gpio_set_direction(uint32_t pin, gpio_direction dir)
{
    if (!g_gpio_init_done)
        gpio_init();

    if (pin < GPIO_SHADOW_MAX_PINS)
        g_gpio_dir_shadow[pin] = (uint8_t)dir;

    /*
      TODO: SoC-specific backend here.

      Example later:
        - compute register address for pin
        - configure function as GPIO
        - set OE/output enable bit
    */

    terminal_print("GPIO dir pin:");
    terminal_print_hex32(pin);
    terminal_print(" dir:");
    terminal_print_hex32((uint32_t)dir);
    terminal_print("\n");

    return 0;
}

int gpio_write(uint32_t pin, gpio_value value)
{
    if (!g_gpio_init_done)
        gpio_init();

    if (pin < GPIO_SHADOW_MAX_PINS)
        g_gpio_val_shadow[pin] = (uint8_t)value;

    /*
      TODO: SoC-specific backend here.

      Example later:
        - compute register address for pin
        - write output bit high/low
    */

    terminal_print("GPIO write pin:");
    terminal_print_hex32(pin);
    terminal_print(" val:");
    terminal_print_hex32((uint32_t)value);
    terminal_print("\n");

    return 0;
}

int gpio_read(uint32_t pin, uint32_t *out_value)
{
    uint32_t v = 0u;

    if (!out_value)
        return -1;

    if (!g_gpio_init_done)
        gpio_init();

    /*
      TODO: SoC-specific backend here.

      For now, return shadow value.
    */
    if (pin < GPIO_SHADOW_MAX_PINS)
        v = g_gpio_val_shadow[pin];

    *out_value = v;

    terminal_print("GPIO read pin:");
    terminal_print_hex32(pin);
    terminal_print(" val:");
    terminal_print_hex32(v);
    terminal_print("\n");

    return 0;
}