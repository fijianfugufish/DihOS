#include "pci/pci_kernel_nic_probe.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "pci/pci_ecam_lookup.h"
#include "memory/mmio_map.h"
#include "gpio/gpio.h"
#include "terminal/terminal_api.h"
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} efi_mdesc_local;

static uint32_t g_net_hints = 0u;

void pci_kernel_set_net_hints(uint32_t hints)
{
    g_net_hints = hints;
}

static int ranges_overlap(uint64_t a_base, uint64_t a_size, uint64_t b_base, uint64_t b_size)
{
    uint64_t a_end = a_base + a_size;
    uint64_t b_end = b_base + b_size;
    if (!a_size || !b_size)
        return 0;
    if (a_end < a_base)
        a_end = ~0ull;
    if (b_end < b_base)
        b_end = ~0ull;
    return (a_base < b_end) && (b_base < a_end);
}

static int ecam_overlaps_efi_ram(const boot_info *bi, uint64_t base, uint64_t size)
{
    const uint32_t EFI_BOOT_SERVICES_CODE = 3u;
    const uint32_t EFI_BOOT_SERVICES_DATA = 4u;
    const uint32_t EFI_CONVENTIONAL_MEMORY = 7u;
    const uint64_t PAGE_SIZE = 4096ull;

    if (!bi || !bi->mmap || !bi->mmap_size)
        return 0;

    uint8_t *p = (uint8_t *)(uintptr_t)bi->mmap;
    uint8_t *end = p + bi->mmap_size;
    uint64_t dsz = bi->mmap_desc_size ? bi->mmap_desc_size : sizeof(efi_mdesc_local);

    for (; p + dsz <= end; p += dsz)
    {
        const efi_mdesc_local *d = (const efi_mdesc_local *)p;
        uint64_t dbase = d->PhysicalStart;
        uint64_t dsize = d->NumberOfPages * PAGE_SIZE;

        if (!(d->Type == EFI_BOOT_SERVICES_CODE ||
              d->Type == EFI_BOOT_SERVICES_DATA ||
              d->Type == EFI_CONVENTIONAL_MEMORY))
            continue;

        if (ranges_overlap(base, size, dbase, dsize))
            return 1;
    }

    return 0;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline uint8_t mmio_read8(uint64_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static inline uint16_t mmio_read16(uint64_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

static inline void mmio_write16(uint64_t addr, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)addr = value;
}

static inline void mmio_write8(uint64_t addr, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static int parse_pci_segment_from_dev_name(const char *dev_name, uint16_t *seg_out)
{
    uint32_t seg = 0;
    uint32_t i = 3u;
    uint32_t digits = 0u;

    if (!dev_name || !seg_out)
        return 0;

    if (!(dev_name[0] == 'P' && dev_name[1] == 'C' && dev_name[2] == 'I'))
        return 0;

    while (dev_name[i])
    {
        char c = dev_name[i++];
        if (c < '0' || c > '9')
            return 0;
        seg = (seg * 10u) + (uint32_t)(c - '0');
        if (seg > 0xFFFFu)
            return 0;
        digits++;
    }

    if (digits == 0u)
        return 0;

    *seg_out = (uint16_t)seg;
    return 1;
}

static int choose_preferred_segment_from_net_resources(uint16_t *seg_out)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();
    uint16_t fallback_seg = 0;
    int have_fallback = 0;

    if (!seg_out || !res || nres == 0u)
        return 0;

    for (uint32_t i = 0; i < nres; ++i)
    {
        uint16_t seg = 0;
        if (!parse_pci_segment_from_dev_name(res[i].dev_name, &seg))
            continue;

        if (!have_fallback)
        {
            fallback_seg = seg;
            have_fallback = 1;
        }

        if (res[i].kind == 1u && res[i].rtype == 0u)
        {
            *seg_out = seg;
            return 1;
        }
    }

    if (have_fallback)
    {
        *seg_out = fallback_seg;
        return 1;
    }

    return 0;
}

static int is_likely_network_class(uint8_t class_code)
{
    if (class_code == 0x02u) /* Network controller */
        return 1;
    if (class_code == 0x0Du) /* Wireless controller */
        return 1;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

static void short_delay(void)
{
    for (volatile uint32_t i = 0; i < 4000000u; ++i)
    {
    }
}

static void print_acpi_net_resource_windows(void)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();

    terminal_print("[K:PCI] ACPI net resource windows available: ");
    terminal_print_inline_hex64(nres);

    for (uint32_t i = 0; i < nres; ++i)
    {
        terminal_print("[K:PCI] platform-net dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" min=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" max=");
        terminal_print_inline_hex64(res[i].max_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);
    }
}

static int sdhci_reset_cmd_dat(uint64_t base)
{
    const uint8_t SDHCI_RESET_CMD = 0x02u;
    const uint8_t SDHCI_RESET_DAT = 0x04u;
    const uint8_t mask = (uint8_t)(SDHCI_RESET_CMD | SDHCI_RESET_DAT);

    if (!base)
        return -1;

    mmio_write8(base + 0x2Fu, mask);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read8(base + 0x2Fu) & mask) == 0u)
            return 0;
    }

    return -2;
}

static void sdhci_enable_status_events(uint64_t base)
{
    const uint32_t SDHCI_STATUS_EN_CMD_COMPLETE = 1u << 0;
    const uint32_t SDHCI_STATUS_EN_ERROR_SUMMARY = 1u << 15;
    const uint32_t SDHCI_ERROR_STATUS_EN_ALL = 0xFFFF0000u;

    if (!base)
        return;

    /*
      SDHCI has a status-enable register separate from the signal-enable
      register. Keep IRQ signalling off, but allow command/error status bits
      to latch so the polling path can see completion.
    */
    mmio_write32(base + 0x34u,
                 SDHCI_ERROR_STATUS_EN_ALL |
                     SDHCI_STATUS_EN_ERROR_SUMMARY |
                     SDHCI_STATUS_EN_CMD_COMPLETE);
    mmio_write32(base + 0x38u, 0u);
}

static uint16_t sdhci_clock_for_400khz(uint32_t base_mhz, uint16_t version)
{
    const uint16_t SDHCI_CLOCK_INT_EN = 0x0001u;
    uint64_t base_hz = (uint64_t)base_mhz * 1000000ull;
    uint32_t spec = (uint32_t)(version & 0xFFu);
    uint32_t divisor = 0;

    if (base_hz == 0u)
        base_hz = 200000000ull;

    if (spec >= 3u)
    {
        divisor = (uint32_t)((base_hz + 799999ull) / 800000ull);
        if (divisor < 1u)
            divisor = 1u;
        if (divisor > 0x3FFu)
            divisor = 0x3FFu;
    }
    else if (base_hz > 400000ull)
    {
        uint32_t actual_div = 2u;
        divisor = 1u;
        while ((base_hz / actual_div) > 400000ull && divisor < 0x80u)
        {
            divisor <<= 1;
            actual_div <<= 1;
        }
    }

    return (uint16_t)(((divisor & 0xFFu) << 8) |
                      ((divisor & 0x300u) >> 2) |
                      SDHCI_CLOCK_INT_EN);
}

static int sdhci_power_clock_init(uint64_t base, uint32_t caps0, uint16_t version)
{
    const uint8_t SDHCI_RESET_ALL = 0x01u;
    const uint8_t SDHCI_POWER_ON = 0x01u;
    const uint8_t SDHCI_POWER_180 = 0x0Au;
    const uint8_t SDHCI_POWER_300 = 0x0Cu;
    const uint8_t SDHCI_POWER_330 = 0x0Eu;
    const uint16_t SDHCI_CLOCK_INT_STABLE = 0x0002u;
    const uint16_t SDHCI_CLOCK_CARD_EN = 0x0004u;
    uint32_t base_mhz;
    uint16_t clock;
    uint8_t power_sel;

    if (!base)
        return -1;

    mmio_write8(base + 0x2Fu, SDHCI_RESET_ALL);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read8(base + 0x2Fu) & SDHCI_RESET_ALL) == 0u)
            break;
        if (i + 1u == 1000000u)
            return -2;
    }

    mmio_write8(base + 0x2Eu, 0x0Eu);
    sdhci_enable_status_events(base);

    if (caps0 & 0x04000000u)
        power_sel = SDHCI_POWER_180;
    else if (caps0 & 0x02000000u)
        power_sel = SDHCI_POWER_300;
    else
        power_sel = SDHCI_POWER_330;
    mmio_write8(base + 0x29u, (uint8_t)(power_sel | SDHCI_POWER_ON));

    base_mhz = (caps0 >> 8) & 0xFFu;
    if (base_mhz == 0u)
        base_mhz = 200u;

    clock = sdhci_clock_for_400khz(base_mhz, version);
    terminal_print("[K:SDIO] clock ctl=");
    terminal_print_inline_hex64(clock);
    terminal_print(" base_mhz=");
    terminal_print_inline_hex64(base_mhz);
    terminal_print(" spec=");
    terminal_print_inline_hex64((uint64_t)(version & 0xFFu));
    terminal_print(" int_en=");
    terminal_print_inline_hex64(mmio_read32(base + 0x34u));

    mmio_write16(base + 0x2Cu, clock);
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if (mmio_read16(base + 0x2Cu) & SDHCI_CLOCK_INT_STABLE)
            break;
        if (i + 1u == 1000000u)
            return -3;
    }

    mmio_write16(base + 0x2Cu, (uint16_t)(clock | SDHCI_CLOCK_CARD_EN));
    for (uint32_t i = 0; i < 1000000u; ++i)
    {
        if (mmio_read16(base + 0x2Cu) & SDHCI_CLOCK_INT_STABLE)
            break;
        if (i + 1u == 1000000u)
            return -4;
    }
    return 0;
}

static int sdhci_send_cmd(uint64_t base, uint32_t idx, uint32_t arg, uint16_t flags, uint32_t *resp_out)
{
    const uint32_t SDHCI_PRESENT_CMD_INHIBIT = 1u << 0;
    const uint32_t SDHCI_INT_CMD_COMPLETE = 1u << 0;
    const uint32_t SDHCI_INT_ERROR = 1u << 15;
    const uint32_t timeout = 1000000u;
    uint32_t status = 0;

    if (!base)
        return -1;

    if (resp_out)
        *resp_out = 0u;

    sdhci_enable_status_events(base);

    for (uint32_t i = 0; i < timeout; ++i)
    {
        if ((mmio_read32(base + 0x24u) & SDHCI_PRESENT_CMD_INHIBIT) == 0u)
            break;
        if (i + 1u == timeout)
        {
            (void)sdhci_reset_cmd_dat(base);
            return -2;
        }
    }

    mmio_write32(base + 0x30u, 0xFFFFFFFFu);
    mmio_write32(base + 0x08u, arg);
    mmio_write16(base + 0x0Eu, (uint16_t)((idx << 8) | flags));

    for (uint32_t i = 0; i < timeout; ++i)
    {
        status = mmio_read32(base + 0x30u);
        if (status & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR))
            break;
        if (i + 1u == timeout)
        {
            terminal_print("[K:SDIO] CMD timeout idx=");
            terminal_print_inline_hex64(idx);
            terminal_print(" present=");
            terminal_print_inline_hex64(mmio_read32(base + 0x24u));
            terminal_print(" int=");
            terminal_print_inline_hex64(status);
            (void)sdhci_reset_cmd_dat(base);
            return -3;
        }
    }

    if (resp_out)
        *resp_out = mmio_read32(base + 0x10u);
    mmio_write32(base + 0x30u, status);

    if (status & SDHCI_INT_ERROR)
    {
        terminal_print("[K:SDIO] CMD int status=");
        terminal_print_inline_hex64(status);
        terminal_print(" idx=");
        terminal_print_inline_hex64(idx);
        (void)sdhci_reset_cmd_dat(base);
        return -4;
    }

    return 0;
}

static int sdhci_cmd0_go_idle(uint64_t base)
{
    return sdhci_send_cmd(base, 0u, 0u, 0u, 0);
}

static int sdhci_cmd5_probe(uint64_t base, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;

    /* CMD5 arg 0 asks an SDIO card for its OCR. */
    return sdhci_send_cmd(base, 5u, 0u, SDHCI_CMD_RESP_SHORT, resp_out);
}

static int sdhci_cmd5_probe_arg(uint64_t base, uint32_t arg, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;

    return sdhci_send_cmd(base, 5u, arg, SDHCI_CMD_RESP_SHORT, resp_out);
}

static int sdhci_cmd8_probe(uint64_t base, uint32_t *resp_out)
{
    const uint16_t SDHCI_CMD_RESP_SHORT = 0x0002u;
    const uint16_t SDHCI_CMD_CRC = 0x0008u;
    const uint16_t SDHCI_CMD_INDEX = 0x0010u;

    return sdhci_send_cmd(base, 8u, 0x000001AAu,
                          (uint16_t)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX),
                          resp_out);
}

static void sdhci_set_bus_power(uint64_t base, uint8_t power_sel, const char *tag)
{
    const uint8_t SDHCI_POWER_ON = 0x01u;

    if (!base)
        return;

    mmio_write8(base + 0x29u, 0u);
    short_delay();
    mmio_write8(base + 0x29u, (uint8_t)(power_sel | SDHCI_POWER_ON));
    short_delay();

    terminal_print("[K:SDIO] bus power ");
    terminal_print(tag ? tag : "?");
    terminal_print(" power=");
    terminal_print_inline_hex64(mmio_read8(base + 0x29u));
}

static int sdio_try_cmd5_arg(uint64_t base, const char *tag, uint32_t arg, uint32_t *resp_out)
{
    int rc;

    terminal_print("[K:SDIO] CMD5 ");
    terminal_print(tag ? tag : "arg");
    terminal_print(" arg=");
    terminal_print_inline_hex64(arg);

    (void)sdhci_cmd0_go_idle(base);
    rc = sdhci_cmd5_probe_arg(base, arg, resp_out);
    terminal_print("[K:SDIO] CMD5 ");
    terminal_print(tag ? tag : "arg");
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    if (resp_out)
    {
        terminal_print(" resp=");
        terminal_print_inline_hex64(*resp_out);
    }

    return rc;
}

static int sdio_voltage_ocr_retries(uint64_t base, uint32_t caps0, uint8_t original_power, uint32_t *resp_out)
{
    const uint8_t SDHCI_POWER_180 = 0x0Au;
    const uint8_t SDHCI_POWER_300 = 0x0Cu;
    const uint8_t SDHCI_POWER_330 = 0x0Eu;
    const uint32_t SDIO_OCR_27_36V = 0x00FF8000u;
    uint8_t original_sel = (uint8_t)(original_power & 0x0Eu);
    int best_rc;

    best_rc = sdio_try_cmd5_arg(base, "ocr-current", SDIO_OCR_27_36V, resp_out);
    if (best_rc == 0)
        return 0;

    if ((caps0 & 0x02000000u) && original_sel != SDHCI_POWER_300)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_300, "3.0V");
        best_rc = sdio_try_cmd5_arg(base, "3.0V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
        best_rc = sdio_try_cmd5_arg(base, "3.0V-ocr", SDIO_OCR_27_36V, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if ((caps0 & 0x01000000u) && original_sel != SDHCI_POWER_330)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_330, "3.3V");
        best_rc = sdio_try_cmd5_arg(base, "3.3V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
        best_rc = sdio_try_cmd5_arg(base, "3.3V-ocr", SDIO_OCR_27_36V, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if ((caps0 & 0x04000000u) && original_sel != SDHCI_POWER_180)
    {
        sdhci_set_bus_power(base, SDHCI_POWER_180, "1.8V");
        best_rc = sdio_try_cmd5_arg(base, "1.8V-arg0", 0u, resp_out);
        if (best_rc == 0)
            return 0;
    }

    if (original_sel)
        sdhci_set_bus_power(base, original_sel, "restore");

    return best_rc;
}

static void sdio_probe_non_sdio_card(uint64_t base)
{
    uint32_t resp = 0;
    int rc;

    terminal_print("[K:SDIO] CMD8 memory-card probe");
    (void)sdhci_cmd0_go_idle(base);
    rc = sdhci_cmd8_probe(base, &resp);
    terminal_print("[K:SDIO] CMD8 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" resp=");
    terminal_print_inline_hex64(resp);
    if (rc == 0)
        terminal_print("[K:SDIO] CMD8 answered; SDC2 looks like SD memory, not SDIO WiFi");
    else
        terminal_print("[K:SDIO] CMD8 no answer; SDC2 has no visible SD/SDIO card");
}

static int sdio_gpio_wake_and_cmd5(uint64_t base, uint32_t *resp_out)
{
    const acpi_net_gpio_hint *gpios = acpi_probe_net_gpios();
    uint32_t count = acpi_probe_net_gpio_count();
    int best_rc = -10;
    uint32_t tried = 0;

    if (!base || !gpios)
        return -1;

    terminal_print("[K:SDIO] GPIO hint count=");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        int is_sdc2 = str_eq(gpios[i].dev_name, "SDC2") || str_eq(gpios[i].hid_name, "QCOM2466");
        int is_gpio_io = (gpios[i].conn_type == 1u);
        int rc;

        if (!is_sdc2 || !is_gpio_io || gpios[i].pin == 0u || gpios[i].pin > 255u)
            continue;

        tried++;
        terminal_print("[K:SDIO] GPIO wake pin=");
        terminal_print_inline_hex64(gpios[i].pin);
        terminal_print(" flags=");
        terminal_print_inline_hex64(gpios[i].flags);
        terminal_print(" cfg=");
        terminal_print_inline_hex64(gpios[i].pin_config);

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO latch-high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);

        rc = gpio_set_direction(gpios[i].pin, GPIO_DIR_OUTPUT);
        terminal_print("[K:SDIO] GPIO dir rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (rc != 0)
        {
            best_rc = rc;
            continue;
        }

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();

        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, resp_out);
        terminal_print("[K:SDIO] CMD5 after GPIO rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (resp_out)
        {
            terminal_print(" resp=");
            terminal_print_inline_hex64(*resp_out);
        }
        best_rc = rc;
        if (rc == 0)
            return 0;

        rc = gpio_write(gpios[i].pin, GPIO_VALUE_LOW);
        terminal_print("[K:SDIO] GPIO pulse-low rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();
        rc = gpio_write(gpios[i].pin, GPIO_VALUE_HIGH);
        terminal_print("[K:SDIO] GPIO pulse-high rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        short_delay();

        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, resp_out);
        terminal_print("[K:SDIO] CMD5 after GPIO pulse rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (resp_out)
        {
            terminal_print(" resp=");
            terminal_print_inline_hex64(*resp_out);
        }
        best_rc = rc;
        if (rc == 0)
            return 0;
    }

    if (tried == 0u)
        terminal_print("[K:SDIO] no usable SDC2 GpioIo hint");

    return best_rc;
}

static void sdio_bootstrap_identify(uint64_t base)
{
    uint32_t present;
    uint32_t caps0;
    uint32_t caps1;
    uint32_t int_status;
    uint16_t clock;
    uint16_t version;
    uint8_t hostctl;
    uint8_t power;

    if (!base)
        return;

    terminal_print("[K:SDIO] identify base=");
    terminal_print_inline_hex64(base);
    terminal_flush_log();

    present = mmio_read32(base + 0x24u);
    caps0 = mmio_read32(base + 0x40u);
    caps1 = mmio_read32(base + 0x44u);
    int_status = mmio_read32(base + 0x30u);
    hostctl = mmio_read8(base + 0x28u);
    power = mmio_read8(base + 0x29u);
    clock = mmio_read16(base + 0x2Cu);
    version = mmio_read16(base + 0xFEu);

    terminal_print("[K:SDIO] present=");
    terminal_print_inline_hex64(present);
    terminal_print(" caps0=");
    terminal_print_inline_hex64(caps0);
    terminal_print(" caps1=");
    terminal_print_inline_hex64(caps1);
    terminal_print(" version=");
    terminal_print_inline_hex64(version);
    terminal_print(" spec=");
    terminal_print_inline_hex64((uint64_t)(version & 0xFFu));
    terminal_print(" vendor=");
    terminal_print_inline_hex64((uint64_t)((version >> 8) & 0xFFu));
    terminal_print("[K:SDIO] hostctl=");
    terminal_print_inline_hex64(hostctl);
    terminal_print(" power=");
    terminal_print_inline_hex64(power);
    terminal_print(" clock=");
    terminal_print_inline_hex64(clock);
    terminal_print(" int=");
    terminal_print_inline_hex64(int_status);

    if (!(power & 0x01u) || ((clock & 0x0005u) != 0x0005u))
    {
        int init_rc;
        terminal_print("[K:SDIO] init power/clock");
        terminal_flush_log();
        init_rc = sdhci_power_clock_init(base, caps0, version);
        terminal_print("[K:SDIO] init rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)init_rc);
        power = mmio_read8(base + 0x29u);
        clock = mmio_read16(base + 0x2Cu);
        present = mmio_read32(base + 0x24u);
        terminal_print("[K:SDIO] post-init present=");
        terminal_print_inline_hex64(present);
        terminal_print(" power=");
        terminal_print_inline_hex64(power);
        terminal_print(" clock=");
        terminal_print_inline_hex64(clock);
    }

    if ((power & 0x01u) && ((clock & 0x0005u) == 0x0005u))
    {
        uint32_t resp = 0;
        int rc;
        terminal_print("[K:SDIO] CMD5 probe");
        (void)sdhci_cmd0_go_idle(base);
        rc = sdhci_cmd5_probe(base, &resp);
        terminal_print("[K:SDIO] CMD5 rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_print(" resp=");
        terminal_print_inline_hex64(resp);
        if (rc != 0)
        {
            int gpio_rc = sdio_gpio_wake_and_cmd5(base, &resp);
            if (gpio_rc != 0)
            {
                int vrc = sdio_voltage_ocr_retries(base, caps0, power, &resp);
                if (vrc != 0)
                    sdio_probe_non_sdio_card(base);
            }
        }
    }
    else
    {
        terminal_print("[K:SDIO] CMD5 skipped: power/clock still off");
    }
}

static uint32_t map_sdio_bootstrap_windows(const boot_info *bi)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();
    uint32_t mapped = 0u;

    for (uint32_t i = 0; i < nres; ++i)
    {
        int is_sdc2 = str_eq(res[i].dev_name, "SDC2") || str_eq(res[i].hid_name, "QCOM2466");
        int is_mmio = (res[i].kind == 3u || res[i].kind == 4u || res[i].kind == 5u);
        int rc;

        if (!is_sdc2 || !is_mmio || !res[i].min_addr || !res[i].span_len)
            continue;

        if (res[i].span_len > 0x00100000ull)
        {
            terminal_print("[K:PCI] SDIO map skip: window too large");
            terminal_print(" len=");
            terminal_print_inline_hex64(res[i].span_len);
            continue;
        }

        if (ecam_overlaps_efi_ram(bi, res[i].min_addr, res[i].span_len))
        {
            terminal_print("[K:PCI] SDIO map skip: overlaps EFI RAM");
            continue;
        }

        terminal_print("[K:PCI] SDIO bootstrap map dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" base=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);

        rc = mmio_map_device_identity(res[i].min_addr, res[i].span_len);
        terminal_print("[K:PCI] SDIO bootstrap map rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (rc == 0)
        {
            mapped++;
            sdio_bootstrap_identify(res[i].min_addr);
        }
    }

    return mapped;
}

static void probe_pci5_mmio_windows(const boot_info *bi)
{
    const acpi_net_resource_window *res = acpi_probe_net_resources();
    uint32_t nres = acpi_probe_net_resource_count();

    for (uint32_t i = 0; i < nres; ++i)
    {
        uint64_t probe_base;
        uint64_t probe_len;
        int rc;

        if (!str_eq(res[i].dev_name, "PCI5"))
            continue;
        if (!(res[i].kind == 1u && res[i].rtype == 0u))
            continue;
        if (!res[i].min_addr || !res[i].span_len)
            continue;

        probe_base = res[i].min_addr;
        probe_len = res[i].span_len;
        if (probe_len > 0x00100000ull)
            probe_len = 0x00100000ull;

        if (ecam_overlaps_efi_ram(bi, probe_base, probe_len))
        {
            terminal_print("[K:PCI] PCI5 MMIO probe skip: overlaps EFI RAM");
            continue;
        }

        terminal_print("[K:PCI] PCI5 MMIO probe map base=");
        terminal_print_inline_hex64(probe_base);
        terminal_print(" len=");
        terminal_print_inline_hex64(probe_len);
        rc = mmio_map_device_identity(probe_base, probe_len);
        terminal_print("[K:PCI] PCI5 MMIO probe map rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        if (rc != 0)
            continue;
        terminal_print("[K:PCI] PCI5 MMIO probe: mapped (read disabled for abort safety)");
    }
}

void pci_kernel_probe_nics_from_mcfg(const boot_info *bi)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint16_t preferred_seg = 0;
    int have_preferred_seg = 0;
    int has_wlan_hint = ((g_net_hints & (DIHOS_NET_HINT_WLAN | DIHOS_NET_HINT_WIFI | DIHOS_NET_HINT_WCN)) != 0u);
    int has_wwan_hint = ((g_net_hints & (DIHOS_NET_HINT_WWAN | DIHOS_NET_HINT_MHI)) != 0u);
    uint32_t sdc2_maps = 0u;
    uint32_t nic_hits = 0u;

    terminal_print("[K:PCI] aa64 ECAM config reads disabled");
    terminal_print("[K:PCI] reason: unreadable ECAM can raise a fatal external abort");

    have_preferred_seg = choose_preferred_segment_from_net_resources(&preferred_seg);
    if (have_preferred_seg)
    {
        terminal_print("[K:PCI] ACPI points network at PCI segment: ");
        terminal_print_inline_hex64(preferred_seg);
    }

    print_acpi_net_resource_windows();
    if (has_wwan_hint && bi && bi->acpi_rsdp)
    {
        uint64_t sta_ret = 0;
        uint64_t ret = 0;
        acpi_probe_net_exec_context_reset();
        {
            uint64_t reg_args[2];
            reg_args[0] = 0x08u;
            reg_args[1] = 1u;
            terminal_print("[K:PCI] ACPI pre-kick GIO0._REG(0x08,1)");
            (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "GIO0", "_REG", 2u, reg_args, 0);
        }

        terminal_print("[K:PCI] ACPI pre-kick SDC2._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_STA", &ret);
        terminal_print("[K:PCI] SDC2._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick SDC2._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_PS0", 0);
        terminal_print("[K:PCI] ACPI pre-kick SDC2._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "SDC2", "_INI", 0);

        terminal_print("[K:PCI] ACPI pre-kick QPPX._RST");
        for (uint64_t rst_arg = 0u; rst_arg <= 3u; ++rst_arg)
        {
            uint64_t args[1];
            args[0] = rst_arg;
            terminal_print("[K:PCI] QPPX._RST arg=");
            terminal_print_inline_hex64(rst_arg);
            (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "QPPX", "_RST", 1u, args, 0);
            short_delay();
        }
        terminal_print("[K:PCI] ACPI pre-kick QPPX._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "QPPX", "_STA", &sta_ret);
        terminal_print("[K:PCI] QPPX._STA ret=");
        terminal_print_inline_hex64(sta_ret);

        terminal_print("[K:PCI] ACPI pre-kick PCI5._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_STA", &ret);
        terminal_print("[K:PCI] PCI5._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick PCI5._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_PS0", 0);
        terminal_print("[K:PCI] ACPI pre-kick PCI5._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_INI", 0);

        terminal_print("[K:PCI] ACPI pre-kick WWAN._STA");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_STA", &ret);
        terminal_print("[K:PCI] WWAN._STA ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI pre-kick WWAN._PS0");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_PS0", 0);
        terminal_print("[K:PCI] ACPI pre-kick WWAN._INI");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_INI", 0);

        for (uint32_t settle = 0; settle < 8u; ++settle)
        {
            terminal_print("[K:PCI] settle poll iter=");
            terminal_print_inline_hex64(settle);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "QPPX", "_STA", &ret);
            terminal_print("[K:PCI] settle QPPX._STA=");
            terminal_print_inline_hex64(ret);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_STA", &ret);
            terminal_print("[K:PCI] settle PCI5._STA=");
            terminal_print_inline_hex64(ret);

            (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "WWAN", "_STA", &ret);
            terminal_print("[K:PCI] settle WWAN._STA=");
            terminal_print_inline_hex64(ret);

            short_delay();
            short_delay();
        }

        probe_pci5_mmio_windows(bi);
    }
    if (!has_wlan_hint && has_wwan_hint)
    {
        terminal_print("[K:PCI] defer SDC2 SDIO bootstrap: ACPI points to WWAN/MHI path and has no WLAN marker");
    }
    else
    {
        sdc2_maps = map_sdio_bootstrap_windows(bi);
    }

    if (nic_hits == 0u && sdc2_maps == 0u)
    {
        terminal_print("[K:PCI] fallback: run safe SDC2 SDIO bootstrap (no NIC path found)");
        sdc2_maps = map_sdio_bootstrap_windows(bi);
    }

    terminal_print("[K:PCI] SDC2 map attempts: ");
    terminal_print_inline_hex64(sdc2_maps);
    terminal_print("[K:PCI] NIC hits total: ");
    terminal_print_inline_hex64(nic_hits);
    return;
#else
    const int probe_level = 3; /* 0=plan only, 1=map only, 2=tiny read, 3=bounded full scan */
    const uint32_t max_map_attempts = 2u;
    const uint32_t max_probe_buses_per_segment = 2u;
    const uint32_t max_visible_logs = 24u;
    uint32_t map_attempts = 0;
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t ecam_count;
    uint32_t nic_hits = 0;
    uint32_t visible_fns = 0;
    uint32_t visible_logs = 0;
    uint16_t preferred_seg = 0;
    int have_preferred_seg = 0;

    if (!bi || !bi->acpi_rsdp)
    {
        terminal_print("[K:PCI] no boot info / no RSDP");
        return;
    }

    ecam_count = acpi_pci_get_ecams_from_rsdp(bi->acpi_rsdp, ecams, DIHOS_PCI_ECAM_MAX);
    terminal_print("[K:PCI] kernel MCFG NIC probe");
    terminal_print("[K:PCI] ECAM count: ");
    terminal_print_inline_hex64(ecam_count);

    have_preferred_seg = choose_preferred_segment_from_net_resources(&preferred_seg);
    if (have_preferred_seg)
    {
        terminal_print("[K:PCI] preferred segment from ACPI net resources: ");
        terminal_print_inline_hex64(preferred_seg);
    }

    for (uint32_t i = 0; i < ecam_count; ++i)
    {
        uint64_t total_buses = 0;
        uint64_t probe_buses = 0;
        uint64_t map_base = ecams[i].base;
        uint64_t map_size;
        int prefer_this_segment = 0;
        int map_rc;

        if (ecams[i].end_bus < ecams[i].start_bus)
            continue;

        total_buses = (uint64_t)ecams[i].end_bus - (uint64_t)ecams[i].start_bus + 1ull;
        probe_buses = total_buses;
        if (probe_buses > max_probe_buses_per_segment)
            probe_buses = max_probe_buses_per_segment;
        if (probe_buses == 0u)
            continue;
        map_size = probe_buses << 20; /* 1 MiB per bus */
        prefer_this_segment = (have_preferred_seg && ecams[i].segment == preferred_seg);

        terminal_print("[K:PCI] seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" buses=");
        terminal_print_inline_hex64(ecams[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64((uint64_t)(ecams[i].start_bus + (uint8_t)(probe_buses - 1u)));
        terminal_print(" base=");
        terminal_print_inline_hex64(map_base);
        terminal_print(" map_size=");
        terminal_print_inline_hex64(map_size);
        if (prefer_this_segment)
            terminal_print(" [preferred]");

        if (ecam_overlaps_efi_ram(bi, map_base, map_size))
        {
            terminal_print("[K:PCI] skip: overlaps EFI RAM");
            continue;
        }

        if (map_base < 0x0000000100000000ull)
        {
            if (!prefer_this_segment)
            {
                terminal_print("[K:PCI] skip: sub-4G segment (not preferred)");
                continue;
            }
            terminal_print("[K:PCI] allow: sub-4G preferred segment");
        }

        if (map_attempts >= max_map_attempts && !prefer_this_segment)
        {
            terminal_print("[K:PCI] skip: staged map attempt limit reached");
            continue;
        }

        if (probe_level <= 0)
        {
            terminal_print("[K:PCI] plan-only mode: mapping/read disabled");
            continue;
        }

        map_rc = mmio_map_device_identity(map_base, map_size);
        if (map_rc != 0)
        {
            terminal_print("[K:PCI] map failed rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)map_rc);
            continue;
        }
        map_attempts++;

        if (probe_level == 1)
        {
            terminal_print("[K:PCI] map-only mode: scan disabled");
            continue;
        }

        if (probe_level == 2)
        {
            for (uint32_t b = 0; b < probe_buses; ++b)
            {
                uint8_t bus = (uint8_t)(ecams[i].start_bus + (uint8_t)b);
                for (uint8_t dev = 0; dev < 8u; ++dev)
                {
                    uint64_t cfg = pci_ecam_config_phys(&ecams[i], bus, dev, 0, 0);
                    uint32_t id = cfg ? mmio_read32(cfg + 0x00) : 0xFFFFFFFFu;
                    terminal_print("[K:PCI] tiny-read bdf=");
                    terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8));
                    terminal_print(" id=");
                    terminal_print_inline_hex64(id);
                }
            }
            continue;
        }

        for (uint32_t b = 0; b < probe_buses; ++b)
        {
            uint8_t bus = (uint8_t)(ecams[i].start_bus + (uint8_t)b);
            for (uint8_t dev = 0; dev < 32u; ++dev)
            {
                for (uint8_t fn = 0; fn < 8u; ++fn)
                {
                    uint64_t cfg = pci_ecam_config_phys(&ecams[i], bus, dev, fn, 0);
                    uint32_t id;
                    uint32_t cc;
                    uint8_t class_code;
                    uint8_t subclass;
                    uint8_t prog_if;

                    if (!cfg)
                        continue;

                    id = mmio_read32(cfg + 0x00);
                    if (id == 0xFFFFFFFFu || id == 0x00000000u)
                    {
                        if (fn == 0)
                            break;
                        continue;
                    }

                    cc = mmio_read32(cfg + 0x08);
                    class_code = (uint8_t)(cc >> 24);
                    subclass = (uint8_t)(cc >> 16);
                    prog_if = (uint8_t)(cc >> 8);
                    visible_fns++;

                    if (visible_logs < max_visible_logs)
                    {
                        terminal_print("[K:PCI] fn seg=");
                        terminal_print_inline_hex64(ecams[i].segment);
                        terminal_print(" bdf=");
                        terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
                        terminal_print(" vid=");
                        terminal_print_inline_hex64((uint16_t)(id & 0xFFFFu));
                        terminal_print(" did=");
                        terminal_print_inline_hex64((uint16_t)((id >> 16) & 0xFFFFu));
                        terminal_print(" cls=");
                        terminal_print_inline_hex64(class_code);
                        terminal_print(" sub=");
                        terminal_print_inline_hex64(subclass);
                        terminal_print(" if=");
                        terminal_print_inline_hex64(prog_if);
                        visible_logs++;
                    }

                    if (is_likely_network_class(class_code))
                    {
                        nic_hits++;
                        terminal_print("[K:PCI] NET seg=");
                        terminal_print_inline_hex64(ecams[i].segment);
                        terminal_print(" bdf=");
                        terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
                        terminal_print(" vid=");
                        terminal_print_inline_hex64((uint16_t)(id & 0xFFFFu));
                        terminal_print(" did=");
                        terminal_print_inline_hex64((uint16_t)((id >> 16) & 0xFFFFu));
                        terminal_print(" cls=");
                        terminal_print_inline_hex64(class_code);
                        terminal_print(" sub=");
                        terminal_print_inline_hex64(subclass);
                        terminal_print(" if=");
                        terminal_print_inline_hex64(prog_if);
                    }
                }
            }
        }
    }

    terminal_print("[K:PCI] visible function count: ");
    terminal_print_inline_hex64(visible_fns);
    terminal_print("[K:PCI] NIC hits total: ");
    terminal_print_inline_hex64(nic_hits);
#endif
}
