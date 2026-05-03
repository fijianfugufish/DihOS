#include "bootinfo.h"
#include "memory/pmem.h"
#include "kwrappers/kfile.h"
#include "kwrappers/ktext.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"
#include "usb/blockdev.h"
#include "kwrappers/string.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kwindow.h"
#include "system/dihos_time.h"
#include "apps/desktop_shell_api.h"
#include "apps/file_explorer_api.h"
#include "apps/sacx_runtime.h"
#include "apps/text_editor_api.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "hardware_probes/acpi_probe_xhci.h"
#include "hardware_probes/acpi_probe_net_candidates.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "pci/pci_ecam_lookup.h"
#include "pci/pci_ecam_map_plan.h"
#include "pci/pci_kernel_nic_probe.h"
#include "memory/mmio_map.h"
#include "pci/pci_dump_mapped.h"
#include "asm/asm.h"

#include "terminal/terminal_api.h"

const boot_info *k_bootinfo_ptr = 0;

extern int usbdisk_bind_and_enumerate(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint);
extern int usbdisk_bind_and_enumerate_multi(const uint64_t *xhci_mmio_hints,
                                            uint32_t hint_count,
                                            uint64_t acpi_rsdp_hint);
extern blockdev_t g_usb_bd;
extern uint32_t usbdisk_get_lba_offset_lo(void);

extern void usbh_dbg_dot(int n, unsigned int rgb);

volatile uint32_t *g_fb32 = 0; // expose to usbdisk.c
volatile uint64_t g_dihos_tick = 0;

static inline void crumb(kcolor c)
{
    kgfx_fill(c);
    kgfx_flush();
}

static void xhci_hint_add(uint64_t *hints, uint32_t *count, uint32_t cap, uint64_t mmio)
{
    if (!hints || !count)
        return;

    mmio &= ~0xFULL;
    if (!mmio)
        return;

    for (uint32_t i = 0; i < *count; ++i)
    {
        if ((hints[i] & ~0xFULL) == mmio)
            return;
    }

    if (*count < cap)
        hints[(*count)++] = mmio;
}

static uint32_t xhci_build_hint_order(const boot_info *bi, uint64_t *hints, uint32_t cap)
{
    uint32_t count = 0;
    uint32_t src_count = 0;
    const uint64_t preferred_fallback = 0x000000000A600000ULL;

    if (!bi || !hints || cap == 0u)
        return 0;

    src_count = bi->xhci_mmio_count;
    if (src_count > BOOTINFO_XHCI_MMIO_MAX)
        src_count = BOOTINFO_XHCI_MMIO_MAX;

    /* The storage port on this machine is behind the A6 fallback. Try it first. */
    for (uint32_t i = 0; i < src_count; ++i)
    {
        if ((bi->xhci_mmio_bases[i] & ~0xFULL) == preferred_fallback)
            xhci_hint_add(hints, &count, cap, bi->xhci_mmio_bases[i]);
    }

    for (uint32_t i = 0; i < src_count; ++i)
        xhci_hint_add(hints, &count, cap, bi->xhci_mmio_bases[i]);

    if (!count)
        xhci_hint_add(hints, &count, cap, bi->xhci_mmio_base);

    return count;
}

static uint64_t pages_for_bytes(uint64_t bytes)
{
    return (bytes + 4095ull) >> 12;
}

static int wifi_fw_have_kind(const boot_info *bi, uint32_t kind)
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

static int wifi_fw_load_one_from_fs(boot_info *bi, uint32_t kind, const char *label, const char *const *paths)
{
    if (!bi || !label || !paths || bi->wifi_fw_count >= BOOTINFO_WIFI_FW_MAX || wifi_fw_have_kind(bi, kind))
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
        pages = pages_for_bytes(size);
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

static void wifi_fw_load_from_fs_if_needed(boot_info *bi, int mounted)
{
    static const char *amss_paths[] = {
        "0:/OS/Firmware/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/OS/ath12k/WCN7850/hw2.0/amss.bin",
        "0:/ath12k/WCN7850/hw2.0/amss.bin",
        0};
    static const char *m3_paths[] = {
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

    (void)wifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_AMSS, "amss.bin", amss_paths);
    (void)wifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_M3, "m3.bin", m3_paths);
    (void)wifi_fw_load_one_from_fs(bi, BOOTINFO_WIFI_FW_BOARD, "board-2.bin", board_paths);

    terminal_print("[K:WIFI-FW] count after kernel fallback: ");
    terminal_print_inline_hex64(bi->wifi_fw_count);
}

void kmain(boot_info *bi)
{
    k_bootinfo_ptr = bi;

    // init graphics & pmem
    if (kgfx_init(bi) != 0)
        for (;;)
            asm_wait();

    g_fb32 = (volatile uint32_t *)(uintptr_t)bi->fb.fb_base;
    pmem_init(bi);

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    /* Probe reads install temporary vectors locally; keep global VBAR untouched. */
#endif

    kbutton_init();
    ktextbox_init();
    kwindow_init();

    crumb((kcolor){20, 20, 20});

    uint64_t xhci_mmio_order[BOOTINFO_XHCI_MMIO_MAX] = {0};
    uint32_t xhci_mmio_count =
        xhci_build_hint_order(bi, xhci_mmio_order, BOOTINFO_XHCI_MMIO_MAX);

    const uint64_t *xhci_mmio_bases =
        xhci_mmio_count ? xhci_mmio_order : 0;

    uint64_t acpi_xhci_bases[BOOTINFO_XHCI_MMIO_MAX] = {0};
    uint32_t acpi_xhci_count =
        acpi_xhci_get_mmios_from_rsdp(
            bi->acpi_rsdp,
            acpi_xhci_bases,
            BOOTINFO_XHCI_MMIO_MAX);
    
    uint32_t xhci_mmio_sources[BOOTINFO_XHCI_MMIO_MAX] = {0};
    uint32_t acpi_probe_thinks_good = 0;

    if (acpi_xhci_count)
    {
        xhci_mmio_bases = acpi_xhci_bases;
        xhci_mmio_count = acpi_xhci_count;
        acpi_probe_thinks_good = 1;

        bi->xhci_mmio_count = acpi_xhci_count;
        bi->xhci_mmio_base = acpi_xhci_bases[0];

        for (uint32_t i = 0; i < BOOTINFO_XHCI_MMIO_MAX; ++i)
        {
            if (i < acpi_xhci_count)
            {
                xhci_mmio_sources[i] = BOOTINFO_XHCI_SOURCE_DISCOVERED;
                bi->xhci_mmio_bases[i] = acpi_xhci_bases[i];
                bi->xhci_mmio_sources[i] = BOOTINFO_XHCI_SOURCE_DISCOVERED;
            }
            else
            {
                xhci_mmio_sources[i] = 0;
                bi->xhci_mmio_bases[i] = 0;
                bi->xhci_mmio_sources[i] = 0;
            }
        }
    }

    int usb_ok = -1;

    if (xhci_mmio_count || bi->acpi_rsdp)
    {
        usb_ok = usbdisk_bind_and_enumerate_multi(
            xhci_mmio_bases,
            xhci_mmio_count,
            bi->acpi_rsdp);
    }

    // Mount only when enumeration succeeded
    int mounted = 0;
    if (usb_ok == 0)
    {
        kfile_bind_blockdev(&g_usb_bd);
        // crumb((kcolor){20, 20, 20});
        mounted = (kfile_mount0() == 0);
    }

    // Prepare backbuffer/scene after breadcrumbs (they draw to front buffer)
    if (kgfx_scene_init() != 0 || !mounted)
        for (;;)
            asm_wait();

    // Try to load a PSF font only if mounted
    kfont font = (kfont){0};
    void *font_blob = 0;
    uint32_t font_blob_sz = 0;
    int have_font = 0;
    if (mounted &&
        ktext_load_psf_file("0:/OS/System/Fonts/Solarize.psf", &font, &font_blob, &font_blob_sz) == 0)
    {
        have_font = 1;
    }

    sacx_runtime_init(have_font ? &font : 0);

    terminal_initialize(&font);
    terminal_print("terminal online");
    terminal_success("sacx runtime online");
    
    terminal_print("[stage2_report] begin");
    terminal_print("stage2_report_len:");
    terminal_print_inline_hex64(bi->stage2_report_len);
    if (bi->stage2_report_len)
        terminal_print(bi->stage2_report);
    else
        terminal_print("(empty)");
    terminal_print("[stage2_report] end");
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
    wifi_fw_load_from_fs_if_needed(bi, mounted);

    /*
    acpi_pci_print_ecams_from_rsdp(bi->acpi_rsdp);
    pci_print_lookup_examples_from_rsdp(bi->acpi_rsdp);
    pci_ecam_print_map_plan_from_rsdp(bi->acpi_rsdp);
    */

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
    if (!bi->pci_nic_count)
    {
        uint32_t net_hints = 0;
        terminal_print("Stage2 NIC hints empty; trying kernel MCFG NIC probe");
        net_hints = acpi_probe_net_candidates_from_rsdp(bi->acpi_rsdp);
        pci_kernel_set_net_hints(net_hints);
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
        pci_kernel_probe_nics_from_mcfg(bi);
    }
    {
        const int enable_risky_mapped_pci_probe = 0;
        if (enable_risky_mapped_pci_probe)
        {
            mmio_map_print_state();
            terminal_print("PCI probe: before ECAM map");
            mmio_map_pci_ecams_from_rsdp(bi->acpi_rsdp);
            terminal_print("PCI probe: after ECAM map");
            terminal_print("PCI probe: before mapped dump");
            terminal_flush_log();
            pci_dump_mapped_ecam_bus0_from_rsdp(bi->acpi_rsdp);
            terminal_print("PCI probe: after mapped dump");
            terminal_flush_log();
        }
        else
        {
            terminal_print("PCI mapped ECAM probe skipped (stability mode)");
            terminal_flush_log();
        }
    }

    terminal_print("ACPI xHCI discovered count: ");
    terminal_print_inline_hex64(acpi_xhci_count);

    for (uint32_t i = 0;
        i < acpi_xhci_count && i < BOOTINFO_XHCI_MMIO_MAX;
        ++i)
    {
        terminal_print("ACPI xHCI base: ");
        terminal_print_inline_hex64(acpi_xhci_bases[i]);
    }

    kinput_init_multi(xhci_mmio_bases, xhci_mmio_count, bi->acpi_rsdp);

    terminal_print("^^ i sure hope this log is good ^^");

    terminal_clear();
    kgfx_render_all(black);

    if (have_font)
    {
        file_explorer_init(&font);
        terminal_success("file explorer online");
        text_editor_init(&font);
        terminal_success("text editor online");
    }
    else
    {
        terminal_warn("font unavailable; explorer/editor skipped");
    }

    desktop_shell_init(have_font ? &font : 0);
    terminal_success("desktop shell online");

    if (kmouse_init() != 0)
    {
        terminal_warn("cursor not loaded");
    }
    kmouse_set_sensitivity_pct(500);

    kgfx_render_all(black);

    uint32_t frame = 0;

    static uint32_t dbg_tick = 0;

    for (;;)
    {
        ++g_dihos_tick;

        kinput_poll();
        kmouse_update();
        kwindow_update_all();
        file_explorer_update();
        text_editor_update();
        desktop_shell_update();
        kbutton_update_all();
        ktextbox_update_all();
        terminal_update_input();

        kgfx_render_all(black);
        sacx_runtime_update();
        frame++;
    }
}
