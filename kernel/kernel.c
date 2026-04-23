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
#include "kwrappers/kwindow.h"
#include "hardware_probes/acpi_probe_hidi2c_ready.h"

#include "terminal/terminal_api.h"

const boot_info *k_bootinfo_ptr = 0;

extern int usbdisk_bind_and_enumerate(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint);
extern blockdev_t g_usb_bd;
extern uint32_t usbdisk_get_lba_offset_lo(void);

extern void usbh_dbg_dot(int n, unsigned int rgb);

volatile uint32_t *g_fb32 = 0; // expose to usbdisk.c

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

    g_fb32 = (volatile uint32_t *)(uintptr_t)bi->fb.fb_base;

    // Breadcrumb A: we reached kernel and framebuffer works
    crumb((kcolor){20, 20, 20});

    // Try USB only if we have *some* hint
    int usb_ok = -1;
    if (bi->xhci_mmio_base || bi->acpi_rsdp)
    {
        usb_ok = usbdisk_bind_and_enumerate(bi->xhci_mmio_base, bi->acpi_rsdp);
    }
    // Breadcrumb B: green if usb_ok==0, red otherwise
    // crumb(usb_ok == 0 ? green : red);

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

    terminal_initialize(&font);
    terminal_print("terminal online");

    kinput_init(bi->xhci_mmio_base, bi->acpi_rsdp);

    terminal_print("^^ i sure hope this log is good ^^");

    terminal_flush_log();
    kgfx_render_all(black);

    // Load images (BMP -> ARGB32) only if mounted
    kimg cat_img = (kimg){0};
    int have_cat = 0;
    if (mounted && kimg_load_bmp(&cat_img, "0:/OS/System/Images/cat.bmp") == 0)
    {
        have_cat = 1;
    }

    /*

    // ---- Debug overlay (text) ----
    extern volatile uint8_t g_scsi_last_sk;
    extern volatile uint8_t g_scsi_last_asc;
    extern volatile uint8_t g_scsi_last_ascq;
    extern volatile uint32_t g_scsi_last_lba;

    extern volatile uint32_t g_disk_last_lba_lo;
    extern volatile uint32_t g_disk_last_count;
    extern volatile int g_disk_last_rc;

    extern volatile uint32_t g_bot_last_stage; // where we failed (190/191/192/193 etc)
    extern volatile uint32_t g_bot_last_dir;   // 0=OUT, 1=IN
    extern volatile uint32_t g_bot_last_req;   // bytes requested
    extern volatile uint32_t g_bot_last_got;   // bytes got in last call
    extern volatile int g_bot_last_r;          // return code from usbh_bulk_*_got

    extern volatile uint32_t g_bot_last_tag;
    extern volatile uint32_t g_bot_last_lba;
    extern volatile uint32_t g_bot_last_cnt;
    extern volatile uint32_t g_bot_last_xfer; // bytes expected for DATA stage
    extern volatile uint32_t g_bot_last_csw_sig;
    extern volatile uint32_t g_bot_last_csw_tag;
    extern volatile uint32_t g_bot_last_csw_res;
    extern volatile uint32_t g_bot_last_csw_status;

    extern volatile uint32_t g_xhci_last_cc;
    extern volatile uint32_t g_xhci_last_ev_epid;
    extern volatile uint32_t g_xhci_last_ev_slot;
    extern volatile uint32_t g_xhci_last_epin;
    extern volatile uint32_t g_xhci_last_epout;
    extern volatile uint32_t g_xhci_last_ev_ptr_lo;
    extern volatile uint32_t g_xhci_last_ev_len;

    volatile uint32_t g_expect_mismatch;
    volatile uint32_t g_expect_last_bad_epid;
    volatile uint32_t g_expect_last_bad_slot;
    volatile uint32_t g_expect_last_bad_ptr_lo;
    volatile uint32_t g_expect_last_bad_ptr_hi;

    static char line1[96];
    static char line2[160];
    static char line3[200];
    static char line4[200];
    static char line5[200];
    static char line6[200];
    static char line7[200];


    if (have_font)
    {
        // disk/msc mapping
        uint32_t off_lo = usbdisk_get_lba_offset_lo();
        uint32_t lba_lo = (uint32_t)g_disk_last_lba_lo;
        uint32_t abs_lo = off_lo + lba_lo;

        // line1: IMG err=XXXXXXXX row=XXXXXXXX
        ksb b1;
        ksb_init(&b1, line1, sizeof(line1));
        ksb_fmt(&b1,
                KSB_S, "IMG err=", KSB_HEX32, (uint32_t)g_kimg_dbg.err,
                KSB_S, " row=", KSB_HEX32, (uint32_t)g_kimg_dbg.yfile,
                KSB_END);

        // line2: want=... got=... fpos=... lba=... cnt=... rc=...
        ksb b2;
        ksb_init(&b2, line2, sizeof(line2));
        ksb_fmt(&b2,
                KSB_S, "want=", KSB_HEX32, (uint32_t)g_kimg_dbg.want,
                KSB_S, " got=", KSB_HEX32, (uint32_t)g_kimg_dbg.got,
                KSB_S, " fpos=", KSB_HEX32, (uint32_t)g_kimg_dbg.fpos_lo,
                KSB_S, " lba=", KSB_HEX32, (uint32_t)g_kimg_dbg.lba_lo,
                KSB_S, " cnt=", KSB_HEX32, (uint32_t)g_disk_last_count,
                KSB_S, " rc=", KSB_HEX32, (uint32_t)g_disk_last_rc,
                KSB_END);

        // line3: sense sk=SS asc=AA ascq=QQ slba=XXXXXXXX off=XXXXXXXX abs=XXXXXXXX
        ksb b3;
        ksb_init(&b3, line3, sizeof(line3));
        ksb_fmt(&b3,
                KSB_S, "sense sk=", KSB_HEX8, (uint32_t)g_scsi_last_sk,
                KSB_S, " asc=", KSB_HEX8, (uint32_t)g_scsi_last_asc,
                KSB_S, " ascq=", KSB_HEX8, (uint32_t)g_scsi_last_ascq,
                KSB_S, " slba=", KSB_HEX32, (uint32_t)g_scsi_last_lba,
                KSB_S, " off=", KSB_HEX32, off_lo,
                KSB_S, " abs=", KSB_HEX32, abs_lo,
                KSB_END);

        // line4: bot stg=XXXXXXXX dir=XXXXXXXX req=XXXXXXXX got=XXXXXXXX rc=XXXXXXXX
        ksb b4;
        ksb_init(&b4, line4, sizeof(line4));
        ksb_fmt(&b4,
                KSB_S, "bot stg=", KSB_HEX32, (uint32_t)g_bot_last_stage,
                KSB_S, " dir=", KSB_HEX32, (uint32_t)g_bot_last_dir,
                KSB_S, " req=", KSB_HEX32, (uint32_t)g_bot_last_req,
                KSB_S, " got=", KSB_HEX32, (uint32_t)g_bot_last_got,
                KSB_S, " rc=", KSB_HEX32, (uint32_t)g_bot_last_r,
                KSB_END);

        // line5: bot tag=XXXXXXXX lba=XXXXXXXX cnt=XXXXXXXX xfer=XXXXXXXX cswS=XXXXXXXX cswR=XXXXXXXX
        ksb b5;
        ksb_init(&b5, line5, sizeof(line5));
        ksb_fmt(&b5,
                KSB_S, "bot tag=", KSB_HEX32, (uint32_t)g_bot_last_tag,
                KSB_S, " lba=", KSB_HEX32, (uint32_t)g_bot_last_lba,
                KSB_S, " cnt=", KSB_HEX32, (uint32_t)g_bot_last_cnt,
                KSB_S, " xfer=", KSB_HEX32, (uint32_t)g_bot_last_xfer,
                KSB_S, " cswS=", KSB_HEX32, (uint32_t)g_bot_last_csw_status,
                KSB_S, " cswR=", KSB_HEX32, (uint32_t)g_bot_last_csw_res,
                KSB_END);

        ksb b6;
        ksb_init(&b6, line6, sizeof(line6));
        ksb_fmt(&b6,
                KSB_S, "xhci ep out=", KSB_HEX32, (uint32_t)g_xhci_last_epout,
                KSB_S, " ep in=", KSB_HEX32, (uint32_t)g_xhci_last_epin,
                KSB_S, " cc=", KSB_HEX32, (uint32_t)g_xhci_last_cc,
                KSB_S, " epid=", KSB_HEX32, (uint32_t)g_xhci_last_ev_epid,
                KSB_S, " slot=", KSB_HEX32, (uint32_t)g_xhci_last_ev_slot,
                KSB_S, " evptr=", KSB_HEX32, (uint32_t)g_xhci_last_ev_ptr_lo,
                KSB_S, " evlen=", KSB_HEX32, (uint32_t)g_xhci_last_ev_len,
                KSB_END);

        ksb b7;
        ksb_init(&b7, line7, sizeof(line7));
        ksb_fmt(&b7,
                KSB_S, "mismatches=", KSB_HEX32, (uint32_t)g_expect_mismatch,
                KSB_S, " bad epid=", KSB_HEX32, (uint32_t)g_expect_last_bad_epid,
                KSB_S, " bad slot=", KSB_HEX32, (uint32_t)g_expect_last_bad_slot,
                KSB_S, " bad lo=", KSB_HEX32, (uint32_t)g_expect_last_bad_ptr_lo,
                KSB_S, " bad hi=", KSB_HEX32, (uint32_t)g_expect_last_bad_ptr_hi,
                KSB_END);
    }
    */

    // Scene
    kwindow_style demo_window_style = kwindow_style_default();
    kgfx_obj_handle taskbar = kgfx_obj_add_rect(0, 0, (uint32_t)kgfx_info()->width, 28, 0, dark_gray, 1);
    kgfx_obj_handle winA = kgfx_obj_add_rect(40, 60, 320, 180, 1, yellow_green, 1);
    kgfx_obj_handle winB = kgfx_obj_add_circle(260, 180, 60, 2, dark_turquoise, 1);
    kwindow_handle demo_window = {-1};

    kbutton_init();
    kwindow_init();

    kgfx_obj_ref(taskbar)->outline_width = 2;
    kgfx_obj_ref(taskbar)->outline = black;
    kgfx_obj_ref(winA)->outline_width = 3;
    kgfx_obj_ref(winA)->outline = gold;
    kgfx_obj_ref(winB)->outline_width = 4;
    kgfx_obj_ref(winB)->outline = white;

    // Show status via fills
    kgfx_obj_set_fill(winA, mounted ? yellow_green : red);
    kgfx_obj_set_fill(winB, have_font ? gold : red);
    kgfx_obj_set_fill(taskbar, have_cat ? cyan : red);

    demo_window_style.body_fill = dark_gray;
    demo_window_style.body_outline = blue;
    demo_window_style.titlebar_fill = dark_slate_gray;
    demo_window_style.close_button_style.fill = red;
    demo_window_style.close_button_style.hover_fill = tomato;
    demo_window_style.close_button_style.pressed_fill = dark_red;
    demo_window = kwindow_create(120, 120, 340, 220, 20,
                                 have_font ? &font : 0,
                                 "Demo Window",
                                 &demo_window_style);
    (void)demo_window;

    if (have_font)
    {
        int cx = (int)kgfx_info()->width / 2;
        kgfx_obj_add_text(&font, "left aligned\nsecond line", 20, 40, 10, white, 255, 2, 1, 4, KTEXT_ALIGN_LEFT, 1);
        kgfx_obj_handle tCenter = kgfx_obj_add_text(&font, "center title", cx, 120, 11, yellow_green, 255, 3, 2, 6, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj *tc = kgfx_obj_ref(tCenter);
        tc->outline_width = 3;
        tc->outline = red;
        tc->outline_alpha = 255;
        kgfx_obj_add_text(&font, "right edge", (int)kgfx_info()->width - 20, 180, 12, cyan, 255, 2, 1, 4, KTEXT_ALIGN_RIGHT, 1);

        /*
        kcolor ok_col = (kcolor){80, 255, 80};
        kcolor bad_col = (kcolor){255, 80, 80};
        kcolor info_col = (kcolor){240, 240, 240};

        kcolor c = have_cat ? ok_col : bad_col;

        kgfx_obj_handle t1 = kgfx_obj_add_text(&font, line1, cx, 220, 11, c, 255, 3, 2, 6, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t2 = kgfx_obj_add_text(&font, line2, cx, 320, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t3 = kgfx_obj_add_text(&font, line3, cx, 420, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t4 = kgfx_obj_add_text(&font, line4, cx, 520, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t5 = kgfx_obj_add_text(&font, line5, cx, 620, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t6 = kgfx_obj_add_text(&font, line6, cx, 720, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);
        kgfx_obj_handle t7 = kgfx_obj_add_text(&font, line7, cx, 820, 11, info_col, 255, 2, 1, 4, KTEXT_ALIGN_CENTER, 1);

        kgfx_obj_ref(t1)->outline_width = 3;
        kgfx_obj_ref(t2)->outline_width = 2;
        kgfx_obj_ref(t3)->outline_width = 2;
        kgfx_obj_ref(t4)->outline_width = 2;
        kgfx_obj_ref(t5)->outline_width = 2;
        kgfx_obj_ref(t6)->outline_width = 2;
        kgfx_obj_ref(t7)->outline_width = 2;
        */
    }

    if (have_cat)
    {
        int cat_x = 500;
        int cat_y = 400;

        kgfx_obj_handle cat = kgfx_obj_add_image(cat_img.px, cat_img.w, cat_img.h, cat_x, cat_y, cat_img.w);

        kgfx_obj *co = kgfx_obj_ref(cat);
        co->alpha = 255;
        co->outline_width = 3;
        co->outline = white;
        co->outline_alpha = 255;
        co->z = 5;

        kgfx_image_set_sample_mode(cat, KGFX_IMAGE_SAMPLE_BILINEAR);
        kgfx_image_set_scale(cat, 500);

        terminal_success("wowo we have cat");
        terminal_print("how cool");
    }
    else
    {
        terminal_warn("cat not loaded");
    }

    if (kmouse_init("0:/OS/System/Images/Mouse/idle.bmp") != 0)
    {
        terminal_warn("cursor not loaded");
    }
    kmouse_set_sensitivity_pct(300);

    kgfx_render_all(black);

    uint32_t frame = 0;

    static uint32_t dbg_tick = 0;

    terminal_clear();

    for (;;)
    {
        kinput_poll();
        kmouse_update();
        kwindow_update_all();
        kbutton_update_all();

        /* keyboard edge tests */
        if (kinput_key_pressed(KEY_A))  /* HID usage 0x04 = 'A' */
            terminal_print("A pressed\n");

        if (kinput_key_released(KEY_A))
            terminal_print("A released\n");

        /* optional extra sanity checks */
        if (kinput_key_pressed(KEY_LSHIFT))  /* Left Shift */
            terminal_print("LShift pressed\n");

        if (kinput_key_released(KEY_LSHIFT))
            terminal_print("LShift released\n");

        kgfx_render_all(black);
        frame++;
    }
}
