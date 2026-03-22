#ifndef QCOM_TLMM_H
#define QCOM_TLMM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * X1E80100 TLMM notes from upstream Linux:
 * - per-GPIO register stride: 0x1000
 * - ctl = 0x000 + stride*gpio
 * - io  = 0x004 + stride*gpio
 * - intr_cfg = 0x008 + stride*gpio
 * - intr_status = 0x00c + stride*gpio
 *
 * This header assumes normal GPIOs, not special SDC/UFS pins.
 */

#define TLMM_GPIO_STRIDE 0x1000u

#define TLMM_GPIO_CTL_OFF(gpio) (0x000u + (uint32_t)(gpio) * TLMM_GPIO_STRIDE)
#define TLMM_GPIO_IO_OFF(gpio) (0x004u + (uint32_t)(gpio) * TLMM_GPIO_STRIDE)
#define TLMM_GPIO_INTR_CFG_OFF(gpio) (0x008u + (uint32_t)(gpio) * TLMM_GPIO_STRIDE)
#define TLMM_GPIO_INTR_STATUS_OFF(gpio) (0x00Cu + (uint32_t)(gpio) * TLMM_GPIO_STRIDE)

/* CTL register fields */
#define TLMM_PULL_BIT 0u
#define TLMM_PULL_MASK (0x3u << TLMM_PULL_BIT)

#define TLMM_MUX_BIT 2u
#define TLMM_MUX_MASK (0xFu << TLMM_MUX_BIT)

#define TLMM_DRV_BIT 6u
#define TLMM_DRV_MASK (0x7u << TLMM_DRV_BIT)

#define TLMM_OE_BIT 9u
#define TLMM_OE_MASK (1u << TLMM_OE_BIT)

#define TLMM_EGPIO_PRESENT_BIT 11u
#define TLMM_EGPIO_ENABLE_BIT 12u
#define TLMM_I2C_PULL_BIT 13u

/* IO register fields */
#define TLMM_IN_BIT 0u
#define TLMM_OUT_BIT 1u
#define TLMM_IN_MASK (1u << TLMM_IN_BIT)
#define TLMM_OUT_MASK (1u << TLMM_OUT_BIT)

/* Common values */
#define TLMM_MUX_FUNC_GPIO 0u

typedef enum tlmm_pull_mode
{
    TLMM_PULL_NONE = 0,
    TLMM_PULL_DOWN = 1,
    TLMM_PULL_KEEPER = 2,
    TLMM_PULL_UP = 3,
} tlmm_pull_mode_t;

/*
 * Qualcomm drive-strength encodings vary by family in meaning, but the field
 * width/position above is what upstream Linux uses for X1E80100.
 * Keep raw encoded values for now until we confirm exact mA mapping needed.
 */
typedef enum tlmm_drv_strength
{
    TLMM_DRV_0 = 0,
    TLMM_DRV_1 = 1,
    TLMM_DRV_2 = 2,
    TLMM_DRV_3 = 3,
    TLMM_DRV_4 = 4,
    TLMM_DRV_5 = 5,
    TLMM_DRV_6 = 6,
    TLMM_DRV_7 = 7,
} tlmm_drv_strength_t;

typedef struct qcom_tlmm
{
    volatile uint8_t *base;
    uint32_t size;
} qcom_tlmm_t;

void tlmm_init(qcom_tlmm_t *t, uintptr_t base, uint32_t size);

uint32_t tlmm_read32(qcom_tlmm_t *t, uint32_t off);
void tlmm_write32(qcom_tlmm_t *t, uint32_t off, uint32_t value);
void tlmm_rmw32(qcom_tlmm_t *t, uint32_t off, uint32_t clear_mask, uint32_t set_mask);

bool tlmm_valid_gpio(qcom_tlmm_t *t, uint32_t gpio);

void tlmm_gpio_set_function_gpio(qcom_tlmm_t *t, uint32_t gpio);
void tlmm_gpio_set_pull(qcom_tlmm_t *t, uint32_t gpio, tlmm_pull_mode_t pull);
void tlmm_gpio_set_drive(qcom_tlmm_t *t, uint32_t gpio, tlmm_drv_strength_t drv);
void tlmm_gpio_set_output_enable(qcom_tlmm_t *t, uint32_t gpio, bool enable);

void tlmm_gpio_write(qcom_tlmm_t *t, uint32_t gpio, bool high);
bool tlmm_gpio_read(qcom_tlmm_t *t, uint32_t gpio);

void tlmm_gpio_config_output(qcom_tlmm_t *t,
                             uint32_t gpio,
                             bool initial_high,
                             tlmm_pull_mode_t pull,
                             tlmm_drv_strength_t drv);

void tlmm_gpio_config_input(qcom_tlmm_t *t,
                            uint32_t gpio,
                            tlmm_pull_mode_t pull);

#endif