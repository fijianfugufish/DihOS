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
#include "kwrappers/kui.h"
#include "system/dihos_time.h"
#include "system/task_accounting.h"
#include "system/cpu_info.h"
#include "system/smp.h"
#include "system/kwork.h"
#include "system/ksystem_font.h"
#include "system/kearly_console.h"
#include "system/boot_volume_blockdev.h"
#include "system/kcrash_map.h"
#include "system/kcrash_map.h"
#include "hyperv/hyperv.h"
#include "hyperv/hyperv_storage.h"
#include "apps/desktop_shell_api.h"
#include "apps/screenshot_service.h"
#include "apps/file_explorer_api.h"
#include "apps/sacx_runtime.h"
#include "apps/task_manager_api.h"
#include "apps/text_editor_api.h"
#include "shell/dihos_shell.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "hardware_probes/acpi_probe_xhci.h"
#include "hardware_probes/acpi_probe_pci_lookup.h"
#include "gpu/adreno_x1_85.h"
#include "gpu/gpu_core.h"
#include "pci/pci_ecam_lookup.h"
#include "pci/pci_ecam_map_plan.h"
#include "memory/mmio_map.h"
#include "pci/pci_dump_mapped.h"
#include "asm/asm.h"
#include "wifi/kwifi.h"
#include "usb/usb_ethernet.h"

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

void kmain(boot_info *bi)
{
    k_bootinfo_ptr = bi;
    asm_enable_fp_simd();

    // init graphics & pmem
    if (kgfx_init(bi) != 0)
        for (;;)
            asm_wait();

    g_fb32 = (volatile uint32_t *)(uintptr_t)bi->fb.fb_base;
    pmem_init(bi);
    int image_decoder_reserved = (kimg_prepare_decoder() == 0);

    kfont fallback_font = (kfont){0};
    int have_fallback_font = (ksystem_font_init_fallback(&fallback_font) == 0);

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    asm_aa64_panic_renderer_init(have_fallback_font ? &fallback_font : 0);
    asm_aa64_install_exception_vectors();
#endif

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    /* Probe reads install temporary vectors locally; keep global VBAR untouched. */
#endif

    kbutton_init();
    ktextbox_init();
    kwindow_init();
    kui_init(have_fallback_font ? &fallback_font : 0);

    crumb((kcolor){20, 20, 20});
    kearly_console_begin(have_fallback_font ? &fallback_font : 0);
    terminal_print("early console online");
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    {
        uint64_t current_el = 0u;
        __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(current_el));
        switch ((current_el >> 2) & 3u)
        {
        case 0u: terminal_print("exception level: el0"); break;
        case 1u: terminal_print("exception level: el1"); break;
        case 2u: terminal_print("exception level: el2"); break;
        default: terminal_print("exception level: el3"); break;
        }
    }
#elif defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)
    terminal_print("exception level: not applicable on x64");
#endif
    if (image_decoder_reserved)
        terminal_success("kimg: decoder arena reserved");
    else
        terminal_warn("kimg: decoder arena reserve failed");
    if (!have_fallback_font)
        terminal_warn("embedded psf fallback unavailable; using block debug font");

    cpu_info_init(bi ? bi->acpi_rsdp : 0u);
#if defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)
    terminal_print("kernel build: HYPERV-33 arch=x64");
#elif defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    terminal_print("kernel build: HYPERV-33 arch=aa64");
#else
    terminal_print("kernel build: HYPERV-33 arch=unknown");
#endif

    hyperv_info hv = (hyperv_info){0};
    int hyperv_present = (hyperv_detect(&hv, bi->acpi_rsdp) == 0);
    if (hyperv_present)
    {
        hyperv_log_detection(&hv);
        if (hyperv_core_init(&hv) != 0)
            terminal_error("hyperv: core init failed");
    }

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
    int mounted = 0;
    blockdev_t hyperv_bd = (blockdev_t){0};
    blockdev_t boot_volume_bd = (blockdev_t){0};

    if (hyperv_present)
    {
        terminal_print("storage: Hyper-V path selected");
        if (hyperv_storage_try_bind(&hyperv_bd, bi->acpi_rsdp) == 0)
        {
            kfile_bind_blockdev(&hyperv_bd);
            mounted = (kfile_mount0() == 0);
            if (mounted)
            {
                terminal_success("storvsc: filesystem mounted");
                terminal_success("storvsc: mounted read/write");
            }
            else
                terminal_error("storvsc: filesystem mount failed");
        }
    }

    if (!mounted && !hyperv_present && (xhci_mmio_count || bi->acpi_rsdp))
    {
        terminal_print("usb: probing xhci storage");
        usb_ok = usbdisk_bind_and_enumerate_multi(
            xhci_mmio_bases,
            xhci_mmio_count,
            bi->acpi_rsdp);
    }
    else if (!mounted && !hyperv_present)
    {
        terminal_warn("usb: no xhci hints or acpi rsdp");
    }

    if (!mounted && usb_ok == 0)
    {
        terminal_print("usb: storage enumerated; mounting filesystem");
        kfile_bind_blockdev(&g_usb_bd);
        // crumb((kcolor){20, 20, 20});
        mounted = (kfile_mount0() == 0);
        if (!mounted)
        {
            terminal_error("usb: filesystem mount failed");
            terminal_error("usb: fatal stop for debug");
            for (;;)
                asm_wait();
        }
    }
    else if (!mounted && !hyperv_present)
    {
        terminal_error("usb: storage unavailable");
        terminal_warn("bootvol: trying UEFI boot-volume RAM fallback");
        if (boot_volume_blockdev_init(bi, &boot_volume_bd) == 0)
        {
            kfile_bind_blockdev(&boot_volume_bd);
            mounted = (kfile_mount0() == 0);
            if (mounted)
            {
                terminal_warn("bootvol: mounted RAM snapshot; writes are not persistent");
            }
        }
    }

    if (!mounted)
    {
        terminal_error("storage: no mountable provider");
        terminal_error("storage: fatal stop for debug");
        for (;;)
            asm_wait();
    }

    // Prepare backbuffer/scene after breadcrumbs (they draw to front buffer)
    if (kgfx_scene_init() != 0)
    {
        terminal_error("gfx: scene init failed");
        for (;;)
            asm_wait();
    }
    kearly_console_end();

    // Prefer the disk system font when mounted; otherwise keep the embedded fallback.
    kfont disk_font = (kfont){0};
    void *font_blob = 0;
    uint32_t font_blob_sz = 0;
    kfont *font = have_fallback_font ? &fallback_font : 0;
    int have_disk_font = 0;
    if (mounted &&
        ksystem_font_load_system_file("0:/OS/System/Fonts/Solarize.psf", &disk_font, &font_blob, &font_blob_sz) == 0)
    {
        font = &disk_font;
        have_disk_font = 1;
    }
    kui_set_font(font);
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    asm_aa64_panic_renderer_init(font);
#endif

    sacx_runtime_init(font);

    terminal_initialize(font);
    terminal_print("terminal online");
    (void)gpu_core_init(bi);
    if (kcrash_map_load("0:/OS/aa64/KERNEL.CRASHMAP") == 0)
        terminal_success("crash map: source locations ready");
    else
        terminal_warn("crash map: source locations unavailable");
    if (kcrash_map_load("0:/OS/aa64/KERNEL.CRASHMAP") == 0)
        terminal_success("crash map: source locations ready");
    else
        terminal_warn("crash map: source locations unavailable");
    /* Load and cache the panic-title emoji while storage is healthy. */
    (void)ktext_measure_line_px(font, "\xf0\x9f\xa5\x80", 1u, 0);
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    {
        uint64_t current_el = 0u;
        __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(current_el));
        switch ((current_el >> 2) & 3u)
        {
        case 0u: terminal_print("exception level: el0"); break;
        case 1u: terminal_print("exception level: el1"); break;
        case 2u: terminal_print("exception level: el2"); break;
        default: terminal_print("exception level: el3"); break;
        }
    }
#elif defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)
    terminal_print("exception level: not applicable on x64");
#endif
    smp_init(bi ? bi->acpi_rsdp : 0u);
    kwork_init();
    if (!mounted)
        terminal_warn("storage offline; file-backed apps disabled");
    else if (have_disk_font)
        terminal_success("font: using disk system font");
    else if (!have_disk_font)
        terminal_warn("font: using embedded baked PSF fallback");
    terminal_success("sacx runtime online");

    terminal_print("[stage2_report] begin");
    terminal_print("stage2_report_len:");
    terminal_print_inline_hex64(bi->stage2_report_len);
    if (bi->stage2_report_len)
        terminal_print(bi->stage2_report);
    else
        terminal_print("(empty)");
    terminal_print("[stage2_report] end");

    kwifi_init(bi, mounted);

    /*
    acpi_pci_print_ecams_from_rsdp(bi->acpi_rsdp);
    pci_print_lookup_examples_from_rsdp(bi->acpi_rsdp);
    pci_ecam_print_map_plan_from_rsdp(bi->acpi_rsdp);
    */
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
    {
        const uint64_t usb_eth_a0_mmio = 0x000000000A000000ULL;
        uint64_t usb_eth_hints[1] = {0};
        uint32_t usb_eth_hint_count = 0u;

        for (uint32_t i = 0u; i < xhci_mmio_count; ++i)
        {
            if ((xhci_mmio_bases[i] & ~0xFULL) == usb_eth_a0_mmio)
            {
                usb_eth_hints[0] = usb_eth_a0_mmio;
                usb_eth_hint_count = 1u;
                break;
            }
        }

        if (usb_eth_hint_count)
        {
            terminal_print("usbnet: probing Ethernet adapters on xHCI A0 only");
            if (usb_ethernet_probe_multi(usb_eth_hints, usb_eth_hint_count, bi->acpi_rsdp) != 0)
                terminal_warn("usbnet: Ethernet unavailable on xHCI A0");
        }
        else
        {
            terminal_warn("usbnet: xHCI A0 not discovered; Ethernet probe skipped");
        }
        terminal_flush_log();
    }

    terminal_print("^^ i sure hope this log is good ^^");
    terminal_flush_log();

    terminal_clear();
    kgfx_render_all(black);

    if (mounted && font)
    {
        file_explorer_init(font);
        terminal_success("file explorer online");
        text_editor_init(font);
        terminal_success("text editor online");
        task_manager_init(font);
        terminal_success("task manager online");
    }
    else
    {
        terminal_warn("file explorer/editor skipped");
    }

    desktop_shell_init(font);
    terminal_success("desktop shell online");

    if (kmouse_init() != 0)
    {
        terminal_warn("cursor not loaded");
    }
    kmouse_set_sensitivity_pct(500);
    screenshot_service_init(font);

    kgfx_render_all(black);

    uint32_t frame = 0;

    static uint32_t dbg_tick = 0;

    for (;;)
    {
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
        /* A worker-core exception is a kernel-wide failure.  Do not let the
           scheduler/compositor overwrite the panic frame it produced. */
        if (asm_aa64_panic_active())
            for (;;)
                asm_wait();
#endif
        ++g_dihos_tick;
        /* GPU completion is observed from the frame loop, not by stalling a
         * shell command.  This is intentionally a bounded, non-spinning
         * poll; a future compositor owns the kick side. */
        (void)gpu_core_mesart_3d_poll(0);
        task_accounting_frame_begin();

        task_accounting_add(TASK_ACCOUNT_INPUT_UI, 1u);
        kinput_poll();
        kmouse_update();
        if (!screenshot_service_update())
        {
            task_accounting_add(TASK_ACCOUNT_INPUT_UI, 1u);
            kwindow_update_all();
            file_explorer_update();
            text_editor_update();
            task_manager_update();
            task_accounting_add(TASK_ACCOUNT_KERNEL_APPS, 1u);
            desktop_shell_update();
            kbutton_update_all();
            kui_update_all();
            ktextbox_update_all();
            task_accounting_add(TASK_ACCOUNT_TERMINAL, 1u);
            terminal_update_input();
        }

        task_accounting_add(TASK_ACCOUNT_RENDER, 1u);
        /* The visual test holds its one completed GPU frame on scanout. CPU
         * presentation resumes immediately when the test is stopped. */
        if (!dihos_shell_mesart_visual_frame_busy())
            kgfx_render_all(black);
        dihos_shell_mesart_visual_submit_after_cpu();
        task_accounting_add(TASK_ACCOUNT_SACX, 1u);
        sacx_runtime_update();
        frame++;
    }
}
