#include "pci/pci_kernel_nic_probe.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "pci/pci_ecam_lookup.h"
#include "memory/mmio_map.h"
#include "memory/pmem.h"
#include "gpio/gpio.h"
#include "terminal/terminal_api.h"
#include "asm/asm.h"
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

#define QCOM_FC7800_SEGMENT 4u
#define QCOM_FC7800_BUS 1u
#define QCOM_FC7800_DEV 0u
#define QCOM_FC7800_FN 0u
#define QCOM_FC7800_VENDOR 0x17CBu
#define QCOM_FC7800_DEVICE 0x1107u
#define QCOM_FC7800_ROOTPORT_DEVICE 0x0111u
#define QWIFI_BAR0_PROBE_BYTES 0x200000ull
#define ATH12K_WINDOW_ENABLE_BIT 0x40000000u
#define ATH12K_WINDOW_REG_ADDRESS 0x310Cu
#define ATH12K_WINDOW_START 0x80000u
#define ATH12K_WINDOW_RANGE_MASK 0x7FFFFu
#define ATH12K_TCSR_SOC_HW_VERSION 0x01B00000u
#define ATH12K_TCSR_SOC_HW_VERSION_MAJOR_MASK 0x00000F00u
#define ATH12K_TCSR_SOC_HW_VERSION_MINOR_MASK 0x000000F0u
#define ATH12K_MHIREGLEN 0x00u
#define ATH12K_MHIVER 0x08u
#define ATH12K_MHICFG 0x10u
#define ATH12K_CHDBOFF 0x18u
#define ATH12K_ERDBOFF 0x20u
#define ATH12K_BHIOFF 0x28u
#define ATH12K_BHIEOFF 0x2Cu
#define ATH12K_DEBUGOFF 0x30u
#define ATH12K_MHICTRL 0x38u
#define ATH12K_MHISTATUS 0x48u
#define ATH12K_CCABAP_LOWER 0x58u
#define ATH12K_CCABAP_HIGHER 0x5Cu
#define ATH12K_ECABAP_LOWER 0x60u
#define ATH12K_ECABAP_HIGHER 0x64u
#define ATH12K_CRCBAP_LOWER 0x68u
#define ATH12K_CRCBAP_HIGHER 0x6Cu
#define ATH12K_CRDB_LOWER 0x70u
#define ATH12K_CRDB_HIGHER 0x74u
#define ATH12K_MHICTRLBASE_LOWER 0x80u
#define ATH12K_MHICTRLBASE_HIGHER 0x84u
#define ATH12K_MHICTRLLIMIT_LOWER 0x88u
#define ATH12K_MHICTRLLIMIT_HIGHER 0x8Cu
#define ATH12K_MHIDATABASE_LOWER 0x98u
#define ATH12K_MHIDATABASE_HIGHER 0x9Cu
#define ATH12K_MHIDATALIMIT_LOWER 0xA0u
#define ATH12K_MHIDATALIMIT_HIGHER 0xA4u
#define ATH12K_MHICFG_NHWER_MASK 0xFF000000u
#define ATH12K_MHICFG_NER_MASK 0x00FF0000u
#define ATH12K_MHICFG_NHWCH_MASK 0x0000FF00u
#define ATH12K_MHICFG_NCH_MASK 0x000000FFu
#define ATH12K_MHICTRL_MHISTATE_MASK 0x0000FF00u
#define ATH12K_MHICTRL_RESET_MASK 0x00000002u
#define ATH12K_MHISTATUS_MHISTATE_MASK 0x0000FF00u
#define ATH12K_MHISTATUS_SYSERR_MASK 0x00000004u
#define ATH12K_MHISTATUS_READY_MASK 0x00000001u
#define ATH12K_BHI_VERSION_MINOR 0x00u
#define ATH12K_BHI_VERSION_MAJOR 0x04u
#define ATH12K_BHI_IMGADDR_LOW 0x08u
#define ATH12K_BHI_IMGADDR_HIGH 0x0Cu
#define ATH12K_BHI_IMGSIZE 0x10u
#define ATH12K_BHI_IMGTXDB 0x18u
#define ATH12K_BHI_EXECENV 0x28u
#define ATH12K_BHI_STATUS 0x2Cu
#define ATH12K_BHI_ERRCODE 0x30u
#define ATH12K_BHI_ERRDBG1 0x34u
#define ATH12K_BHI_ERRDBG2 0x38u
#define ATH12K_BHI_ERRDBG3 0x3Cu
#define ATH12K_BHI_SERIALNU 0x40u
#define ATH12K_BHIE_MSMSOCID 0x00u
#define ATH12K_BHIE_TXVECADDR_LOW 0x2Cu
#define ATH12K_BHIE_TXVECADDR_HIGH 0x30u
#define ATH12K_BHIE_TXVECSIZE 0x34u
#define ATH12K_BHIE_TXVECDB 0x3Cu
#define ATH12K_BHIE_TXVECSTATUS 0x44u
#define ATH12K_BHIE_RXVECSTATUS 0x78u
#define ATH12K_PCIE_TXVECDB 0x360u
#define ATH12K_PCIE_TXVECSTATUS 0x368u
#define ATH12K_PCIE_RXVECDB 0x394u
#define ATH12K_PCIE_RXVECSTATUS 0x39Cu
#define QWIFI_MHI_MAX_CHAN 128u
#define QWIFI_MHI_EVENT_RINGS 1u
#define QWIFI_MHI_CMD_RINGS 1u
#define QWIFI_MHI_RING_BYTES 4096u
#define QWIFI_MHI_EVENT_ELEMENT_BYTES 16u
#define QWIFI_MHI_SBL_BYTES 0x80000u
#define QWIFI_BHIE_SEG_BYTES 0x80000u
#define QWIFI_BHIE_MAX_SEGMENTS 32u
#define QWIFI_MHI_ER_TYPE_VALID 1u
#define QWIFI_MHI_STATE_M0 2u
#define QWIFI_BHI_STATUS_SUCCESS 2u
#define QWIFI_BHI_STATUS_ERROR 3u
#define QWIFI_BHIE_STATUS_XFER_COMPL 2u
#define QWIFI_BHIE_STATUS_ERROR 3u

typedef struct __attribute__((packed))
{
    uint32_t chcfg;
    uint32_t chtype;
    uint32_t erindex;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_chan_ctxt;

typedef struct __attribute__((packed))
{
    uint32_t intmod;
    uint32_t ertype;
    uint32_t msivec;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_event_ctxt;

typedef struct __attribute__((packed))
{
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint64_t rbase;
    uint64_t rlen;
    uint64_t rp;
    uint64_t wp;
} qwifi_mhi_cmd_ctxt;

typedef struct __attribute__((packed))
{
    uint64_t dma_addr;
    uint64_t size;
} qwifi_bhi_vec_entry;

static int pci_probe_config_read32(uint64_t addr, uint32_t *out_value)
{
    if (!addr || !out_value)
        return -1;

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return asm_aa64_try_read32(addr, out_value);
#else
    *out_value = mmio_read32(addr);
    return 0;
#endif
}

static int pci_probe_mmio_write32(uint64_t addr, uint32_t value)
{
    if (!addr)
        return -1;

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return asm_aa64_try_write32(addr, value);
#else
    mmio_write32(addr, value);
    return 0;
#endif
}

static uint64_t qwifi_bar0_base_from_config(uint32_t bar0, uint32_t bar1)
{
    if (bar0 & 0x1u)
        return 0u;
    if ((bar0 & 0x6u) == 0x4u)
        return (((uint64_t)bar1) << 32) | (uint64_t)(bar0 & ~0xFull);
    return (uint64_t)(bar0 & ~0xFull);
}

static void qwifi_zero(void *ptr, uint64_t bytes)
{
    uint8_t *p = (uint8_t *)ptr;

    if (!p)
        return;

    for (uint64_t i = 0; i < bytes; ++i)
        p[i] = 0u;
}

static uint64_t qwifi_pages_for(uint64_t bytes)
{
    return (bytes + 4095ull) >> 12;
}

static int qwifi_mhi_write32(uint64_t bar0_base, uint32_t off, uint32_t value)
{
    int rc = pci_probe_mmio_write32(bar0_base + off, value);
    if (rc != 0)
    {
        terminal_print("[K:QWIFI] MHI write failed off=");
        terminal_print_inline_hex64(off);
        terminal_print(" rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();
    }
    return rc;
}

static int qwifi_mhi_write64_pair(uint64_t bar0_base, uint32_t lower_off, uint32_t higher_off, uint64_t value)
{
    int rc;

    rc = qwifi_mhi_write32(bar0_base, higher_off, (uint32_t)(value >> 32));
    if (rc != 0)
        return rc;
    return qwifi_mhi_write32(bar0_base, lower_off, (uint32_t)(value & 0xFFFFFFFFu));
}

static int qwifi_get_fw_blob(const boot_info *bi, uint32_t kind, uint64_t *base_phys, uint64_t *size_bytes)
{
    uint32_t count;

    if (!bi || !base_phys || !size_bytes)
        return 0;

    count = bi->wifi_fw_count;
    if (count > BOOTINFO_WIFI_FW_MAX)
        count = BOOTINFO_WIFI_FW_MAX;

    for (uint32_t i = 0; i < count; ++i)
    {
        if (bi->wifi_fw[i].kind != kind)
            continue;
        if (!bi->wifi_fw[i].base_phys || !bi->wifi_fw[i].size_bytes)
            continue;

        *base_phys = bi->wifi_fw[i].base_phys;
        *size_bytes = bi->wifi_fw[i].size_bytes;
        return 1;
    }

    return 0;
}

static void qwifi_copy_from_phys(void *dst, uint64_t src_phys, uint64_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)pmem_phys_to_virt(src_phys);

    if (!d || !s)
        return;

    for (uint64_t i = 0; i < bytes; ++i)
        d[i] = s[i];
}

static uint32_t qwifi_bhi_status_code(uint32_t status)
{
    return (status >> 30) & 0x3u;
}

static int qwifi_bhi_try_load_sbl(uint64_t bar0_base, uint32_t bhioff, const boot_info *bi)
{
    static int attempted = 0;
    uint64_t amss_phys = 0;
    uint64_t amss_size = 0;
    uint64_t sbl_size;
    void *sbl_buf;
    uint64_t sbl_phys;
    uint32_t status = 0;
    uint32_t status_code = 0;
    uint32_t execenv = 0;
    uint32_t errcode = 0;
    uint32_t errdbg1 = 0;
    uint32_t errdbg2 = 0;
    uint32_t errdbg3 = 0;
    uint32_t poll;
    int rc = 0;

    if (attempted)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: already attempted");
        terminal_flush_log();
        return 0;
    }
    attempted = 1;

    if (!bar0_base || !bhioff || bhioff >= 0x1000u)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: invalid BHI offset");
        terminal_flush_log();
        return 0;
    }

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_AMSS, &amss_phys, &amss_size))
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: AMSS firmware missing");
        terminal_flush_log();
        return 0;
    }

    sbl_size = amss_size;
    if (sbl_size > QWIFI_MHI_SBL_BYTES)
        sbl_size = QWIFI_MHI_SBL_BYTES;
    if (!sbl_size || sbl_size > 0xFFFFFFFFull)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: invalid AMSS size=");
        terminal_print_inline_hex64(amss_size);
        terminal_flush_log();
        return 0;
    }

    sbl_buf = pmem_alloc_pages_lowdma(qwifi_pages_for(sbl_size));
    if (!sbl_buf)
    {
        terminal_print("[K:QWIFI] BHI SBL load skipped: low-DMA alloc failed bytes=");
        terminal_print_inline_hex64(sbl_size);
        terminal_flush_log();
        return 0;
    }

    qwifi_copy_from_phys(sbl_buf, amss_phys, sbl_size);
    asm_dma_clean_range(sbl_buf, sbl_size);
    sbl_phys = pmem_virt_to_phys(sbl_buf);

    terminal_print("[K:QWIFI] BHI SBL load begin fw_pa=");
    terminal_print_inline_hex64(amss_phys);
    terminal_print(" fw_size=");
    terminal_print_inline_hex64(amss_size);
    terminal_print(" sbl_pa=");
    terminal_print_inline_hex64(sbl_phys);
    terminal_print(" sbl_size=");
    terminal_print_inline_hex64(sbl_size);
    terminal_flush_log();

    rc |= qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_STATUS, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base,
                                 bhioff + ATH12K_BHI_IMGADDR_LOW,
                                 bhioff + ATH12K_BHI_IMGADDR_HIGH,
                                 sbl_phys);
    rc |= qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_IMGSIZE, (uint32_t)sbl_size);

    terminal_print("[K:QWIFI] BHI SBL programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    asm_mmio_barrier();
    rc = qwifi_mhi_write32(bar0_base, bhioff + ATH12K_BHI_IMGTXDB, 1u);
    terminal_print("[K:QWIFI] BHI SBL doorbell rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;
    asm_mmio_barrier();

    for (poll = 0; poll < 4000000u; ++poll)
    {
        rc = pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_STATUS, &status);
        if (rc != 0)
            break;
        status_code = qwifi_bhi_status_code(status);
        if (status_code != 0u)
            break;
        asm_relax();
    }

    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRCODE, &errcode);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG1, &errdbg1);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG2, &errdbg2);
    (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_ERRDBG3, &errdbg3);

    terminal_print("[K:QWIFI] BHI SBL result rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" status=");
    terminal_print_inline_hex64(status);
    terminal_print(" code=");
    terminal_print_inline_hex64(status_code);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_print(" err=");
    terminal_print_inline_hex64(errcode);
    terminal_print(" dbg1=");
    terminal_print_inline_hex64(errdbg1);
    terminal_print(" dbg2=");
    terminal_print_inline_hex64(errdbg2);
    terminal_print(" dbg3=");
    terminal_print_inline_hex64(errdbg3);
    if (status_code == QWIFI_BHI_STATUS_SUCCESS)
        terminal_print(" [SBL accepted]");
    else if (status_code == QWIFI_BHI_STATUS_ERROR)
        terminal_print(" [SBL error]");
    else if (rc == 0)
        terminal_print(" [SBL timeout]");
    terminal_flush_log();

    return status_code == QWIFI_BHI_STATUS_SUCCESS;
}

static void qwifi_dump_pci_caps(uint64_t cfg, uint32_t cap_ptr_raw)
{
    uint32_t ptr = cap_ptr_raw & 0xFFu;
    uint32_t seen = 0;

    if (ptr == 0u)
    {
        terminal_print("[K:QWIFI] no PCI capability list");
        terminal_flush_log();
        return;
    }

    terminal_print("[K:QWIFI] PCI capability list start=");
    terminal_print_inline_hex64(ptr);
    terminal_flush_log();

    while (ptr >= 0x40u && ptr < 0x100u && seen < 16u)
    {
        uint32_t dword0 = 0;
        uint32_t dword1 = 0;
        uint32_t dword2 = 0;
        uint32_t cap_id;
        uint32_t next;
        int rc;

        rc = pci_probe_config_read32(cfg + ptr, &dword0);
        if (rc != 0)
        {
            terminal_print("[K:QWIFI] PCI cap read failed ptr=");
            terminal_print_inline_hex64(ptr);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_flush_log();
            return;
        }

        cap_id = dword0 & 0xFFu;
        next = (dword0 >> 8) & 0xFFu;

        terminal_print("[K:QWIFI] PCI cap ptr=");
        terminal_print_inline_hex64(ptr);
        terminal_print(" id=");
        terminal_print_inline_hex64(cap_id);
        terminal_print(" next=");
        terminal_print_inline_hex64(next);
        terminal_print(" d0=");
        terminal_print_inline_hex64(dword0);

        if (cap_id == 0x05u) /* MSI */
        {
            uint32_t msgctl = (dword0 >> 16) & 0xFFFFu;
            terminal_print(" MSI ctl=");
            terminal_print_inline_hex64(msgctl);
        }
        else if (cap_id == 0x10u) /* PCI Express */
        {
            (void)pci_probe_config_read32(cfg + ptr + 0x04u, &dword1);
            terminal_print(" PCIE cap=");
            terminal_print_inline_hex64((dword0 >> 16) & 0xFFFFu);
            terminal_print(" devcap=");
            terminal_print_inline_hex64(dword1);
        }
        else if (cap_id == 0x11u) /* MSI-X */
        {
            (void)pci_probe_config_read32(cfg + ptr + 0x04u, &dword1);
            (void)pci_probe_config_read32(cfg + ptr + 0x08u, &dword2);
            terminal_print(" MSIX ctl=");
            terminal_print_inline_hex64((dword0 >> 16) & 0xFFFFu);
            terminal_print(" table=");
            terminal_print_inline_hex64(dword1);
            terminal_print(" pba=");
            terminal_print_inline_hex64(dword2);
        }

        terminal_flush_log();

        if (next == 0u || next == ptr)
            break;
        ptr = next;
        seen++;
    }
}

static int qwifi_ath12k_read32(uint64_t bar0_base, uint32_t offset, uint32_t *out_value)
{
    uint32_t window;
    uint32_t readback = 0;
    uint64_t read_addr;
    int rc;

    if (!bar0_base || !out_value)
        return -1;

    if (offset < ATH12K_WINDOW_START)
        return pci_probe_config_read32(bar0_base + offset, out_value);

    window = (offset >> 19) & 0x3Fu;
    rc = pci_probe_mmio_write32(bar0_base + ATH12K_WINDOW_REG_ADDRESS,
                                ATH12K_WINDOW_ENABLE_BIT | window);
    terminal_print("[K:QWIFI] ath12k window select off=");
    terminal_print_inline_hex64(offset);
    terminal_print(" window=");
    terminal_print_inline_hex64(window);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return rc;

    (void)pci_probe_config_read32(bar0_base + ATH12K_WINDOW_REG_ADDRESS, &readback);
    terminal_print("[K:QWIFI] ath12k window readback=");
    terminal_print_inline_hex64(readback);
    terminal_flush_log();

    read_addr = bar0_base + ATH12K_WINDOW_START + (uint64_t)(offset & ATH12K_WINDOW_RANGE_MASK);
    return pci_probe_config_read32(read_addr, out_value);
}

static void qwifi_mhi_program_minimal_contexts(uint64_t bar0_base, uint32_t mhicfg)
{
    static int programmed = 0;
    uint32_t nch = mhicfg & ATH12K_MHICFG_NCH_MASK;
    uint32_t chan_count = nch;
    uint64_t chan_bytes;
    uint64_t event_bytes = sizeof(qwifi_mhi_event_ctxt) * QWIFI_MHI_EVENT_RINGS;
    uint64_t cmd_bytes = sizeof(qwifi_mhi_cmd_ctxt) * QWIFI_MHI_CMD_RINGS;
    qwifi_mhi_chan_ctxt *chan_ctxt;
    qwifi_mhi_event_ctxt *event_ctxt;
    qwifi_mhi_cmd_ctxt *cmd_ctxt;
    void *event_ring;
    void *cmd_ring;
    uint64_t chan_pa;
    uint64_t event_pa;
    uint64_t cmd_pa;
    uint64_t event_ring_pa;
    uint64_t cmd_ring_pa;
    uint32_t readback = 0;
    uint32_t new_mhicfg;
    int rc = 0;

    if (programmed)
    {
        terminal_print("[K:QWIFI] MHI minimal context reprogram after state change");
        terminal_flush_log();
    }

    if (chan_count == 0u || chan_count > QWIFI_MHI_MAX_CHAN)
        chan_count = QWIFI_MHI_MAX_CHAN;
    chan_bytes = sizeof(qwifi_mhi_chan_ctxt) * (uint64_t)chan_count;

    chan_ctxt = (qwifi_mhi_chan_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(chan_bytes));
    event_ctxt = (qwifi_mhi_event_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(event_bytes));
    cmd_ctxt = (qwifi_mhi_cmd_ctxt *)pmem_alloc_pages_lowdma(qwifi_pages_for(cmd_bytes));
    event_ring = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_RING_BYTES));
    cmd_ring = pmem_alloc_pages_lowdma(qwifi_pages_for(QWIFI_MHI_RING_BYTES));

    if (!chan_ctxt || !event_ctxt || !cmd_ctxt || !event_ring || !cmd_ring)
    {
        terminal_print("[K:QWIFI] MHI minimal context alloc failed");
        terminal_flush_log();
        return;
    }

    qwifi_zero(chan_ctxt, chan_bytes);
    qwifi_zero(event_ctxt, event_bytes);
    qwifi_zero(cmd_ctxt, cmd_bytes);
    qwifi_zero(event_ring, QWIFI_MHI_RING_BYTES);
    qwifi_zero(cmd_ring, QWIFI_MHI_RING_BYTES);

    chan_pa = pmem_virt_to_phys(chan_ctxt);
    event_pa = pmem_virt_to_phys(event_ctxt);
    cmd_pa = pmem_virt_to_phys(cmd_ctxt);
    event_ring_pa = pmem_virt_to_phys(event_ring);
    cmd_ring_pa = pmem_virt_to_phys(cmd_ring);

    event_ctxt[0].ertype = QWIFI_MHI_ER_TYPE_VALID;
    event_ctxt[0].msivec = 0u;
    event_ctxt[0].rbase = event_ring_pa;
    event_ctxt[0].rlen = QWIFI_MHI_RING_BYTES;
    event_ctxt[0].rp = event_ring_pa;
    event_ctxt[0].wp = event_ring_pa + QWIFI_MHI_RING_BYTES - QWIFI_MHI_EVENT_ELEMENT_BYTES;

    cmd_ctxt[0].rbase = cmd_ring_pa;
    cmd_ctxt[0].rlen = QWIFI_MHI_RING_BYTES;
    cmd_ctxt[0].rp = cmd_ring_pa;
    cmd_ctxt[0].wp = cmd_ring_pa;

    asm_dma_clean_range(chan_ctxt, chan_bytes);
    asm_dma_clean_range(event_ctxt, event_bytes);
    asm_dma_clean_range(cmd_ctxt, cmd_bytes);
    asm_dma_clean_range(event_ring, QWIFI_MHI_RING_BYTES);
    asm_dma_clean_range(cmd_ring, QWIFI_MHI_RING_BYTES);

    terminal_print("[K:QWIFI] MHI ctx alloc chan_pa=");
    terminal_print_inline_hex64(chan_pa);
    terminal_print(" event_pa=");
    terminal_print_inline_hex64(event_pa);
    terminal_print(" cmd_pa=");
    terminal_print_inline_hex64(cmd_pa);
    terminal_print(" evring_pa=");
    terminal_print_inline_hex64(event_ring_pa);
    terminal_print(" cmdring_pa=");
    terminal_print_inline_hex64(cmd_ring_pa);
    terminal_flush_log();

    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_CCABAP_LOWER, ATH12K_CCABAP_HIGHER, chan_pa);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_ECABAP_LOWER, ATH12K_ECABAP_HIGHER, event_pa);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_CRCBAP_LOWER, ATH12K_CRCBAP_HIGHER, cmd_pa);

    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHICTRLBASE_LOWER, ATH12K_MHICTRLBASE_HIGHER, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHICTRLLIMIT_LOWER, ATH12K_MHICTRLLIMIT_HIGHER, 0xFFFFFFFFull);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHIDATABASE_LOWER, ATH12K_MHIDATABASE_HIGHER, 0u);
    rc |= qwifi_mhi_write64_pair(bar0_base, ATH12K_MHIDATALIMIT_LOWER, ATH12K_MHIDATALIMIT_HIGHER, 0xFFFFFFFFull);

    new_mhicfg = (mhicfg & ~(ATH12K_MHICFG_NER_MASK | ATH12K_MHICFG_NHWER_MASK)) |
                 (QWIFI_MHI_EVENT_RINGS << 16);
    rc |= qwifi_mhi_write32(bar0_base, ATH12K_MHICFG, new_mhicfg);

    (void)pci_probe_config_read32(bar0_base + ATH12K_CCABAP_LOWER, &readback);
    terminal_print("[K:QWIFI] MHI ctx programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" ccabap_lo=");
    terminal_print_inline_hex64(readback);
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICFG, &readback);
    terminal_print(" mhicfg=");
    terminal_print_inline_hex64(readback);
    terminal_flush_log();

    if (rc == 0)
        programmed = 1;
}

static int qwifi_mhi_wait_ready_and_enter_m0(uint64_t bar0_base,
                                             uint32_t bhioff,
                                             uint32_t mhicfg_hint)
{
    uint32_t mhictrl = 0;
    uint32_t mhistatus = 0;
    uint32_t mhicfg = 0;
    uint32_t execenv = 0;
    uint32_t poll;
    uint32_t ready = 0;
    uint32_t reset = 1;
    int rc = 0;

    for (poll = 0; poll < 4000000u; ++poll)
    {
        (void)pci_probe_config_read32(bar0_base + ATH12K_MHICTRL, &mhictrl);
        (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);
        if (bhioff != 0u && bhioff < 0x1000u)
            (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);

        reset = (mhictrl & ATH12K_MHICTRL_RESET_MASK) ? 1u : 0u;
        ready = (mhistatus & ATH12K_MHISTATUS_READY_MASK) ? 1u : 0u;
        if (!reset && ready)
            break;
        asm_relax();
    }

    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICFG, &mhicfg);
    terminal_print("[K:QWIFI] post-SBL READY poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" reset=");
    terminal_print_inline_hex64(reset);
    terminal_print(" ready=");
    terminal_print_inline_hex64(ready);
    terminal_print(" mhictrl=");
    terminal_print_inline_hex64(mhictrl);
    terminal_print(" mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_print(" mhicfg=");
    terminal_print_inline_hex64(mhicfg);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_flush_log();

    if (reset || !ready)
        return 0;

    qwifi_mhi_program_minimal_contexts(bar0_base, mhicfg ? mhicfg : mhicfg_hint);

    rc = qwifi_mhi_write32(bar0_base, ATH12K_MHICTRL, QWIFI_MHI_STATE_M0 << 8);
    asm_mmio_barrier();
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHICTRL, &mhictrl);
    (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);

    terminal_print("[K:QWIFI] MHI enter M0 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" mhictrl=");
    terminal_print_inline_hex64(mhictrl);
    terminal_print(" mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_flush_log();

    return rc == 0;
}

static int qwifi_bhie_try_load_amss(uint64_t bar0_base, uint32_t bhioff, uint32_t bhieoff, const boot_info *bi)
{
    static int attempted = 0;
    uint64_t amss_phys = 0;
    uint64_t amss_size = 0;
    uint64_t copied = 0;
    uint32_t seg_count;
    uint64_t vec_bytes;
    qwifi_bhi_vec_entry *vec;
    uint64_t vec_phys;
    uint32_t status = 0;
    uint32_t status_code = 0;
    uint32_t execenv = 0;
    uint32_t mhistatus = 0;
    uint32_t poll;
    const uint32_t sequence = 2u;
    int rc = 0;

    if (attempted)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: already attempted");
        terminal_flush_log();
        return 0;
    }
    attempted = 1;

    if (!bar0_base || !bhieoff || bhieoff >= 0x1000u)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: invalid BHIE offset");
        terminal_flush_log();
        return 0;
    }

    if (!qwifi_get_fw_blob(bi, BOOTINFO_WIFI_FW_AMSS, &amss_phys, &amss_size))
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: AMSS firmware missing");
        terminal_flush_log();
        return 0;
    }

    seg_count = (uint32_t)((amss_size + QWIFI_BHIE_SEG_BYTES - 1u) / QWIFI_BHIE_SEG_BYTES);
    if (!seg_count || seg_count > QWIFI_BHIE_MAX_SEGMENTS)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: segment count=");
        terminal_print_inline_hex64(seg_count);
        terminal_print(" fw_size=");
        terminal_print_inline_hex64(amss_size);
        terminal_flush_log();
        return 0;
    }

    vec_bytes = sizeof(qwifi_bhi_vec_entry) * (uint64_t)seg_count;
    vec = (qwifi_bhi_vec_entry *)pmem_alloc_pages_lowdma(qwifi_pages_for(vec_bytes));
    if (!vec)
    {
        terminal_print("[K:QWIFI] BHIE AMSS load skipped: vec alloc failed bytes=");
        terminal_print_inline_hex64(vec_bytes);
        terminal_flush_log();
        return 0;
    }
    qwifi_zero(vec, vec_bytes);

    for (uint32_t i = 0; i < seg_count; ++i)
    {
        uint64_t chunk = amss_size - copied;
        void *seg_buf;
        uint64_t seg_phys;

        if (chunk > QWIFI_BHIE_SEG_BYTES)
            chunk = QWIFI_BHIE_SEG_BYTES;

        seg_buf = pmem_alloc_pages_lowdma(qwifi_pages_for(chunk));
        if (!seg_buf)
        {
            terminal_print("[K:QWIFI] BHIE AMSS segment alloc failed i=");
            terminal_print_inline_hex64(i);
            terminal_print(" bytes=");
            terminal_print_inline_hex64(chunk);
            terminal_flush_log();
            return 0;
        }

        qwifi_copy_from_phys(seg_buf, amss_phys + copied, chunk);
        asm_dma_clean_range(seg_buf, chunk);
        seg_phys = pmem_virt_to_phys(seg_buf);
        vec[i].dma_addr = seg_phys;
        vec[i].size = chunk;
        copied += chunk;
    }

    asm_dma_clean_range(vec, vec_bytes);
    vec_phys = pmem_virt_to_phys(vec);

    terminal_print("[K:QWIFI] BHIE AMSS load begin fw_pa=");
    terminal_print_inline_hex64(amss_phys);
    terminal_print(" fw_size=");
    terminal_print_inline_hex64(amss_size);
    terminal_print(" segments=");
    terminal_print_inline_hex64(seg_count);
    terminal_print(" vec_pa=");
    terminal_print_inline_hex64(vec_phys);
    terminal_print(" vec_bytes=");
    terminal_print_inline_hex64(vec_bytes);
    terminal_flush_log();

    rc |= qwifi_mhi_write64_pair(bar0_base,
                                 bhieoff + ATH12K_BHIE_TXVECADDR_LOW,
                                 bhieoff + ATH12K_BHIE_TXVECADDR_HIGH,
                                 vec_phys);
    rc |= qwifi_mhi_write32(bar0_base, bhieoff + ATH12K_BHIE_TXVECSIZE, (uint32_t)vec_bytes);
    terminal_print("[K:QWIFI] BHIE AMSS programmed rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    asm_mmio_barrier();
    rc = qwifi_mhi_write32(bar0_base, bhieoff + ATH12K_BHIE_TXVECDB, sequence);
    terminal_print("[K:QWIFI] BHIE AMSS doorbell rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" seq=");
    terminal_print_inline_hex64(sequence);
    terminal_flush_log();
    if (rc != 0)
        return 0;
    asm_mmio_barrier();

    for (poll = 0; poll < 8000000u; ++poll)
    {
        rc = pci_probe_config_read32(bar0_base + bhieoff + ATH12K_BHIE_TXVECSTATUS, &status);
        if (rc != 0)
            break;
        status_code = qwifi_bhi_status_code(status);
        if (status_code != 0u)
            break;
        asm_relax();
    }

    terminal_print("[K:QWIFI] BHIE AMSS result rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" poll=");
    terminal_print_inline_hex64(poll);
    terminal_print(" status=");
    terminal_print_inline_hex64(status);
    terminal_print(" code=");
    terminal_print_inline_hex64(status_code);
    terminal_print(" seq_seen=");
    terminal_print_inline_hex64(status & 0x3FFFFFFFu);
    if (status_code == QWIFI_BHIE_STATUS_XFER_COMPL)
        terminal_print(" [AMSS accepted]");
    else if (status_code == QWIFI_BHIE_STATUS_ERROR)
        terminal_print(" [AMSS error]");
    else if (rc == 0)
        terminal_print(" [AMSS timeout]");
    terminal_flush_log();

    if (bhioff != 0u && bhioff < 0x1000u)
    {
        for (poll = 0; poll < 4000000u; ++poll)
        {
            (void)pci_probe_config_read32(bar0_base + bhioff + ATH12K_BHI_EXECENV, &execenv);
            if (execenv > 1u)
                break;
            asm_relax();
        }
    }

    (void)pci_probe_config_read32(bar0_base + ATH12K_MHISTATUS, &mhistatus);
    (void)pci_probe_config_read32(bar0_base + bhieoff + ATH12K_BHIE_TXVECSTATUS, &status);
    terminal_print("[K:QWIFI] post-AMSS mhistatus=");
    terminal_print_inline_hex64(mhistatus);
    terminal_print(" bhie_status=");
    terminal_print_inline_hex64(status);
    terminal_print(" execenv=");
    terminal_print_inline_hex64(execenv);
    terminal_flush_log();

    return status_code == QWIFI_BHIE_STATUS_XFER_COMPL;
}

static void qwifi_dump_ath12k_probe_regs(uint64_t bar0_base, const boot_info *bi)
{
    static const struct
    {
        const char *name;
        uint32_t off;
    } mhi_regs[] = {
        {"MHIREGLEN", ATH12K_MHIREGLEN},
        {"MHIVER", ATH12K_MHIVER},
        {"MHICFG", ATH12K_MHICFG},
        {"CHDBOFF", ATH12K_CHDBOFF},
        {"ERDBOFF", ATH12K_ERDBOFF},
        {"BHIOFF", ATH12K_BHIOFF},
        {"BHIEOFF", ATH12K_BHIEOFF},
        {"DEBUGOFF", ATH12K_DEBUGOFF},
        {"MHICTRL", ATH12K_MHICTRL},
        {"MHISTATUS", ATH12K_MHISTATUS},
        {"PCIE_TXVECDB", ATH12K_PCIE_TXVECDB},
        {"PCIE_TXVECSTATUS", ATH12K_PCIE_TXVECSTATUS},
        {"PCIE_RXVECDB", ATH12K_PCIE_RXVECDB},
        {"PCIE_RXVECSTATUS", ATH12K_PCIE_RXVECSTATUS},
    };
    static const struct
    {
        const char *name;
        uint32_t rel;
    } bhi_regs[] = {
        {"BHI_VERSION_MINOR", ATH12K_BHI_VERSION_MINOR},
        {"BHI_VERSION_MAJOR", ATH12K_BHI_VERSION_MAJOR},
        {"BHI_EXECENV", ATH12K_BHI_EXECENV},
        {"BHI_STATUS", ATH12K_BHI_STATUS},
        {"BHI_ERRCODE", ATH12K_BHI_ERRCODE},
        {"BHI_ERRDBG1", ATH12K_BHI_ERRDBG1},
        {"BHI_ERRDBG2", ATH12K_BHI_ERRDBG2},
        {"BHI_ERRDBG3", ATH12K_BHI_ERRDBG3},
        {"BHI_SERIALNU", ATH12K_BHI_SERIALNU},
    };
    static const struct
    {
        const char *name;
        uint32_t rel;
    } bhie_regs[] = {
        {"BHIE_MSMSOCID", ATH12K_BHIE_MSMSOCID},
        {"BHIE_TXVECSTATUS", ATH12K_BHIE_TXVECSTATUS},
        {"BHIE_RXVECSTATUS", ATH12K_BHIE_RXVECSTATUS},
    };
    uint32_t mhicfg = 0;
    uint32_t mhictrl = 0;
    uint32_t mhistatus = 0;
    uint32_t bhioff = 0;
    uint32_t bhieoff = 0;
    uint32_t bhi_status = 0;
    uint32_t value = 0;
    int sbl_ok = 0;
    int rc;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(mhi_regs) / sizeof(mhi_regs[0])); ++i)
    {
        rc = pci_probe_config_read32(bar0_base + mhi_regs[i].off, &value);
        terminal_print("[K:QWIFI] MHI reg ");
        terminal_print(mhi_regs[i].name);
        terminal_print(" off=");
        terminal_print_inline_hex64(mhi_regs[i].off);
        terminal_print(" rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_print(" value=");
        terminal_print_inline_hex64(value);
        terminal_flush_log();
        if (rc != 0)
            break;

        if (mhi_regs[i].off == ATH12K_MHICFG)
            mhicfg = value;
        else if (mhi_regs[i].off == ATH12K_MHICTRL)
            mhictrl = value;
        else if (mhi_regs[i].off == ATH12K_MHISTATUS)
            mhistatus = value;
        else if (mhi_regs[i].off == ATH12K_BHIOFF)
            bhioff = value;
        else if (mhi_regs[i].off == ATH12K_BHIEOFF)
            bhieoff = value;
    }

    terminal_print("[K:QWIFI] MHI decode nch=");
    terminal_print_inline_hex64(mhicfg & ATH12K_MHICFG_NCH_MASK);
    terminal_print(" nhwch=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NHWCH_MASK) >> 8);
    terminal_print(" ner=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NER_MASK) >> 16);
    terminal_print(" nhwer=");
    terminal_print_inline_hex64((mhicfg & ATH12K_MHICFG_NHWER_MASK) >> 24);
    terminal_print(" ctrl_state=");
    terminal_print_inline_hex64((mhictrl & ATH12K_MHICTRL_MHISTATE_MASK) >> 8);
    terminal_print(" ctrl_reset=");
    terminal_print_inline_hex64((mhictrl & ATH12K_MHICTRL_RESET_MASK) ? 1u : 0u);
    terminal_print(" status_state=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_MHISTATE_MASK) >> 8);
    terminal_print(" ready=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_READY_MASK) ? 1u : 0u);
    terminal_print(" syserr=");
    terminal_print_inline_hex64((mhistatus & ATH12K_MHISTATUS_SYSERR_MASK) ? 1u : 0u);
    terminal_flush_log();

    qwifi_mhi_program_minimal_contexts(bar0_base, mhicfg);

    if (bhioff != 0u && bhioff < 0x1000u)
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(bhi_regs) / sizeof(bhi_regs[0])); ++i)
        {
            rc = pci_probe_config_read32(bar0_base + bhioff + bhi_regs[i].rel, &value);
            terminal_print("[K:QWIFI] BHI reg ");
            terminal_print(bhi_regs[i].name);
            terminal_print(" off=");
            terminal_print_inline_hex64(bhioff + bhi_regs[i].rel);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_print(" value=");
            terminal_print_inline_hex64(value);
            terminal_flush_log();
            if (rc != 0)
                break;
            if (bhi_regs[i].rel == ATH12K_BHI_STATUS)
                bhi_status = value;
        }

        terminal_print("[K:QWIFI] BHI decode status=");
        terminal_print_inline_hex64((bhi_status >> 30) & 0x3u);
        terminal_print(" raw=");
        terminal_print_inline_hex64(bhi_status);
        terminal_flush_log();

        sbl_ok = qwifi_bhi_try_load_sbl(bar0_base, bhioff, bi);
    }

    if (bhieoff != 0u && bhieoff < 0x1000u)
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(bhie_regs) / sizeof(bhie_regs[0])); ++i)
        {
            rc = pci_probe_config_read32(bar0_base + bhieoff + bhie_regs[i].rel, &value);
            terminal_print("[K:QWIFI] BHIE reg ");
            terminal_print(bhie_regs[i].name);
            terminal_print(" off=");
            terminal_print_inline_hex64(bhieoff + bhie_regs[i].rel);
            terminal_print(" rc=");
            terminal_print_inline_hex64((uint64_t)(int64_t)rc);
            terminal_print(" value=");
            terminal_print_inline_hex64(value);
            terminal_flush_log();
            if (rc != 0)
                break;
        }
    }

    if (sbl_ok && bhieoff != 0u && bhieoff < 0x1000u)
    {
        int m0_ok = qwifi_mhi_wait_ready_and_enter_m0(bar0_base, bhioff, mhicfg);
        if (m0_ok)
            (void)qwifi_bhie_try_load_amss(bar0_base, bhioff, bhieoff, bi);
        else
        {
            terminal_print("[K:QWIFI] BHIE AMSS skipped: MHI did not reach READY/M0");
            terminal_flush_log();
        }
    }

    value = 0;
    rc = qwifi_ath12k_read32(bar0_base, ATH12K_TCSR_SOC_HW_VERSION, &value);
    terminal_print("[K:QWIFI] ath12k TCSR_SOC_HW_VERSION rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" value=");
    terminal_print_inline_hex64(value);
    if (rc == 0)
    {
        uint32_t major = (value & ATH12K_TCSR_SOC_HW_VERSION_MAJOR_MASK) >> 8;
        uint32_t minor = (value & ATH12K_TCSR_SOC_HW_VERSION_MINOR_MASK) >> 4;
        terminal_print(" major=");
        terminal_print_inline_hex64(major);
        terminal_print(" minor=");
        terminal_print_inline_hex64(minor);
        if (major == 2u)
            terminal_print(" [WCN7850 hw2.x expected]");
    }
    terminal_flush_log();
}

static void qwifi_acpi_exec0(uint64_t rsdp, const char dev_name4[4], const char method_name4[4])
{
    uint64_t ret = 0;
    int rc;

    terminal_print("[K:QWIFI] ACPI ");
    terminal_print(dev_name4);
    terminal_print(".");
    terminal_print(method_name4);
    terminal_flush_log();

    rc = acpi_probe_net_exec_device_method(rsdp, dev_name4, method_name4, &ret);

    terminal_print("[K:QWIFI] ACPI result ");
    terminal_print(dev_name4);
    terminal_print(".");
    terminal_print(method_name4);
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" ret=");
    terminal_print_inline_hex64(ret);
    terminal_flush_log();
}

static void qwifi_acpi_prekick(const boot_info *bi)
{
    uint64_t reg_args[2];

    if (!bi || !bi->acpi_rsdp)
        return;

    terminal_print("[K:QWIFI] hardcoded ACPI path: _SB.PCI4.RP1_.WLN_");
    terminal_flush_log();

    acpi_probe_net_exec_context_reset();

    reg_args[0] = 0x08u;
    reg_args[1] = 1u;
    terminal_print("[K:QWIFI] ACPI GIO0._REG(0x08,1)");
    terminal_flush_log();
    (void)acpi_probe_net_exec_device_method_args(bi->acpi_rsdp, "GIO0", "_REG", 2u, reg_args, 0);

    qwifi_acpi_exec0(bi->acpi_rsdp, "PCI4", "_STA");
    qwifi_acpi_exec0(bi->acpi_rsdp, "PCI4", "_PS0");
    qwifi_acpi_exec0(bi->acpi_rsdp, "RP1_", "_PS0");
}

static int qwifi_find_ecam(uint64_t rsdp_phys, dihos_pci_ecam *out_ecam)
{
    dihos_pci_ecam ecams[DIHOS_PCI_ECAM_MAX];
    uint32_t count;

    if (!out_ecam || !rsdp_phys)
        return 0;

    count = acpi_pci_get_ecams_from_rsdp(rsdp_phys, ecams, DIHOS_PCI_ECAM_MAX);
    terminal_print("[K:QWIFI] MCFG ECAM count=");
    terminal_print_inline_hex64(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        terminal_print("[K:QWIFI] ECAM seg=");
        terminal_print_inline_hex64(ecams[i].segment);
        terminal_print(" base=");
        terminal_print_inline_hex64(ecams[i].base);
        terminal_print(" buses=");
        terminal_print_inline_hex64(ecams[i].start_bus);
        terminal_print("-");
        terminal_print_inline_hex64(ecams[i].end_bus);

        if (ecams[i].segment == QCOM_FC7800_SEGMENT &&
            QCOM_FC7800_BUS >= ecams[i].start_bus &&
            QCOM_FC7800_BUS <= ecams[i].end_bus)
        {
            *out_ecam = ecams[i];
            terminal_print("[K:QWIFI] selected segment 4 ECAM");
            terminal_flush_log();
            return 1;
        }
    }

    terminal_print("[K:QWIFI] segment 4 ECAM not found");
    terminal_flush_log();
    return 0;
}

static void qwifi_enable_and_dump_bar0(const boot_info *bi,
                                       uint64_t cfg,
                                       uint32_t cmd_status,
                                       uint32_t bar0,
                                       uint32_t bar1)
{
    uint64_t bar0_base = qwifi_bar0_base_from_config(bar0, bar1);
    uint16_t cmd = (uint16_t)(cmd_status & 0xFFFFu);
    uint16_t wanted_cmd = (uint16_t)(cmd | 0x0006u); /* Memory Space + Bus Master */
    uint32_t readback = 0;
    int rc;

    terminal_print("[K:QWIFI] BAR0 decoded base=");
    terminal_print_inline_hex64(bar0_base);
    terminal_print(" raw0=");
    terminal_print_inline_hex64(bar0);
    terminal_print(" raw1=");
    terminal_print_inline_hex64(bar1);
    terminal_flush_log();

    if (!bar0_base)
    {
        terminal_print("[K:QWIFI] BAR0 unavailable; skip MMIO dump");
        terminal_flush_log();
        return;
    }

    if ((cmd & 0x0006u) != 0x0006u)
    {
        terminal_print("[K:QWIFI] enable PCI command MEM|BUSMASTER old=");
        terminal_print_inline_hex64(cmd);
        terminal_print(" new=");
        terminal_print_inline_hex64(wanted_cmd);
        terminal_flush_log();

        rc = pci_probe_mmio_write32(cfg + 0x04u, (uint32_t)wanted_cmd);
        terminal_print("[K:QWIFI] command write rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();

        (void)pci_probe_config_read32(cfg + 0x04u, &readback);
        terminal_print("[K:QWIFI] command readback=");
        terminal_print_inline_hex64(readback);
        terminal_flush_log();
    }

    if (ecam_overlaps_efi_ram(bi, bar0_base, QWIFI_BAR0_PROBE_BYTES))
    {
        terminal_print("[K:QWIFI] skip BAR0 map: overlaps EFI RAM");
        terminal_flush_log();
        return;
    }

    rc = mmio_map_device_identity(bar0_base, QWIFI_BAR0_PROBE_BYTES);
    terminal_print("[K:QWIFI] BAR0 map rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" bytes=");
    terminal_print_inline_hex64(QWIFI_BAR0_PROBE_BYTES);
    terminal_flush_log();
    if (rc != 0)
        return;

    qwifi_dump_ath12k_probe_regs(bar0_base, bi);
}

static uint32_t qwifi_probe_one_function(const boot_info *bi,
                                         const dihos_pci_ecam *ecam,
                                         const char *label,
                                         uint8_t bus,
                                         uint8_t dev,
                                         uint8_t fn,
                                         uint16_t expected_device)
{
    uint64_t cfg;
    uint64_t page;
    uint32_t id = 0;
    uint32_t cmd = 0;
    uint32_t classrev = 0;
    uint32_t hdr = 0;
    uint32_t bar0 = 0;
    uint32_t bar1 = 0;
    uint32_t cap = 0;
    uint16_t vendor;
    uint16_t device;
    int rc;

    cfg = pci_ecam_config_phys(ecam, bus, dev, fn, 0u);
    if (!cfg)
    {
        terminal_print("[K:QWIFI] cfg unavailable for ");
        terminal_print(label);
        return 0;
    }

    page = cfg & ~0xFFFull;
    terminal_print("[K:QWIFI] probe ");
    terminal_print(label);
    terminal_print(" seg=");
    terminal_print_inline_hex64(QCOM_FC7800_SEGMENT);
    terminal_print(" bdf=");
    terminal_print_inline_hex64(((uint64_t)bus << 16) | ((uint64_t)dev << 8) | (uint64_t)fn);
    terminal_print(" cfg=");
    terminal_print_inline_hex64(cfg);
    terminal_print(" page=");
    terminal_print_inline_hex64(page);
    terminal_flush_log();

    if (ecam_overlaps_efi_ram(bi, page, 0x1000ull))
    {
        terminal_print("[K:QWIFI] skip cfg page: overlaps EFI RAM");
        terminal_flush_log();
        return 0;
    }

    rc = mmio_map_device_identity(page, 0x1000ull);
    terminal_print("[K:QWIFI] cfg page map rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_flush_log();
    if (rc != 0)
        return 0;

    terminal_print("[K:QWIFI] read id32 ");
    terminal_print(label);
    terminal_flush_log();
    rc = pci_probe_config_read32(cfg + 0x00u, &id);
    terminal_print("[K:QWIFI] id32 rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" id=");
    terminal_print_inline_hex64(id);
    terminal_flush_log();
    if (rc != 0 || id == 0u || id == 0xFFFFFFFFu)
        return 0;

    vendor = (uint16_t)(id & 0xFFFFu);
    device = (uint16_t)((id >> 16) & 0xFFFFu);

    (void)pci_probe_config_read32(cfg + 0x04u, &cmd);
    (void)pci_probe_config_read32(cfg + 0x08u, &classrev);
    (void)pci_probe_config_read32(cfg + 0x0Cu, &hdr);
    (void)pci_probe_config_read32(cfg + 0x10u, &bar0);
    (void)pci_probe_config_read32(cfg + 0x14u, &bar1);
    (void)pci_probe_config_read32(cfg + 0x34u, &cap);

    terminal_print("[K:QWIFI] fn ");
    terminal_print(label);
    terminal_print(" vid=");
    terminal_print_inline_hex64(vendor);
    terminal_print(" did=");
    terminal_print_inline_hex64(device);
    terminal_print(" cmd=");
    terminal_print_inline_hex64(cmd);
    terminal_print(" classrev=");
    terminal_print_inline_hex64(classrev);
    terminal_print(" hdr=");
    terminal_print_inline_hex64(hdr);
    terminal_print(" bar0=");
    terminal_print_inline_hex64(bar0);
    terminal_print(" bar1=");
    terminal_print_inline_hex64(bar1);
    terminal_print(" cap=");
    terminal_print_inline_hex64(cap);
    terminal_flush_log();

    if (vendor == QCOM_FC7800_VENDOR && device == expected_device)
    {
        terminal_print("[K:QWIFI] MATCH ");
        terminal_print(label);
        terminal_print(" Qualcomm ");
        terminal_print_inline_hex64(vendor);
        terminal_print(":");
        terminal_print_inline_hex64(device);
        terminal_flush_log();
        if (expected_device == QCOM_FC7800_DEVICE)
            qwifi_dump_pci_caps(cfg, cap);
        if (expected_device == QCOM_FC7800_DEVICE)
            qwifi_enable_and_dump_bar0(bi, cfg, cmd, bar0, bar1);
        return 1;
    }

    return 0;
}

static uint32_t probe_hardcoded_qcom_fc7800_wifi(const boot_info *bi)
{
    dihos_pci_ecam ecam = {0};
    uint32_t hits = 0u;

    terminal_print("[K:QWIFI] Windows hint: FastConnect 7800 PCI 17CB:1107 at PCI4.RP1_.WLN_ seg4 bus1 dev0 fn0");
    terminal_flush_log();

    if (!bi || !bi->acpi_rsdp)
    {
        terminal_print("[K:QWIFI] no boot info / no RSDP");
        return 0u;
    }

    qwifi_acpi_prekick(bi);

    if (!qwifi_find_ecam(bi->acpi_rsdp, &ecam))
        return 0u;

    (void)qwifi_probe_one_function(bi, &ecam, "parent-RP1", 0u, 0u, 0u, QCOM_FC7800_ROOTPORT_DEVICE);
    hits += qwifi_probe_one_function(bi, &ecam, "wifi-WLN", QCOM_FC7800_BUS, QCOM_FC7800_DEV, QCOM_FC7800_FN, QCOM_FC7800_DEVICE);

    terminal_print("[K:QWIFI] hardcoded hit count=");
    terminal_print_inline_hex64(hits);
    terminal_flush_log();
    return hits;
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
        terminal_flush_log();
        rc = mmio_map_device_identity(probe_base, probe_len);
        terminal_print("[K:PCI] PCI5 MMIO probe map rc=");
        terminal_print_inline_hex64((uint64_t)(int64_t)rc);
        terminal_flush_log();
        if (rc != 0)
            continue;
        terminal_print("[K:PCI] PCI5 MMIO probe: mapped (read disabled; aperture aborts unrecoverably)");
        terminal_flush_log();
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
    nic_hits += probe_hardcoded_qcom_fc7800_wifi(bi);
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] hardcoded Qualcomm WiFi path found; skipping PCI5/SDIO fallback");
        terminal_flush_log();
    }

    if (nic_hits == 0u && has_wwan_hint && bi && bi->acpi_rsdp)
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
        terminal_print("[K:PCI] ACPI probe PCI5._PSC");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "_PSC", &ret);
        terminal_print("[K:PCI] PCI5._PSC ret=");
        terminal_print_inline_hex64(ret);
        terminal_print("[K:PCI] ACPI probe PCI5.PVD5");
        (void)acpi_probe_net_exec_device_method(bi->acpi_rsdp, "PCI5", "PVD5", &ret);
        terminal_print("[K:PCI] PCI5.PVD5 ret=");
        terminal_print_inline_hex64(ret);
        terminal_flush_log();
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
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] SDC2 SDIO bootstrap skipped: real WiFi is PCIe 17CB:1107");
    }
    else if (!has_wlan_hint && has_wwan_hint)
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
    terminal_flush_log();
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

    nic_hits += probe_hardcoded_qcom_fc7800_wifi(bi);
    if (nic_hits != 0u)
    {
        terminal_print("[K:PCI] hardcoded Qualcomm WiFi path found; skipping broad PCI scan");
        terminal_print("[K:PCI] NIC hits total: ");
        terminal_print_inline_hex64(nic_hits);
        return;
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
