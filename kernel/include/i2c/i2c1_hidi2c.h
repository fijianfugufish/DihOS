#ifndef I2C1_HIDI2C_H
#define I2C1_HIDI2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define I2C1_MMIO_BASE 0x00B80000u
#define I2C1_MMIO_SIZE 0x00004000u
#define I2C1_IRQ_GSI 0x00000195u

#define ECKB_I2C_ADDR 0x3Au
#define ECKB_GPIO_PIN 0x0180u
#define TCPD_I2C_ADDR 0x2Cu
#define TCPD_GPIO_PIN 0x03C0u
#define HIDI2C_REPORT_DESC_MAX 1024u

#define HIDI2C_DESC_REG_GUESS 0x0001u

    typedef struct hidi2c_desc
    {
        uint16_t wHIDDescLength;
        uint16_t bcdVersion;
        uint16_t wReportDescLength;
        uint16_t wReportDescRegister;
        uint16_t wInputRegister;
        uint16_t wMaxInputLength;
        uint16_t wOutputRegister;
        uint16_t wMaxOutputLength;
        uint16_t wCommandRegister;
        uint16_t wDataRegister;
        uint16_t wVendorID;
        uint16_t wProductID;
        uint16_t wVersionID;
        uint32_t valid;
    } hidi2c_desc;

    typedef struct hidi2c_raw_report
    {
        uint8_t data[128];
        uint32_t len;
        uint8_t available;
    } hidi2c_raw_report;

    typedef struct hidi2c_device
    {
        const char *name;
        uint8_t i2c_addr_7bit;
        uint32_t gpio_pin;
        uint16_t hid_desc_reg;
        hidi2c_desc desc;
        hidi2c_raw_report last_report;
        uint8_t report_desc[HIDI2C_REPORT_DESC_MAX];
        uint16_t report_desc_len;
        uint8_t report_desc_valid;
        uint8_t online;
    } hidi2c_device;

    int i2c1_bus_init(void);
    int i2c1_bus_write_read(uint8_t addr7,
                            const void *tx, uint32_t tx_len,
                            void *rx, uint32_t rx_len);
    int i2c1_bus_write_read_combined(uint8_t addr7,
                                     const void *tx, uint32_t tx_len,
                                     void *rx, uint32_t rx_len);
    int i2c1_bus_write(uint8_t addr7, const void *tx, uint32_t tx_len);
    int i2c1_bus_read(uint8_t addr7, void *rx, uint32_t rx_len);
    int i2c1_bus_addr_only(uint8_t addr7);

    void i2c1_hidi2c_init(uint64_t rsdp_phys);
    void i2c1_hidi2c_poll(void);

    const hidi2c_device *i2c1_hidi2c_keyboard(void);
    const hidi2c_device *i2c1_hidi2c_touchpad(void);

#ifdef __cplusplus
}
#endif

#endif
