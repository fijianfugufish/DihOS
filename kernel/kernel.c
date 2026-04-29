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

#include "terminal/terminal_api.h"

const boot_info *k_bootinfo_ptr = 0;

extern int usbdisk_bind_and_enumerate(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint);
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

void kmain(const boot_info *bi)
{
    k_bootinfo_ptr = bi;

    // init graphics & pmem
    if (kgfx_init(bi) != 0)
        for (;;)
            __asm__ __volatile__("wfe");

    pmem_init(bi);

    kbutton_init();
    ktextbox_init();
    kwindow_init();

    g_fb32 = (volatile uint32_t *)(uintptr_t)bi->fb.fb_base;

    crumb((kcolor){20, 20, 20});

    // Try USB only if we have *some* hint
    int usb_ok = -1;
    if (bi->xhci_mmio_base || bi->acpi_rsdp)
    {
        usb_ok = usbdisk_bind_and_enumerate(bi->xhci_mmio_base, bi->acpi_rsdp);
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
            __asm__ __volatile__("wfe");

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

    kinput_init(bi->xhci_mmio_base, bi->acpi_rsdp);

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
