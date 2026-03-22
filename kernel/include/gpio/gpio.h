#pragma once
#include <stdint.h>

typedef enum
{
    GPIO_DIR_INPUT = 0,
    GPIO_DIR_OUTPUT = 1
} gpio_direction;

typedef enum
{
    GPIO_VALUE_LOW = 0,
    GPIO_VALUE_HIGH = 1
} gpio_value;

/*
  Generic GPIO API.

  Current status:
  - compiles
  - logs calls
  - backend hooks are ready
  - SoC-specific MMIO implementation still needs to be filled in
*/

int gpio_init(void);
int gpio_set_direction(uint32_t pin, gpio_direction dir);
int gpio_write(uint32_t pin, gpio_value value);
int gpio_read(uint32_t pin, uint32_t *out_value);

/* convenience */
static inline int gpio_set_output(uint32_t pin)
{
    return gpio_set_direction(pin, GPIO_DIR_OUTPUT);
}

static inline int gpio_set_input(uint32_t pin)
{
    return gpio_set_direction(pin, GPIO_DIR_INPUT);
}