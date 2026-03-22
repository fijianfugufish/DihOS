#include "gpio/qcom_tlmm.h"

/*
 * Match your repo style here:
 * - if you already have mmio_fence()/mmio_wmb(), use those instead
 * - if not, these compiler barriers are a decent minimum start
 */
static inline void tlmm_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

void tlmm_init(qcom_tlmm_t *t, uintptr_t base, uint32_t size)
{
    t->base = (volatile uint8_t *)base;
    t->size = size;
}

uint32_t tlmm_read32(qcom_tlmm_t *t, uint32_t off)
{
    volatile uint32_t *p = (volatile uint32_t *)(t->base + off);
    tlmm_barrier();
    return *p;
}

void tlmm_write32(qcom_tlmm_t *t, uint32_t off, uint32_t value)
{
    volatile uint32_t *p = (volatile uint32_t *)(t->base + off);
    *p = value;
    tlmm_barrier();
}

void tlmm_rmw32(qcom_tlmm_t *t, uint32_t off, uint32_t clear_mask, uint32_t set_mask)
{
    uint32_t v = tlmm_read32(t, off);
    v &= ~clear_mask;
    v |= set_mask;
    tlmm_write32(t, off, v);
}

bool tlmm_valid_gpio(qcom_tlmm_t *t, uint32_t gpio)
{
    uint32_t ctl = TLMM_GPIO_CTL_OFF(gpio);
    uint32_t io = TLMM_GPIO_IO_OFF(gpio);

    if (!t || !t->base)
        return false;

    if (io >= t->size || ctl >= t->size)
        return false;

    return true;
}

void tlmm_gpio_set_function_gpio(qcom_tlmm_t *t, uint32_t gpio)
{
    tlmm_rmw32(t,
               TLMM_GPIO_CTL_OFF(gpio),
               TLMM_MUX_MASK,
               (TLMM_MUX_FUNC_GPIO << TLMM_MUX_BIT));
}

void tlmm_gpio_set_pull(qcom_tlmm_t *t, uint32_t gpio, tlmm_pull_mode_t pull)
{
    tlmm_rmw32(t,
               TLMM_GPIO_CTL_OFF(gpio),
               TLMM_PULL_MASK,
               ((uint32_t)pull << TLMM_PULL_BIT));
}

void tlmm_gpio_set_drive(qcom_tlmm_t *t, uint32_t gpio, tlmm_drv_strength_t drv)
{
    tlmm_rmw32(t,
               TLMM_GPIO_CTL_OFF(gpio),
               TLMM_DRV_MASK,
               ((uint32_t)drv << TLMM_DRV_BIT));
}

void tlmm_gpio_set_output_enable(qcom_tlmm_t *t, uint32_t gpio, bool enable)
{
    tlmm_rmw32(t,
               TLMM_GPIO_CTL_OFF(gpio),
               TLMM_OE_MASK,
               enable ? TLMM_OE_MASK : 0u);
}

void tlmm_gpio_write(qcom_tlmm_t *t, uint32_t gpio, bool high)
{
    tlmm_rmw32(t,
               TLMM_GPIO_IO_OFF(gpio),
               TLMM_OUT_MASK,
               high ? TLMM_OUT_MASK : 0u);
}

bool tlmm_gpio_read(qcom_tlmm_t *t, uint32_t gpio)
{
    uint32_t v = tlmm_read32(t, TLMM_GPIO_IO_OFF(gpio));
    return (v & TLMM_IN_MASK) != 0;
}

void tlmm_gpio_config_output(qcom_tlmm_t *t,
                             uint32_t gpio,
                             bool initial_high,
                             tlmm_pull_mode_t pull,
                             tlmm_drv_strength_t drv)
{
    if (!tlmm_valid_gpio(t, gpio))
        return;

    tlmm_gpio_set_function_gpio(t, gpio);
    tlmm_gpio_set_pull(t, gpio, pull);
    tlmm_gpio_set_drive(t, gpio, drv);

    /* Set level before enabling OE to avoid a glitch. */
    tlmm_gpio_write(t, gpio, initial_high);
    tlmm_gpio_set_output_enable(t, gpio, true);
}

void tlmm_gpio_config_input(qcom_tlmm_t *t,
                            uint32_t gpio,
                            tlmm_pull_mode_t pull)
{
    if (!tlmm_valid_gpio(t, gpio))
        return;

    tlmm_gpio_set_function_gpio(t, gpio);
    tlmm_gpio_set_pull(t, gpio, pull);
    tlmm_gpio_set_output_enable(t, gpio, false);
}