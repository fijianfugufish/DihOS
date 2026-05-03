#include "wifi/kwifi.h"
#include "asm/asm.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "kwrappers/kfile.h"
#include "memory/pmem.h"
#include "pci/pci_kernel_nic_probe.h"
#include "terminal/terminal_api.h"

#include <stdint.h>

static uint64_t kwifi_pages_for_bytes(uint64_t bytes)
{
    return (bytes + 4095ull) >> 12;
}

static int kwifi_fw_have_kind(const boot_info *bi, uint32_t kind)
{
    if (!bi)
        return 0;

    for (uint32_t i = 0; i < bi->wifi_fw_count && i < BOOTINFO_WIFI_FW_MAX; ++i)
    {
        if (bi->wifi_fw[i].kind == kind && bi->wifi_fw[i].base_phys && bi->wifi_fw[i].size_bytes)
            return 1;
    }

    return 0;
}

static int kwifi_fw_load_one_from_fs(boot_info *bi, uint32_t kind, const char *label, const char *const *paths)
{
    if (!bi || !label || !paths || bi->wifi_fw_count >= BOOTINFO_WIFI_FW_MAX || kwifi_fw_have_kind(bi, kind))
        return -1;

    for (uint32_t p = 0; paths[p]; ++p)
    {
        KFile f = (KFile){0};
        uint64_t size;
        uint64_t pages;
        void *buf;
        uint32_t got = 0;

        if (kfile_open(&f, paths[p], KFILE_READ) != 0)
            continue;

        size = kfile_size(&f);
        pages = kwifi_pages_for_bytes(size);
        buf = (size && pages) ? pmem_alloc_pages_lowdma(pages) : 0;
        if (!buf)
        {
            kfile_close(&f);
            terminal_print("[K:WIFI-FW] alloc failed for ");
            terminal_print(label);
            return -2;
        }

        if (kfile_read(&f, buf, (uint32_t)size, &got) != 0 || (uint64_t)got != size)
        {
            kfile_close(&f);
            terminal_print("[K:WIFI-FW] read failed for ");
            terminal_print(label);
            terminal_print(" got=");
            terminal_print_inline_hex64(got);
            terminal_print(" size=");
            terminal_print_inline_hex64(size);
            return -3;
        }

        kfile_close(&f);
        asm_dma_clean_range(buf, size);

        bi->wifi_fw[bi->wifi_fw_count].kind = kind;
        bi->wifi_fw[bi->wifi_fw_count].base_phys = pmem_virt_to_phys(buf);
        bi->wifi_fw[bi->wifi_fw_count].size_bytes = size;
        bi->wifi_fw_count++;

        terminal_print("[K:WIFI-FW] loaded ");
        terminal_print(label);
        terminal_print(" path=");
        terminal_print(paths[p]);
        terminal_print(" base=");
        terminal_print_inline_hex64(pmem_virt_to_phys(buf));
        terminal_print(" size=");
        terminal_print_inline_hex64(size);
        return 0;
    }

    terminal_print("[K:WIFI-FW] missing ");
    terminal_print(label);
    return -4;
}

static void kwifi_fw_load_from_fs_if_needed(boot_info *bi, int mounted)
{
    static const char *amss_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/ath12k/WCN7850/hw2.0/ncm865/amss.bin",
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/ath12k/WCN7850/hw2.0/amss.bin",
        0};
    static const char *m3_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/ath12k/WCN7850/hw2.0/ncm865/m3.bin",
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/m3.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/m3.bin",
        "0:/ath12k/WCN7850/hw2.0/m3.bin",
        0};
    static const char *board_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/board-2.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/board-2.bin",
        "0:/ath12k/WCN7850/hw2.0/board-2.bin",
        0};

    if (!bi)
        return;

    if (bi->wifi_fw_count >= BOOTINFO_WIFI_FW_MAX)
        return;

    if (!mounted)
    {
        terminal_print("[K:WIFI-FW] kernel FAT not mounted; firmware fallback skipped");
        return;
    }

    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_AMSS, "amss.bin", amss_paths);
    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_M3, "m3.bin", m3_paths);
    (void)kwifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_BOARD, "board-2.bin", board_paths);

    terminal_print("[K:WIFI-FW] count after kernel fallback: ");
    terminal_print_inline_hex64(bi->wifi_fw_count);
}

static void kwifi_print_stage2_firmware(const boot_info *bi)
{
    if (!bi)
        return;

    terminal_print("Stage2 WiFi FW count: ");
    terminal_print_inline_hex64(bi->wifi_fw_count);
    for (uint32_t i = 0; i < bi->wifi_fw_count && i < BOOTINFO_WIFI_FW_MAX; ++i)
    {
        terminal_print("WiFi FW kind=");
        terminal_print_inline_hex64(bi->wifi_fw[i].kind);
        terminal_print(" base=");
        terminal_print_inline_hex64(bi->wifi_fw[i].base_phys);
        terminal_print(" size=");
        terminal_print_inline_hex64(bi->wifi_fw[i].size_bytes);
    }
}

static void kwifi_print_stage2_nic_hints(const boot_info *bi)
{
    if (!bi)
        return;

    terminal_print("Stage2 NIC hint count: ");
    terminal_print_inline_hex64(bi->pci_nic_count);
    for (uint32_t i = 0; i < bi->pci_nic_count && i < BOOTINFO_PCI_NIC_MAX; ++i)
    {
        terminal_print("NIC seg=");
        terminal_print_inline_hex64(bi->pci_nics[i].segment);
        terminal_print(" bdf=");
        terminal_print_inline_hex64(((uint64_t)bi->pci_nics[i].bus << 16) |
                                    ((uint64_t)bi->pci_nics[i].dev << 8) |
                                    (uint64_t)bi->pci_nics[i].fn);
        terminal_print(" vid=");
        terminal_print_inline_hex64(bi->pci_nics[i].vendor_id);
        terminal_print(" did=");
        terminal_print_inline_hex64(bi->pci_nics[i].device_id);
        terminal_print(" class=");
        terminal_print_inline_hex64(bi->pci_nics[i].class_code);
        terminal_print(" sub=");
        terminal_print_inline_hex64(bi->pci_nics[i].subclass);
        terminal_print(" if=");
        terminal_print_inline_hex64(bi->pci_nics[i].prog_if);
        terminal_print(" bar0=");
        terminal_print_inline_hex64(bi->pci_nics[i].bar0_mmio_base);
    }
}

static void kwifi_print_acpi_net_hints(uint32_t net_hints)
{
    if ((net_hints & (DIHOS_NET_HINT_WLAN | DIHOS_NET_HINT_WIFI | DIHOS_NET_HINT_WCN)) != 0u)
        terminal_print("[NET] ACPI suggests WLAN/WCN platform path");
    else
        terminal_print("[NET] ACPI has no explicit WLAN/WIFI/WCN device marker");
    if ((net_hints & DIHOS_NET_HINT_SDIO) != 0u)
        terminal_print("[NET] ACPI suggests SDIO/SDHost network path");
    if ((net_hints & (DIHOS_NET_HINT_WWAN | DIHOS_NET_HINT_MHI)) != 0u)
        terminal_print("[NET] ACPI suggests WWAN/MHI path (non-PCI NIC likely)");
    else if ((net_hints & DIHOS_NET_HINT_USB) != 0u)
        terminal_print("[NET] ACPI suggests USB network path");
}

static void kwifi_print_acpi_resource_windows(void)
{
    uint32_t nres = acpi_probe_net_resource_count();
    const acpi_net_resource_window *res = acpi_probe_net_resources();

    terminal_print("[NET] ACPI resource window count:");
    terminal_print_inline_hex32(nres);
    for (uint32_t i = 0; i < nres; ++i)
    {
        terminal_print("[NET] res dev=");
        terminal_print(res[i].dev_name);
        terminal_print(" hid=");
        terminal_print(res[i].hid_name);
        terminal_print(" kind=");
        terminal_print_inline_hex32(res[i].kind);
        terminal_print(" rtype=");
        terminal_print_inline_hex32(res[i].rtype);
        terminal_print(" min=");
        terminal_print_inline_hex64(res[i].min_addr);
        terminal_print(" max=");
        terminal_print_inline_hex64(res[i].max_addr);
        terminal_print(" len=");
        terminal_print_inline_hex64(res[i].span_len);
    }
}

void kwifi_init(boot_info *bi, int storage_mounted)
{
    uint32_t net_hints = 0;

    if (!bi)
        return;

    terminal_print("[K:WIFI] init begin");
    kwifi_print_stage2_firmware(bi);
    kwifi_fw_load_from_fs_if_needed(bi, storage_mounted);
    kwifi_print_stage2_nic_hints(bi);

    if (bi->pci_nic_count)
    {
        terminal_print("[K:WIFI] using stage2 NIC hints; kernel MCFG fallback skipped");
        terminal_print("[K:WIFI] init end");
        return;
    }

    terminal_print("Stage2 NIC hints empty; trying kernel MCFG NIC probe");
    net_hints = acpi_probe_net_candidates_from_rsdp(bi->acpi_rsdp);
    pci_kernel_set_net_hints(net_hints);
    kwifi_print_acpi_net_hints(net_hints);
    kwifi_print_acpi_resource_windows();
    pci_kernel_probe_nics_from_mcfg(bi);
    terminal_print("[K:WIFI] init end");
}
