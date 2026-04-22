// src/usbh_xhci.c — verbose “dot probe” build that enumerates MSC
#include "usb/usbh.h"
#include "usb/xhci_regs.h"
#include "bootinfo.h"
#include <stdint.h>
#include <stddef.h>

#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"

/* --- TRB constants (kept here so it compiles even if your header is thin) --- */
#ifndef TRB_TYPE_LINK
#define TRB_TYPE_LINK 6
#endif
#ifndef TRB_TYPE_NORMAL
#define TRB_TYPE_NORMAL 1
#endif
#ifndef TRB_TYPE_ENABLE_SLOT
#define TRB_TYPE_ENABLE_SLOT 9
#endif
#ifdef TRB_TYPE_ADDR_DEVICE
#undef TRB_TYPE_ADDR_DEVICE
#endif
#define TRB_TYPE_ADDR_DEVICE 11

#ifndef TRB_TYPE_ADDRESS_DEVICE
#define TRB_TYPE_ADDRESS_DEVICE TRB_TYPE_ADDR_DEVICE
#endif

#ifndef TRB_TYPE_CONFIG_EP
#define TRB_TYPE_CONFIG_EP 12
#endif
#ifndef TRB_TYPE_EVALUATE_CTX
#define TRB_TYPE_EVALUATE_CTX 13
#endif
#ifndef TRB_TYPE_SETUP_STAGE
#define TRB_TYPE_SETUP_STAGE 2
#endif
#ifndef TRB_TYPE_DATA_STAGE
#define TRB_TYPE_DATA_STAGE 3
#endif
#ifndef TRB_TYPE_STATUS_STAGE
#define TRB_TYPE_STATUS_STAGE 4
#endif
#ifndef TRB_TYPE_NOOP_CMD
#define TRB_TYPE_NOOP_CMD 23
#endif

// Event TRBs
#ifndef TRB_TYPE_XFER_EVT
#define TRB_TYPE_XFER_EVT 32
#endif

#ifndef TRB_TYPE_CMD_CMPL_EVT
#define TRB_TYPE_CMD_CMPL_EVT 33
#endif

// Tripwire: if anything changes these later, fail the build immediately.
#if TRB_TYPE_XFER_EVT != 32
#error "TRB_TYPE_XFER_EVT must be 32"
#endif
#if TRB_TYPE_CMD_CMPL_EVT != 33
#error "TRB_TYPE_CMD_CMPL_EVT must be 33"
#endif

#define DESC_DEV 1
#define DESC_CONFIG 2
#define DESC_HID 0x21
#define DESC_REPORT 0x22
#define DESC_INTERFACE 4
#define DESC_ENDPOINT 5

volatile uint32_t g_xhci_last_cc = 0;
volatile uint32_t g_xhci_last_ev_epid = 0;
volatile uint32_t g_xhci_last_ev_slot = 0;
volatile uint32_t g_xhci_last_epout = 0;     // endpoint index/number if you have it
volatile uint32_t g_xhci_last_epin = 0;      // endpoint index/number if you have it
volatile uint32_t g_xhci_last_ev_ptr_lo = 0; // TRB pointer low (from Transfer Event)
volatile uint32_t g_xhci_last_ev_len = 0;    // transfer length field (from Transfer Event)

/* ---------- tiny on-screen dots ---------- */
extern volatile uint32_t *g_fb32;
extern const boot_info *k_bootinfo_ptr;
static inline void dot(int n, uint32_t rgb)
{
    if (!g_fb32 || !k_bootinfo_ptr)
        return;
    uint32_t w = k_bootinfo_ptr->fb.width, h = k_bootinfo_ptr->fb.height, gs = k_bootinfo_ptr->fb.pitch / 4;
    uint32_t x = 2 + (uint32_t)(n * 3), y = 2;
    if (x + 1 < w && y + 1 < h)
    {
        g_fb32[y * gs + x] = rgb;
        g_fb32[y * gs + x + 1] = rgb;
        g_fb32[(y + 1) * gs + x] = rgb;
        g_fb32[(y + 1) * gs + x + 1] = rgb;
    }
}

// exported debug dot for other modules
void usbh_dbg_dot(int n, uint32_t rgb)
{
    dot(n, rgb);
}

#define C_S1 0xFFFFFFu
#define C_OK 0x00FF00u
#define C_ER 0xFF0000u
#define C_YL 0xFFFF00u
#define C_CY 0x00FFFFu

static uint64_t g_expect_trbptr = 0;
static uint8_t g_expect_slot = 0;
static uint8_t g_expect_epid = 0;
static uint8_t g_expect_valid = 0;

static volatile uint32_t g_expect_mismatch = 0; // count of ignored Transfer Events
static volatile uint32_t g_expect_last_bad_epid = 0;
static volatile uint32_t g_expect_last_bad_slot = 0;
static volatile uint32_t g_expect_last_bad_ptr_lo = 0;
static volatile uint32_t g_expect_last_bad_ptr_hi = 0;

static volatile uint32_t g_last_xfer_cc = 0;
static volatile uint32_t g_last_xfer_rem = 0;

static void xhci_dbg(const char *msg)
{
    terminal_print(msg);
    kgfx_render_all(black);
}

static void hex2(uint8_t v, char out[3])
{
    static const char *hex = "0123456789ABCDEF";
    out[0] = hex[(v >> 4) & 0xF];
    out[1] = hex[v & 0xF];
    out[2] = 0;
}

static void xhci_dbg_if_line(uint8_t cls, uint8_t sub, uint8_t proto)
{
    char c[3], s[3], p[3];
    hex2(cls, c);
    hex2(sub, s);
    hex2(proto, p);

    char msg[64];
    msg[0] = 0;

    // "IF c=XX s=XX p=XX"
    msg[0] = 'I';
    msg[1] = 'F';
    msg[2] = ' ';
    msg[3] = 'c';
    msg[4] = '=';
    msg[5] = c[0];
    msg[6] = c[1];
    msg[7] = ' ';
    msg[8] = 's';
    msg[9] = '=';
    msg[10] = s[0];
    msg[11] = s[1];
    msg[12] = ' ';
    msg[13] = 'p';
    msg[14] = '=';
    msg[15] = p[0];
    msg[16] = p[1];
    msg[17] = 0;

    terminal_print(msg);
    kgfx_render_all(black);
}

static void xhci_dbg_ep_line(uint8_t addr, uint8_t attr)
{
    char a[3], t[3];
    hex2(addr, a);
    hex2(attr, t);

    char msg[64];
    msg[0] = 0;

    // "EP a=XX t=XX"
    msg[0] = 'E';
    msg[1] = 'P';
    msg[2] = ' ';
    msg[3] = 'a';
    msg[4] = '=';
    msg[5] = a[0];
    msg[6] = a[1];
    msg[7] = ' ';
    msg[8] = 't';
    msg[9] = '=';
    msg[10] = t[0];
    msg[11] = t[1];
    msg[12] = 0;

    terminal_print(msg);
    kgfx_render_all(black);
}

static void xhci_dbg_portsc_line(const char *tag, int port_id, int attempt, uint32_t v)
{
    terminal_print_inline(tag);
    terminal_print_inline(" p=");
    terminal_print_inline_hex32((uint32_t)port_id);
    terminal_print_inline(" try=");
    terminal_print_inline_hex32((uint32_t)attempt);
    terminal_print_inline(" ccs=");
    terminal_print_inline_hex8((uint8_t)((v & PORTSC_CCS) ? 1u : 0u));
    terminal_print_inline(" pp=");
    terminal_print_inline_hex8((uint8_t)((v & PORTSC_PP) ? 1u : 0u));
    terminal_print_inline(" ped=");
    terminal_print_inline_hex8((uint8_t)((v & PORTSC_PED) ? 1u : 0u));
    terminal_print_inline(" pls=");
    terminal_print_inline_hex8((uint8_t)((v & PORTSC_PLS_MASK) >> 5));
    terminal_print_inline(" pr=");
    terminal_print_inline_hex8((uint8_t)((v & PORTSC_PR) ? 1u : 0u));
    terminal_print_inline(" v=");
    terminal_print_inline_hex32(v);
    terminal_print("");
    kgfx_render_all(black);
}

static void xhci_dbg_dump_cfg_brief(const uint8_t *cfg, uint16_t len)
{
    terminal_print("cfg dump: start");
    kgfx_render_all(black);

    uint16_t off = 0;
    while (off + 2 <= len)
    {
        uint8_t L = cfg[off];
        uint8_t T = cfg[off + 1];

        if (!L || off + L > len)
        {
            terminal_warn("cfg dump: bad length");
            kgfx_render_all(black);
            break;
        }

        if (T == DESC_INTERFACE && L >= 9)
        {
            xhci_dbg_if_line(cfg[off + 5], cfg[off + 6], cfg[off + 7]);
        }
        else if (T == DESC_ENDPOINT && L >= 7)
        {
            xhci_dbg_ep_line(cfg[off + 2], cfg[off + 3]);
        }

        off += L;
    }

    terminal_print("cfg dump: end");
    kgfx_render_all(black);
}

static inline void mmio_wmb(void)
{
    __asm__ __volatile__("dsb sy; isb" ::: "memory");
}

static inline void udelay(int n)
{
    for (volatile int i = 0; i < n * 1000; i++)
        __asm__ __volatile__("");
}
static inline void mdelay(int n)
{
    for (int i = 0; i < n; i++)
        udelay(1000);
}

extern void *memset(void *dst, int c, unsigned long n);

/* DMA via your pmem */
extern void *pmem_alloc_pages_lowdma(uint64_t n); // NEW
static int g_xhci_ac64 = 0;                       /* HCCPARAMS1.AC64 (0=32-bit DMA) */
extern void *pmem_alloc_pages(uint64_t n);
static void *alloc_dma(uint64_t bytes)
{
    uint64_t p = (bytes + 4095) >> 12;
    void *v = pmem_alloc_pages_lowdma(p);
    if (v)
        return v;

    if (!g_xhci_ac64)
        return 0;

    return pmem_alloc_pages(p); // 64-bit DMA allowed
}

void *usbh_alloc_dma(uint32_t bytes)
{
    return alloc_dma((uint64_t)bytes);
}

static inline void dma_flush(void *p, uint64_t nbytes)
{
    uintptr_t a = (uintptr_t)p & ~63ull, e = ((uintptr_t)p + nbytes + 63ull) & ~63ull;
    for (; a < e; a += 64)
        __asm__ __volatile__("dc cvac, %0" ::"r"(a) : "memory");
    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
}
static inline void dma_invalidate(void *p, uint64_t nbytes)
{
    uintptr_t a = (uintptr_t)p & ~63ull, e = ((uintptr_t)p + nbytes + 63ull) & ~63ull;
    for (; a < e; a += 64)
        __asm__ __volatile__("dc ivac, %0" ::"r"(a) : "memory");
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
}

/* ---------- global xHCI state ---------- */
typedef struct
{
    xhci_regs_t R;
    uint64_t mmio_base;
    struct
    {
        uint64_t base;
        uint32_t size, enq;
        uint8_t cycle;
    } cmd;
    struct
    {
        uint64_t evt_base;
        uint32_t size, deq;
        uint8_t cycle;
    } ev;
    uint64_t dcbaa, dev_ctx, input_ctx;
    uint64_t ctrl_tr, bulk_in_tr, bulk_out_tr, intr_in_tr;
    uint8_t ctrl_cycle, bin_cycle, bout_cycle, iin_cycle;
    uint32_t ctrl_enq, bin_enq, bout_enq, iin_enq;
    uint32_t n_ports;
    int slot_id, port_id;
} xhci_t;

typedef struct
{
    xhci_t hc;
    uint8_t claimed_ports[256];
    uint8_t ac64;
    uint32_t ctx_dwords;
    uint32_t ctx_stride;
    uint8_t in_use;
} xhci_saved_state_t;

static xhci_t G;
static uint8_t g_claimed_ports[256];
static xhci_saved_state_t g_saved_states[8];
static uint32_t G_ctx_dwords = 16; /* default 64B contexts */
static uint32_t G_ctx_stride = 64; /* bytes per context   */

static void claimed_ports_copy(uint8_t *dst, const uint8_t *src)
{
    for (uint32_t i = 0; i < 256; ++i)
        dst[i] = src[i];
}

static void claimed_ports_clear(void)
{
    for (uint32_t i = 0; i < 256; ++i)
        g_claimed_ports[i] = 0;
}

static xhci_saved_state_t *alloc_saved_state(void)
{
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_saved_states) / sizeof(g_saved_states[0])); ++i)
    {
        if (!g_saved_states[i].in_use)
        {
            g_saved_states[i].in_use = 1;
            return &g_saved_states[i];
        }
    }

    return 0;
}

static int port_is_claimed(uint8_t port_id)
{
    return port_id != 0 && g_claimed_ports[port_id] != 0;
}

static void port_claim(uint8_t port_id)
{
    if (port_id != 0)
        g_claimed_ports[port_id] = 1;
}

static void dev_state_save(usbh_dev_t *d)
{
    xhci_saved_state_t *saved = 0;

    if (!d)
        return;

    if (d->hc && d->hc != &G)
        saved = (xhci_saved_state_t *)(uintptr_t)d->hc;
    else
        saved = alloc_saved_state();

    if (saved)
    {
        saved->hc = G;
        saved->ac64 = (uint8_t)g_xhci_ac64;
        saved->ctx_dwords = G_ctx_dwords;
        saved->ctx_stride = G_ctx_stride;
        claimed_ports_copy(saved->claimed_ports, g_claimed_ports);
        d->hc = saved;
    }

    d->slot_id = (uint8_t)G.slot_id;
    d->port_id = (uint8_t)G.port_id;
    d->devctx = (void *)(uintptr_t)G.dev_ctx;
    d->input_ctx = (void *)(uintptr_t)G.input_ctx;
    d->ctrl_tr = (void *)(uintptr_t)G.ctrl_tr;
    d->bulk_in_tr = (void *)(uintptr_t)G.bulk_in_tr;
    d->bulk_out_tr = (void *)(uintptr_t)G.bulk_out_tr;
    d->intr_in_tr = (void *)(uintptr_t)G.intr_in_tr;
    d->ctrl_enq = G.ctrl_enq;
    d->bin_enq = G.bin_enq;
    d->bout_enq = G.bout_enq;
    d->iin_enq = G.iin_enq;
    d->ctrl_cycle = G.ctrl_cycle;
    d->bin_cycle = G.bin_cycle;
    d->bout_cycle = G.bout_cycle;
    d->iin_cycle = G.iin_cycle;
}

static int dev_state_load(const usbh_dev_t *d)
{
    const xhci_saved_state_t *saved;

    if (!d)
        return -1;

    saved = (const xhci_saved_state_t *)(uintptr_t)d->hc;
    if (saved && saved != (const xhci_saved_state_t *)(uintptr_t)&G && saved->in_use && saved->hc.mmio_base)
    {
        G = saved->hc;
        g_xhci_ac64 = saved->ac64;
        G_ctx_dwords = saved->ctx_dwords;
        G_ctx_stride = saved->ctx_stride;
        claimed_ports_copy(g_claimed_ports, saved->claimed_ports);
        return 0;
    }

    if (d->slot_id == 0 || !d->devctx || !d->input_ctx || !d->ctrl_tr)
        return -1;

    G.slot_id = d->slot_id;
    G.port_id = d->port_id;
    G.dev_ctx = (uint64_t)(uintptr_t)d->devctx;
    G.input_ctx = (uint64_t)(uintptr_t)d->input_ctx;
    G.ctrl_tr = (uint64_t)(uintptr_t)d->ctrl_tr;
    G.bulk_in_tr = (uint64_t)(uintptr_t)d->bulk_in_tr;
    G.bulk_out_tr = (uint64_t)(uintptr_t)d->bulk_out_tr;
    G.intr_in_tr = (uint64_t)(uintptr_t)d->intr_in_tr;
    G.ctrl_enq = d->ctrl_enq;
    G.bin_enq = d->bin_enq;
    G.bout_enq = d->bout_enq;
    G.iin_enq = d->iin_enq;
    G.ctrl_cycle = d->ctrl_cycle;
    G.bin_cycle = d->bin_cycle;
    G.bout_cycle = d->bout_cycle;
    G.iin_cycle = d->iin_cycle;

    return 0;
}

/* ---- CTX types ---- */
#pragma pack(push, 16)
typedef struct
{
    uint32_t d[16];
} ctx_slot_t; // 64B
typedef struct
{
    uint32_t d[16];
} ctx_ep_t; // 64B
typedef struct
{
    uint32_t drop_flags, add_flags;
    uint64_t rsvd;
    ctx_slot_t slot;
    ctx_ep_t ep[31];
} input_ctx_t;
#pragma pack(pop)

/* ---- TRB + helpers ---- */
typedef struct
{
    uint32_t d0, d1, d2, d3;
} trb_t;
static trb_t *ring_next(trb_t *ring, uint32_t size, uint32_t *enq, uint8_t *cycle)
{
    uint32_t i = *enq;

    if (i >= size - 1)
    {
        trb_t *link = &ring[size - 1];

        // Program LINK TRB with current producer cycle, TC=1
        link->d3 = (TRB_TYPE_LINK << 10) | (1u << 1) | ((*cycle) & 1u);

        // Wrap to start, consume ring[0], and TOGGLE producer cycle.
        // The next enqueue must advance to ring[1], otherwise we keep
        // reusing ring[0] forever after the first wrap.
        *enq = 1;
        *cycle ^= 1u;
        return &ring[0];
    }

    *enq = i + 1;
    return &ring[i];
}

static inline void mmio_fence(void)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static void ring_cmd_db(void)
{
    // Doorbell array is in runtime regs. DB[0] rings the command ring.
    // On ARM, make sure TRB writes + cache flush are visible before MMIO write.
    mmio_fence();
    G.R.doorbell[0] = 0;
    mmio_fence();
}

static void ring_ep_db(uint8_t slot, uint8_t epid)
{
    // dot 210 = entered
    dot(210, 0xFFFFFFu);

    // sanity dots
    dot(212, (slot ? 0x00FFFFu : 0xFF00FFu)); // slot nonzero?
    dot(213, (epid ? 0x00FF00u : 0xFF0000u)); // epid nonzero?

    // xHCI doorbell array base was already computed at init:
    // G.R.doorbell = (volatile uint32_t *)(mmio + dboff);
    volatile uint32_t *db = G.R.doorbell;

    // Value: bits 0..7 = target (endpoint DCI), bits 8..15 = stream ID (0)
    uint32_t v = (uint32_t)epid;

    // IMPORTANT:
    // Doorbells are effectively write-only. Do NOT read back (undefined).
    db[slot] = v;

    mmio_wmb();

    // dot 211 = write issued (green)
    dot(211, 0x00FF00u);
}

static uint8_t dci_from_ep(uint8_t ep, int in)
{
    // ep might be a raw bEndpointAddress (e.g. 0x81, 0x02).
    // xHCI DCI needs the endpoint NUMBER only (1..15), not the direction bit.
    uint8_t epn = (uint8_t)(ep & 0x0Fu);

    // DCI: 2*epn for OUT, 2*epn+1 for IN. (EP0 IN is DCI=1; EP0 OUT is DCI=0)
    uint8_t dci = (uint8_t)((epn << 1) | (in ? 1 : 0));

    // Optional sanity dot: red if nonsense (should be <= 31)
    if (dci > 31)
        usbh_dbg_dot(168, 0xFF0000u);
    else
        usbh_dbg_dot(168, 0x00FF00u);

    return dci;
}

/* forward declaration to avoid implicit declaration error */
static int wait_cmd_complete(uint32_t ms, trb_t *out);

/* --- ERDP write helper: re-arm with EHB=1 and DCS=G.ev.cycle ----------- */
static inline void evring_write_erdp(void)
{
    uintptr_t rt = (uintptr_t)(G.mmio_base + (G.R.cap->RTSOFF & ~0x1Fu));
    uintptr_t int0 = rt + 0x20u; /* <-- Interrupter 0 starts at +0x20 */

    volatile uint64_t *ERDP = (volatile uint64_t *)(int0 + 0x18);

    uint64_t deq = (uint64_t)(uintptr_t)G.ev.evt_base + ((uint64_t)G.ev.deq * sizeof(trb_t));

    *ERDP = deq | (1ull << 3); /* EHB=1 */
    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
}

/* --- sanity check: send a No-Op Command to confirm DMA + rings --- */
static int issue_noop_cmd(void)
{
    /* re-arm ERDP before we start waiting (some xHCs want this *after* RS as well) */
    evring_write_erdp();

    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);

    *t = (trb_t){
        .d0 = 0,
        .d1 = 0,
        .d2 = 0,
        .d3 = (TRB_TYPE_NOOP_CMD << 10) | (G.cmd.cycle & 1)};

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    /* tiny settle helps on some ARM xHCs */
    mdelay(2);

    /* Wait up to 2 s for a completion event */
    if (wait_cmd_complete(2000, NULL) == 0)
    {
        dot(9, 0x00A0FFu); /* blue: rings + DMA WORKING */
        return 0;
    }

    /* draw a clear timeout triad to confirm we actually waited */
    uint32_t usbsts = G.R.op->USBSTS;
    dot(9, C_ER);                                /* red = no completion */
    dot(10, (usbsts & 1u) ? C_ER : C_OK);        /* HCH==0 → green */
    dot(11, (usbsts & (1u << 3)) ? C_OK : C_ER); /* EINT set → green */
    mdelay(500);
    return -1;
}

/* --- wait helpers -------------------------------------------------------- */
/* --- wait for an event (type or any) ------------------------------------ */
/* Return 0 on match and copy the TRB into *out if provided */
static void dot_digit(uint32_t dotno, uint32_t digit)
{
    static const uint32_t pal[10] = {
        0x000000u, // 0 none/black
        0xFFFFFFu, // 1 white
        0xFFFF00u, // 2 yellow
        0x00FF00u, // 3 green
        0x00FFFFu, // 4 cyan
        0x0000FFu, // 5 blue
        0xFF0000u, // 6 red
        0xFF00FFu, // 7 magenta
        0xFFA500u, // 8 orange
        0x808080u  // 9 grey
    };
    dot(dotno, pal[digit % 10u]);
}

static void dot_u8_as_2digits(uint32_t base_dot, uint32_t v)
{
    uint32_t tens = (v / 10u) % 10u;
    uint32_t ones = v % 10u;
    dot_digit(base_dot + 0, tens);
    dot_digit(base_dot + 1, ones);
}

static int wait_event_type(uint32_t expect_type, uint32_t timeout_ms, trb_t *out)
{
    trb_t *ring = (trb_t *)(uintptr_t)G.ev.evt_base;
    uint32_t idx = G.ev.deq;
    uint8_t ccs = G.ev.cycle;

    uint32_t ticks = timeout_ms ? timeout_ms : 12000;

    dot(182, 0xFFFF00u); /* enter */

    /* show what we're expecting: 32=cyn, 33=grn, else red */
    if (expect_type == 32u)
        dot(195, 0x00FFFFu);
    else if (expect_type == 33u)
        dot(195, 0x00FF00u);
    else
        dot(195, 0xFF0000u);

    for (uint32_t t = 0; t < ticks; t++)
    {
        trb_t *e = &ring[idx];

        /* CRITICAL: event ring is DMA-written by xHCI, so invalidate before reading */
        dma_invalidate(e, sizeof(*e));

        trb_t cur = *e; // snapshot this event TRB *now*
        uint32_t d3 = cur.d3;

        uint8_t cyc = (uint8_t)(d3 & 1u);

        if (cyc == ccs)
        {
            uint32_t type = (d3 >> 10) & 0x3Fu;

            /* Type beacon */
            if (type == 32u)
                dot(194, 0x00FFFFu); /* Transfer Event */
            else if (type == 33u)
                dot(194, 0x00FF00u); /* Cmd Completion */
            else
                dot(194, 0xFF0000u);

            dot(183, 0x00FF00u); /* event present */

            dot_u8_as_2digits(192, type);

            if (out)
                *out = *e;

            /* consume */
            idx++;
            if (idx >= G.ev.size)
            {
                idx = 0;
                ccs ^= 1;
            }
            G.ev.deq = idx;
            G.ev.cycle = ccs;
            evring_write_erdp();

            /* If transfer event, show CC on dots 190/191 */
            if (type == 32u)
            {
                dot(184, 0x00FFFFu); /* xfer evt */
                uint32_t cc = (cur.d2 >> 24) & 0xFFu;
                dot_u8_as_2digits(190, cc);

                g_xhci_last_cc = cc;
                g_xhci_last_ev_epid = (cur.d3 >> 16) & 0x1Fu; // Endpoint ID from Transfer Event
                g_xhci_last_ev_slot = (cur.d3 >> 24) & 0xFFu; // Slot ID from Transfer Event
                g_xhci_last_ev_ptr_lo = cur.d0;
                g_xhci_last_ev_len = cur.d2 & 0x00FFFFFFu; // this is "remaining", not "done"

                // If we are waiting for a specific bulk TRB, ignore unrelated transfer events.
                if (expect_type == 32u && g_expect_valid)
                {
                    uint8_t ev_epid = (uint8_t)((cur.d3 >> 16) & 0x1Fu);
                    uint8_t ev_slot = (uint8_t)((cur.d3 >> 24) & 0xFFu);
                    uint64_t ev_ptr = ((uint64_t)cur.d1 << 32) | (uint64_t)cur.d0;

                    // Mask to 16-byte TRB alignment (defensive)
                    uint64_t ev_ptr_m = ev_ptr & ~0xFULL;
                    uint64_t exp_ptr_m = g_expect_trbptr & ~0xFULL;

                    if (ev_slot != g_expect_slot || ev_epid != g_expect_epid || ev_ptr_m != exp_ptr_m)
                    {
                        // Record why we rejected it (so your overlay is meaningful)
                        g_expect_mismatch++;
                        if (ev_slot != g_expect_slot)
                            g_expect_last_bad_slot++;
                        if (ev_epid != g_expect_epid)
                            g_expect_last_bad_epid++;

                        // Store the last "wrong" pointer low/high for display
                        g_expect_last_bad_ptr_lo = (uint32_t)ev_ptr_m;
                        g_expect_last_bad_ptr_hi = (uint32_t)(ev_ptr_m >> 32);

                        continue;
                    }
                }

                if (cc == 1u)
                    dot(185, 0x00FF00u); /* success */
                else if (cc == 6u)
                    dot(186, 0xFF0000u); /* stall */
                else if (cc == 13u)
                    dot(186, 0xFF00FFu); /* short packet */
                else
                    dot(186, 0xFFFF00u); /* other */
            }

            if (expect_type == 0xFFFFFFFFu || type == expect_type)
            {
                dot(187, 0x00FF00u); /* matched expected type */
                return 0;
            }

            dot(188, 0xFF0000u); /* not expected type */
        }

        udelay(1000);
    }

    dot(189, 0xFF0000u); /* timeout */
    return -1;
}

static int wait_cmd_complete(uint32_t ms, trb_t *out)
{
    dot(178, 0x0000FFu);                  /* wait_cmd_complete called */
    return wait_event_type(33u, ms, out); /* 33 = Command Completion Event */
}

// NOTE:
//   0  = transfer completed successfully (including Short Packet)
//   1  = timed out waiting for a Transfer Event
//  -1  = Transfer Event arrived but completed with an error CC
//
// Exact-length policy is decided by the caller.
static int wait_xfer_complete(uint32_t ms)
{
    dot(180, 0x00FFFFu); /* wait_xfer_complete called */

    trb_t evt;
    int r = wait_event_type(32u, ms, &evt); /* 32 = Transfer Event */
    dot(r == 0 ? 181 : 189, r == 0 ? 0x00FF00u : 0xFF0000u);

    if (r != 0)
        return 1;

    // Transfer Event TRB:
    // d2[23:0]  = Transfer Length Remaining
    // d2[31:24] = Completion Code
    uint32_t rem = evt.d2 & 0x00FFFFFFu;
    uint32_t cc = (evt.d2 >> 24) & 0xFFu;

    g_last_xfer_cc = cc;
    g_last_xfer_rem = rem;

    // 1 = Success, 13 = Short Packet (valid for IN/interrupt transfers)
    if (cc != 1u && cc != 13u)
        return -1;

    return 0;
}

/* Always 64 bytes for the Input Control Context, regardless of CSZ */
#define ICC_BYTES 64

/* Wait for a CMD Completion Event and render dots 19..21 + 30..34 */
static int do_address_device(int bsr /* 0 or 1 */)
{
    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);

    t->d0 = (uint32_t)G.input_ctx;
    t->d1 = (uint32_t)(G.input_ctx >> 32);
    t->d2 = bsr ? (1u << 9) : 0u; /* BSR */
    t->d3 = (TRB_TYPE_ADDR_DEVICE << 10) | (G.cmd.cycle & 1) | ((G.slot_id & 0xFF) << 24);

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    trb_t evt;
    if (wait_cmd_complete(12000, &evt))
    {
        uint32_t usbsts = G.R.op->USBSTS;
        dot(10, C_ER);
        dot(11, (usbsts & 1u) ? C_ER : C_OK);
        dot(12, (usbsts & (1u << 3)) ? C_OK : C_ER);
        mdelay(800);
        return -1;
    }

    uint32_t cc = (evt.d2 >> 24) & 0xFF;
    uint8_t evt_slot = (evt.d3 >> 24) & 0xFF;

    dot(30, ((cc >> 4) ? C_ER : C_OK));
    dot(31, (cc == 1) ? C_OK : C_ER);
    dot(32, (evt_slot == (uint8_t)G.slot_id) ? C_OK : C_ER);

    /* palette for CC < 16 */
    static const uint32_t pal[16] = {
        0x000000u, 0x00FF00u, 0xFF0000u, 0xFFFF00u,
        0x00FFFFu, 0xFF00FFu, 0xFF8800u, 0x0080FFu,
        0xFFFFFFu, 0x00FF80u, 0xFF8080u, 0x8080FFu,
        0x80FF80u, 0xFFCC00u, 0xCCCCCCu, 0x404040u};
    dot(34, (cc < 16) ? pal[cc] : 0xFFFFFFu);

    if (cc == 1)
    {
        dot(33, 0x00FF00);
        return 0;
    }

    /* ---- NEW: dump live PORTSC info when it fails ---- */
    uint32_t ps = G.R.ports[G.port_id - 1].PORTSC;
    uint32_t pls = (ps & PORTSC_PLS_MASK) >> 5;
    uint32_t spd = (ps >> PORTSC_SPEED_SHIFT) & 0xF;

    dot(35, (ps & PORTSC_CCS) ? C_OK : C_ER); /* connected? */
    dot(36, (ps & PORTSC_PED) ? C_OK : C_ER); /* enabled? */
    dot(37, (ps & PORTSC_PR) ? C_ER : C_OK);  /* still in reset? */
    dot(38, (pls == 0) ? C_OK : C_ER);        /* U0? (pls==0) */
    dot(39, (spd << 20) | 0x0000FFu);         /* speed nibble, visible */

    return -1;
}

/* ===== ACPI → ECAM fallback (simple scanner) ===== */
#pragma pack(push, 1)
typedef struct
{
    char s[8];
} sig8;
typedef struct
{
    char s[4];
} sig4;
typedef struct
{
    sig8 Sig;
    uint8_t Chk;
    char OEMID[6];
    uint8_t Rev;
    uint32_t Rsdt, Length;
    uint64_t Xsdt;
    uint8_t ExtChk;
    uint8_t rsvd[3];
} acpi_rsdp;
typedef struct
{
    sig4 Sig;
    uint32_t Length;
    uint8_t Rev, Chk;
    char OEMID[6], OEMTID[8];
    uint32_t OEMRev, CreatorID, CreatorRev;
} acpi_sdt;
typedef struct
{
    acpi_sdt Hdr;
    uint64_t Entry[];
} acpi_xsdt;
typedef struct
{
    acpi_sdt Hdr;
    uint64_t Reserved;
    struct
    {
        uint64_t Base;
        uint16_t Seg;
        uint8_t BusStart, BusEnd;
        uint32_t _r;
    } entry[];
} acpi_mcfg;
#pragma pack(pop)

static int find_xhci_mmio_from_acpi(uint64_t rsdp_pa, uint64_t *mmio_out)
{
    if (!rsdp_pa)
        return -1;
    acpi_rsdp *R = (acpi_rsdp *)(uintptr_t)rsdp_pa;
    acpi_xsdt *X = (acpi_xsdt *)(uintptr_t)R->Xsdt;
    if (!X)
        return -1;
    uint32_t n = (X->Hdr.Length - sizeof(acpi_sdt)) / 8;
    acpi_mcfg *M = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        acpi_sdt *s = (acpi_sdt *)(uintptr_t)X->Entry[i];
        if (s->Sig.s[0] == 'M' && s->Sig.s[1] == 'C' && s->Sig.s[2] == 'F' && s->Sig.s[3] == 'G')
        {
            M = (acpi_mcfg *)s;
            break;
        }
    }
    if (!M)
        return -1;
    uint32_t segs = (M->Hdr.Length - sizeof(acpi_mcfg)) / 16;
    for (uint32_t e = 0; e < segs; e++)
    {
        uint64_t ecam = M->entry[e].Base;
        for (uint32_t bus = M->entry[e].BusStart; bus <= M->entry[e].BusEnd; bus++)
            for (uint32_t dev = 0; dev < 32; dev++)
                for (uint32_t fn = 0; fn < 8; fn++)
                {
                    uintptr_t cfg = (uintptr_t)(ecam + ((uint64_t)bus << 20) + ((uint64_t)dev << 15) + ((uint64_t)fn << 12));
                    uint32_t vid = *(volatile uint32_t *)(cfg + 0x00);
                    if ((vid & 0xFFFF) == 0xFFFF)
                        continue;
                    uint32_t classdw = *(volatile uint32_t *)(cfg + 0x08);
                    if (((classdw >> 24) & 0xFF) == 0x0C && ((classdw >> 16) & 0xFF) == 0x03 && ((classdw >> 8) & 0xFF) == 0x30)
                    {
                        uint32_t bar0 = *(volatile uint32_t *)(cfg + 0x10), bar1 = *(volatile uint32_t *)(cfg + 0x14);
                        *mmio_out = ((uint64_t)bar1 << 32) | (bar0 & ~0xFu);
                        return 0;
                    }
                }
    }
    return -1;
}

/* ===== map regs → dot #1, reset/start → dot #2 ===== */
static int xhci_map_regs(uint64_t mmio)
{
    dot(1, C_S1);
    G.mmio_base = mmio;
    xhci_caps_t *cap = (xhci_caps_t *)(uintptr_t)mmio;
    uint32_t caplen = cap->CAPLENGTH_HCIVERSION & 0xFF;
    g_xhci_ac64 = (cap->HCCPARAMS1 & 1u) ? 1 : 0;
    xhci_op_t *op = (xhci_op_t *)(uintptr_t)(mmio + caplen);

    uint32_t dboff = cap->DBOFF & ~0x3u, rtsoff = cap->RTSOFF & ~0x1Fu;
    G.R.cap = cap;
    G.R.op = op;
    G.R.doorbell = (volatile uint32_t *)(uintptr_t)(mmio + dboff);
    G.R.ports = (xhci_port_t *)((uintptr_t)op + 0x400);
    G.n_ports = (cap->HCSPARAMS1 >> 24) & 0xFF;
    (void)rtsoff; /* runtime calc is done later */
    return 0;
}
#ifndef USBSTS_HCH
#define USBSTS_HCH (1u << 0)
#endif
#ifndef USBSTS_CNR
#define USBSTS_CNR (1u << 11)
#endif
#ifndef USBCMD_RS
#define USBCMD_RS (1u << 0)
#endif
#ifndef USBCMD_HCRST
#define USBCMD_HCRST (1u << 1)
#endif

static int wait_halted(uint32_t timeout_ms)
{
    /* Wait until USBSTS.HCH (HCHalted) becomes 1 */
    for (uint32_t ms = 0; ms < timeout_ms; ms++)
    {
        if (G.R.op->USBSTS & USBSTS_HCH)
            return 0;
        udelay(1000);
    }
    return -1;
}

static int xhci_reset_start(void)
{
    // Stop controller, reset, then run.
    // Ordering matters on ARM: fence after MMIO writes.

    // Stop (clear RS)
    uint32_t cmd = G.R.op->USBCMD;
    cmd &= ~USBCMD_RS;
    G.R.op->USBCMD = cmd;
    mmio_fence();

    // Wait until HCHalted=1
    if (wait_halted(2000))
        return -1;

    // Reset (set HCRST)
    cmd = G.R.op->USBCMD;
    cmd |= USBCMD_HCRST;
    G.R.op->USBCMD = cmd;
    mmio_fence();

    // Wait for HCRST to clear
    for (uint32_t ms = 0; ms < 2000; ms++)
    {
        uint32_t c = G.R.op->USBCMD;
        if ((c & USBCMD_HCRST) == 0)
            break;
        udelay(1000);
        if (ms == 1999)
            return -1;
    }

    // Wait until HCHalted=1 again (post-reset)
    if (wait_halted(2000))
        return -1;

    return 0;
}

/* --- ring initializers --------------------------------------------------- */
static void ring_init_xfer_or_cmd(uint64_t *base, uint32_t *size)
{
    const uint32_t N = 256;
    trb_t *r = (trb_t *)alloc_dma((uint64_t)N * sizeof(trb_t));
    if (!r)
    {
        *base = 0;
        *size = 0;
        return;
    }
    for (uint32_t i = 0; i < N; i++)
        r[i] = (trb_t){0, 0, 0, 0};

    /* Link TRB must point back to ring base and have TC=1 (bit1).
       Set the Cycle bit to the ring’s initial producer cycle (you use 1). */
    uint64_t p = (uint64_t)(uintptr_t)r;
    r[N - 1].d0 = (uint32_t)p;
    r[N - 1].d1 = (uint32_t)(p >> 32);
    r[N - 1].d2 = 0;
    r[N - 1].d3 = (TRB_TYPE_LINK << 10) | (1u << 1) | 1u; /* TC=1, CYCLE=1 */

    *base = (uint64_t)(uintptr_t)r;
    *size = N;
    dma_flush(r, (uint64_t)N * sizeof(trb_t));
}

static void ring_init_event(uint64_t *base, uint32_t *size)
{
    const uint32_t N = 256;
    trb_t *r = (trb_t *)alloc_dma((uint64_t)N * sizeof(trb_t));
    if (!r)
    {
        *base = 0;
        *size = 0;
        return;
    }
    for (uint32_t i = 0; i < N; i++)
        r[i] = (trb_t){0, 0, 0, 0};
    *base = (uint64_t)(uintptr_t)r;
    *size = N;
    dma_flush(r, (uint64_t)N * sizeof(trb_t));
}

/* --- constants for ports/interrupter ------------------------------------ */
#ifndef IMAN_IP
#define IMAN_IP (1u << 0)
#endif
#ifndef IMAN_IE
#define IMAN_IE (1u << 1)
#endif
#ifndef USBCMD_INTE
#define USBCMD_INTE (1u << 2)
#endif
#ifndef PORTSC_PP
#define PORTSC_PP (1u << 9)
#endif
#ifndef PORTSC_CCS
#define PORTSC_CCS (1u << 0)
#endif
#ifndef PORTSC_PR
#define PORTSC_PR (1u << 4)
#endif
#ifndef PORTSC_CSC
#define PORTSC_CSC (1u << 17)
#endif
#ifndef PORTSC_PEC
#define PORTSC_PEC (1u << 18)
#endif
#ifndef PORTSC_PRC
#define PORTSC_PRC (1u << 21)
#endif

typedef struct
{
    uint64_t seg_base;
    uint32_t seg_size;
    uint32_t rsvd;
} __attribute__((packed)) xhci_erst_ent_t;

static int init_event_ring(void)
{
    // Allocate Event Ring segment (TRBs) and ERST (1 entry).
    // NOTE: alloc_dma() returns a *virtual* address we can access.
    // We program the controller with the same address because your bring-up
    // is currently identity-mapped for low DMA.

    void *erst_v = alloc_dma(4096);
    void *ers_v = alloc_dma(4096);

    if (!erst_v || !ers_v)
        return -1;

    uint64_t erst = (uint64_t)(uintptr_t)erst_v;
    uint64_t ers = (uint64_t)(uintptr_t)ers_v;

    // Clear ring + ERST
    memset(erst_v, 0, 4096);
    memset(ers_v, 0, 4096);

    // Event ring segment state
    G.ev.evt_base = ers;
    G.ev.size = (uint32_t)(4096 / sizeof(trb_t));
    G.ev.deq = 0;
    G.ev.cycle = 1; // CCS starts at 1

    // ERST entry: { RingSegmentBase, RingSegmentSize }
    xhci_erst_ent_t *E = (xhci_erst_ent_t *)erst_v;
    E[0].seg_base = ers;
    E[0].seg_size = G.ev.size;

    // Flush DMA structures before telling controller
    dma_flush(erst_v, 4096);
    dma_flush(ers_v, 4096);

    // Program Interrupter 0 registers (Runtime offset + 0x20)
    uintptr_t rt = (uintptr_t)(G.mmio_base + (G.R.cap->RTSOFF & ~0x1Fu));
    uintptr_t int0 = rt + 0x20u;
    volatile xhci_intr_regs_t *ir0 = (volatile xhci_intr_regs_t *)int0;

    // Disable interrupts while programming (clear IE)
    uint32_t cur = ir0->IMAN;
    ir0->IMAN = (cur & ~IMAN_IE);
    mmio_fence();

    // ERST size = 1 entry
    ir0->ERSTSZ = 1;
    mmio_fence();

    // ERST base address
    ir0->ERSTBA = erst;
    mmio_fence();

    // Event Ring Dequeue Pointer (start of segment). Also clear EHB.
    ir0->ERDP = ers;
    mmio_fence();

    // IMOD optional; set to 0 for “no moderation”
    ir0->IMOD = 0;
    mmio_fence();

    // Clear pending IP then enable IE (preserving other bits)
    cur = ir0->IMAN;
    ir0->IMAN = (cur | IMAN_IP); // RW1C clear
    mmio_fence();
    cur = ir0->IMAN;
    ir0->IMAN = (cur | IMAN_IE | IMAN_IP); // enable + clear any pending
    mmio_fence();

    return 0;
}

static int alloc_low_dma_page_test(void)
{
    /* Not a “test” so much as “make sure we can get low DMA”. */
    void *p = pmem_alloc_pages_lowdma(1);
    if (!p)
        return -1;

    /* If your allocator ever returns something >=4GB here, you want to know. */
    if (((uint64_t)(uintptr_t)p) >= 0x100000000ull)
        return -1;

    return 0;
}

static int enable_irqs_and_run(void)
{
    /* Allocate DCBAA (256 entries is fine for bring-up) */
    uint64_t *dcbaa = (uint64_t *)alloc_dma(256ull * sizeof(uint64_t));
    if (!dcbaa)
        return -1;

    for (int i = 0; i < 256; i++)
        dcbaa[i] = 0;

    dma_flush(dcbaa, 256ull * sizeof(uint64_t));
    G.dcbaa = (uint64_t)(uintptr_t)dcbaa;

    /* Wire core pointers */
    G.R.op->DCBAAP = (uint64_t)(uintptr_t)dcbaa;

    /* CRCR: command ring base + RCS (bit0) */
    G.R.op->CRCR = ((uint64_t)G.cmd.base & ~0x3Full) | 1ull;

    /* CONFIG: MaxSlotsEn (use at least 8, but not more than controller supports) */
    uint32_t maxslots = (G.R.cap->HCSPARAMS1 & 0xFFu);
    if (maxslots == 0)
        maxslots = 8;
    if (maxslots > 32)
        maxslots = 32;
    if (maxslots < 8)
        maxslots = 8;

    G.R.op->CONFIG = maxslots;

    /* Enable interrupts globally + Run */
    G.R.op->USBCMD |= (USBCMD_INTE | USBCMD_RS);
    mmio_wmb();

    /* Wait for HCH to clear */
    for (int i = 0; i < 5000; i++)
    {
        if ((G.R.op->USBSTS & USBSTS_HCH) == 0)
            return 0;
        mdelay(1);
    }

    return -1;
}

static uint64_t pci_bar_mmio_base(uintptr_t cfg)
{
    uint32_t bar0 = *(volatile uint32_t *)(cfg + 0x10);
    uint32_t bar1 = *(volatile uint32_t *)(cfg + 0x14);

    /* ignore I/O bars */
    if (bar0 & 1u)
        return 0;

    /* 64-bit MMIO bar? (bits 2:1 == 10b) */
    if ((bar0 & 0x6u) == 0x4u)
        return (((uint64_t)bar1 << 32) | (bar0 & ~0xFu));

    /* 32-bit MMIO bar */
    return ((uint64_t)(bar0 & ~0xFu));
}

static int pci_enable_busmaster_for_mmio(uint64_t rsdp_pa, uint64_t want_mmio)
{
    if (!rsdp_pa || !want_mmio)
        return -1;

    acpi_rsdp *R = (acpi_rsdp *)(uintptr_t)rsdp_pa;
    acpi_xsdt *X = (acpi_xsdt *)(uintptr_t)R->Xsdt;
    if (!X)
        return -1;

    uint32_t n = (X->Hdr.Length - sizeof(acpi_sdt)) / 8;
    acpi_mcfg *M = 0;

    for (uint32_t i = 0; i < n; i++)
    {
        acpi_sdt *s = (acpi_sdt *)(uintptr_t)X->Entry[i];
        if (s->Sig.s[0] == 'M' && s->Sig.s[1] == 'C' && s->Sig.s[2] == 'F' && s->Sig.s[3] == 'G')
        {
            M = (acpi_mcfg *)s;
            break;
        }
    }
    if (!M)
        return -1;

    uint64_t want = (want_mmio & ~0xFull);

    /* each MCFG entry is 16 bytes in your packed struct */
    uint32_t segs = (M->Hdr.Length - sizeof(acpi_mcfg)) / 16;

    for (uint32_t si = 0; si < segs; si++)
    {
        uint64_t ecam = M->entry[si].Base;
        uint8_t b0 = M->entry[si].BusStart, b1 = M->entry[si].BusEnd;

        for (uint32_t bus = b0; bus <= b1; bus++)
            for (uint32_t dev = 0; dev < 32; dev++)
                for (uint32_t fn = 0; fn < 8; fn++)
                {
                    uintptr_t cfg = (uintptr_t)(ecam + ((uint64_t)bus << 20) + ((uint64_t)dev << 15) + ((uint64_t)fn << 12));

                    uint32_t id = *(volatile uint32_t *)(cfg + 0x00);
                    if ((id & 0xFFFFu) == 0xFFFFu)
                        continue;

                    /* match by BAR base */
                    uint64_t bar_mmio = pci_bar_mmio_base(cfg);
                    if ((bar_mmio & ~0xFull) != want)
                        continue;

                    /* PCI COMMAND (offset 0x04): bit1=MEM, bit2=BUSMASTER */
                    volatile uint16_t *cmd = (volatile uint16_t *)(cfg + 0x04);
                    uint16_t v = *cmd;
                    v |= (1u << 1); /* Memory Space */
                    v |= (1u << 2); /* Bus Master */
                    *cmd = v;

                    /* make sure the write lands */
                    __asm__ __volatile__("dsb sy; isb" ::: "memory");
                    return 0;
                }
    }

    return -1;
}

/* Split-out: command ring init (CRCR) */
static int init_cmd_ring(void)
{
    ring_init_xfer_or_cmd(&G.cmd.base, &G.cmd.size);
    G.cmd.enq = 0;
    G.cmd.cycle = 1;

    if (!G.cmd.base || !G.cmd.size)
        return -1;

    /* CRCR: ring base | RCS (bit0) */
    xhci_write64(&G.R.op->CRCR, (uint64_t)G.cmd.base | 1u);
    return 0;
}

/* Split-out: DCBAA + scratchpads (DCBAAP) */
static int init_dcbaa_and_scratch(void)
{
    /* DCBAA must be 256 entries */
    uint64_t *dcbaa = (uint64_t *)alloc_dma(256 * sizeof(uint64_t));
    if (!dcbaa)
        return -1;

    for (uint32_t i = 0; i < 256; i++)
        dcbaa[i] = 0;

    /* Scratchpads: HCSPARAMS2[31:27] */
    uint32_t hcs2 = G.R.cap->HCSPARAMS2;
    uint32_t n_sp = (hcs2 >> 27) & 0x1Fu;

    if (n_sp)
    {
        uint64_t *spa = (uint64_t *)alloc_dma((uint64_t)n_sp * 8u);
        if (!spa)
            return -1;

        for (uint32_t i = 0; i < n_sp; i++)
        {
            void *pg = alloc_dma(4096);
            if (!pg)
                return -1;

            spa[i] = (uint64_t)(uintptr_t)pg;
            dma_flush(pg, 4096);
        }

        dma_flush(spa, (uint64_t)n_sp * 8u);
        dcbaa[0] = (uint64_t)(uintptr_t)spa;
    }

    G.dcbaa = (uint64_t)(uintptr_t)dcbaa;
    dma_flush(dcbaa, 256 * sizeof(uint64_t));

    xhci_write64(&G.R.op->DCBAAP, G.dcbaa);
    return 0;
}

/* ===== public bring-up (dots #3..#7) ===== */
int usbh_init(uint64_t xhci_mmio_hint, uint64_t acpi_rsdp_hint)
{
    dot(1, 0xFFFFFF);

    uint64_t mmio = xhci_mmio_hint;
    if (!mmio && acpi_rsdp_hint)
        find_xhci_mmio_from_acpi(acpi_rsdp_hint, &mmio);

    if (!mmio)
    {
        dot(2, C_ER);
        return -1;
    }

    if (G.mmio_base == mmio && G.cmd.base && G.ev.evt_base && G.dcbaa)
    {
        dot(7, C_OK);
        return 0;
    }

    /* quick sanity: CAPLENGTH in [0x20..0x40], HCIVERSION major == 0x01 */
    {
        volatile const xhci_caps_t *c = (volatile const xhci_caps_t *)(uintptr_t)mmio;
        uint8_t caplen = (uint8_t)(c->CAPLENGTH_HCIVERSION & 0xFF);
        uint16_t hciver = (uint16_t)((c->CAPLENGTH_HCIVERSION >> 16) & 0xFFFF);
        uint8_t maxports = (uint8_t)((c->HCSPARAMS1 >> 24) & 0xFF);

        int ok = (caplen >= 0x20 && caplen <= 0x40) &&
                 ((hciver & 0xFF00) == 0x0100) &&
                 (maxports != 0);

        dot(17, ok ? C_OK : C_ER);
        if (!ok)
            return -1;
    }

    if (xhci_map_regs(mmio))
        return -1;

    claimed_ports_clear();

    dot(2, C_YL);

    /* Reset + start (your file may call either name; alias exists above) */
    if (xhci_reset_start())
        return -1;
    dot(4, C_S1);

    /* Command ring */
    if (init_cmd_ring())
        return -1;
    dot(5, C_S1);

    /* Event ring (your existing code/impl must exist) */
    if (init_event_ring())
        return -1;

    dot(8, 0x00FFFF);

    /* DCBAA (+ scratchpads if required) */
    if (init_dcbaa_and_scratch())
        return -1;

    /* enable enough slots for boot media + one HID device, then run */
    {
        uint32_t max_slots = G.R.cap->HCSPARAMS1 & 0xFFu;
        uint32_t cfg_slots;
        if (!max_slots)
            return -1;

        cfg_slots = (max_slots >= 2u) ? 2u : 1u;

        G.R.op->CONFIG = (G.R.op->CONFIG & ~0xFFu) | cfg_slots;
        mmio_wmb();

        /* Run controller (your existing helper) */
        if (enable_irqs_and_run())
            return -1;
    }

    dot(6, C_OK);

    /* prove command/event path works */
    if (issue_noop_cmd())
        return -1;

    dot(7, C_OK);
    dot(9, 0x00A0FFu);
    return 0;
}

/* Evaluate Context to change EP0 MPS (uses ICC=64B layout) */
static int ep0_evaluate_context_set_mps(uint16_t mps0)
{
    uint8_t *in = (uint8_t *)(uintptr_t)G.input_ctx;
    if (!in)
        return -1;

    uint32_t *icc = (uint32_t *)(in + 0);
    uint32_t *ep0 = (uint32_t *)(in + ICC_BYTES + (1u * G_ctx_stride));

    icc[0] = 0;
    icc[1] = (1u << 1); /* A1 only */

    /* EP0 DW1 contains MPS in bits 31:16 */
    ep0[1] &= ~(0xFFFFu << 16);
    ep0[1] |= ((uint32_t)mps0 << 16);

    dma_flush(in, 4096);
    mmio_wmb();

    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);

    t->d0 = (uint32_t)G.input_ctx;
    t->d1 = (uint32_t)(G.input_ctx >> 32);
    t->d2 = 0;
    t->d3 = (TRB_TYPE_EVALUATE_CTX << 10) | (G.cmd.cycle & 1) | ((G.slot_id & 0xFF) << 24);

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    return wait_cmd_complete(12000, 0);
}

static int wait_port_u0_enabled(uint32_t port_index0, uint32_t timeout_ms)
{
    volatile uint32_t *PORTSC = &G.R.ports[port_index0].PORTSC;

    for (uint32_t t = 0; t < timeout_ms; ++t)
    {
        uint32_t v = *PORTSC;

        /* Clear change bits (RW1C) every loop */
        if (v & (PORTSC_CSC | PORTSC_PRC | PORTSC_PEC))
        {
            *PORTSC = v | PORTSC_CSC | PORTSC_PRC | PORTSC_PEC;
            mmio_wmb();
            v = *PORTSC;
        }

        uint32_t pls = (v & PORTSC_PLS_MASK) >> 5;

        /* Require: connected + enabled + link U0 + not in reset */
        if ((v & PORTSC_CCS) && (v & PORTSC_PED) && ((v & PORTSC_PR) == 0) && (pls == 0))
            return 0;

        mdelay(1);
    }

    return -1;
}

/* ===== port reset → dot #8, enable slot → #9, address → #10 ===== */
static int reset_first_connected_port(int *port_id_out)
{
    if (!port_id_out)
        return -1;

    dot(10, C_CY); /* enter */

    /* show CCS/PP state for first 8 ports on-screen:
       dots 20..27 = CCS (green=connected, red=not)
       dots 28..35 = PP  (green=powered, magenta=not) */
    for (int i = 20; i < 36; i++)
        dot(i, 0);

    for (int tries = 0; tries < 60; ++tries)
    {
        uint32_t max_show = (G.n_ports < 8) ? G.n_ports : 8;

        /* snapshot */
        uint32_t any_ccs = 0;
        for (uint32_t p = 0; p < max_show; ++p)
        {
            uint32_t v = G.R.ports[p].PORTSC;
            dot(20 + (int)p, (v & PORTSC_CCS) ? C_OK : C_ER);
            dot(28 + (int)p, (v & PORTSC_PP) ? C_OK : 0xFF00FF);
            any_ccs |= (v & PORTSC_CCS);
        }
        dot(11, any_ccs ? C_OK : C_ER);

        /* find a connected port and actually reset it */
        for (uint32_t p = 0; p < G.n_ports; ++p)
        {
            volatile uint32_t *PORTSC = &G.R.ports[p].PORTSC;
            uint32_t v = *PORTSC;

            /* ensure power */
            if (!(v & PORTSC_PP))
            {
                *PORTSC = v | PORTSC_PP;
                mmio_wmb();
                mdelay(20);
                v = *PORTSC;
            }

            /* clear sticky change bits */
            uint32_t chg = (PORTSC_CSC | PORTSC_PEC | PORTSC_PRC | PORTSC_PLC | PORTSC_CEC);
            if (v & chg)
            {
                *PORTSC = v | chg;
                mmio_wmb();
                (void)*PORTSC;
                v = *PORTSC;
            }

            if (!(v & PORTSC_CCS))
                continue; /* not connected */

            /* Port Reset (PR) */
            dot(12, C_YL);
            *PORTSC = v | PORTSC_PR;
            mmio_wmb();

            /* wait PR to clear */
            int pr_cleared = 0;
            for (int i = 0; i < 500; ++i)
            {
                uint32_t s = *PORTSC;
                if (!(s & PORTSC_PR))
                {
                    pr_cleared = 1;
                    v = s;
                    break;
                }
                mdelay(1);
            }
            dot(13, pr_cleared ? C_OK : C_ER);
            if (!pr_cleared)
                continue;

            /* wait for Port Enabled + U0 (PLS==0) */
            int ready = 0;
            for (int i = 0; i < 800; ++i)
            {
                uint32_t s = *PORTSC;
                uint32_t pls = (s & PORTSC_PLS_MASK) >> 5;
                if ((s & PORTSC_PED) && (pls == 0))
                {
                    ready = 1;
                    v = s;
                    break;
                }
                mdelay(1);
            }

            dot(14, (v & PORTSC_PED) ? C_OK : C_ER);
            dot(15, (((v & PORTSC_PLS_MASK) >> 5) == 0) ? C_OK : C_ER);

            /* show SPEED nibble as a visible tint (not critical, just useful) */
            uint32_t spd = (v >> PORTSC_SPEED_SHIFT) & 0xFu;
            dot(16, (spd << 20) | 0x0000FF);

            if (!ready)
                continue;

            *port_id_out = (int)(p + 1);
            dot(10, C_OK); /* success */
            return 0;
        }

        mdelay(20);
    }

    dot(10, C_ER); /* fail */
    return -1;
}

static int reset_specific_connected_port(int port_id)
{
    enum
    {
        HID_RESET_MAX_TRIES = 4,
        HID_RESET_PR_TIMEOUT_MS = 120,
        HID_RESET_READY_TIMEOUT_MS = 180,
        HID_RESET_RETRY_DELAY_MS = 10
    };

    if (port_id <= 0 || port_id > (int)G.n_ports)
        return -1;

    for (int tries = 0; tries < HID_RESET_MAX_TRIES; ++tries)
    {
        int attempt = tries + 1;
        volatile uint32_t *PORTSC = &G.R.ports[port_id - 1].PORTSC;
        uint32_t v = *PORTSC;

        xhci_dbg_portsc_line("hid reset: start", port_id, attempt, v);

        if (!(v & PORTSC_PP))
        {
            *PORTSC = v | PORTSC_PP;
            mmio_wmb();
            mdelay(20);
            v = *PORTSC;
            xhci_dbg_portsc_line("hid reset: after-pp", port_id, attempt, v);
        }

        {
            uint32_t chg = (PORTSC_CSC | PORTSC_PEC | PORTSC_PRC | PORTSC_PLC | PORTSC_CEC);
            if (v & chg)
            {
                *PORTSC = v | chg;
                mmio_wmb();
                (void)*PORTSC;
                v = *PORTSC;
                xhci_dbg_portsc_line("hid reset: after-rw1c", port_id, attempt, v);
            }
        }

        if (!(v & PORTSC_CCS))
        {
            xhci_dbg_portsc_line("hid reset: no-ccs", port_id, attempt, v);
            mdelay(HID_RESET_RETRY_DELAY_MS);
            continue;
        }

        *PORTSC = v | PORTSC_PR;
        mmio_wmb();
        xhci_dbg_portsc_line("hid reset: pr-set", port_id, attempt, *PORTSC);

        for (int i = 0; i < HID_RESET_PR_TIMEOUT_MS; ++i)
        {
            uint32_t s = *PORTSC;
            if (!(s & PORTSC_PR))
            {
                uint32_t pls = (s & PORTSC_PLS_MASK) >> 5;

                xhci_dbg_portsc_line("hid reset: pr-clear", port_id, attempt, s);

                /*
                 * Some ports briefly report the exact ready state we want
                 * immediately after reset clears, then fall back out while
                 * the slower wait loop keeps polling. Accept that state
                 * directly here so HID can continue to enumeration.
                 */
                if ((s & PORTSC_CCS) && (s & PORTSC_PED) && (pls == 0))
                {
                    xhci_dbg_portsc_line("hid reset: ok", port_id, attempt, s);
                    return 0;
                }

                if (wait_port_u0_enabled((uint32_t)(port_id - 1), HID_RESET_READY_TIMEOUT_MS) == 0)
                {
                    xhci_dbg_portsc_line("hid reset: ok", port_id, attempt, *PORTSC);
                    return 0;
                }

                xhci_dbg_portsc_line("hid reset: not-ready", port_id, attempt, *PORTSC);
                break;
            }
            mdelay(1);
        }

        if ((*PORTSC & PORTSC_PR) != 0)
            xhci_dbg_portsc_line("hid reset: pr-timeout", port_id, attempt, *PORTSC);

        mdelay(HID_RESET_RETRY_DELAY_MS);
    }

    xhci_dbg("hid reset: failed all tries");
    return -1;
}

/* --- CMD: Enable Slot -> returns real slot id --------------------------- */
static int issue_enable_slot(int *slot_id_out)
{
    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);
    *t = (trb_t){0, 0, 0, (TRB_TYPE_ENABLE_SLOT << 10) | (G.cmd.cycle & 1)};

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    trb_t evt;
    if (wait_cmd_complete(12000, &evt))
        return -1;

    /* Slot ID is bits 31:24 of d3 for Command Completion Event */
    int slot = (int)((evt.d3 >> 24) & 0xFF);
    if (slot <= 0)
        return -1;

    G.slot_id = slot;
    if (slot_id_out)
        *slot_id_out = slot;

    dot(9, C_YL);
    return 0;
}

/* Single-step Address Device (BSR=0) with correct ICC=64B layout */
static int address_device(void)
{
    uint8_t *in = (uint8_t *)pmem_alloc_pages_lowdma(1);
    void *dev = pmem_alloc_pages_lowdma(1);
    if (!in || !dev)
        return -1;

    for (uint32_t i = 0; i < 4096 / 4; ++i)
        ((uint32_t *)in)[i] = 0;
    for (uint32_t i = 0; i < 4096 / 4; ++i)
        ((uint32_t *)dev)[i] = 0;

    G.input_ctx = (uint64_t)(uintptr_t)in;
    G.dev_ctx = (uint64_t)(uintptr_t)dev;

    /* DCBAA[slot] -> device context */
    ((uint64_t *)(uintptr_t)G.dcbaa)[G.slot_id] = G.dev_ctx;
    dma_flush((void *)(uintptr_t)G.dcbaa, 256 * sizeof(uint64_t));
    mmio_wmb();

    uint32_t *icc = (uint32_t *)(in + 0);
    uint32_t *slot = (uint32_t *)(in + ICC_BYTES);
    uint32_t *ep0 = (uint32_t *)(in + ICC_BYTES + (1u * G_ctx_stride));

    uint32_t portsc = G.R.ports[G.port_id - 1].PORTSC;
    uint32_t spd = (portsc >> 10) & 0xF;
    uint16_t mps0 = (spd >= 4) ? 512 : (spd == 3) ? 64
                                                  : 8;

    dot(16, 0x00FF00);
    dot(17, (spd << 16) | 0x00FF00);
    dot(18, (mps0 == 512) ? 0x00FF00 : (mps0 == 64) ? 0xFFFF00
                                                    : 0xFF8800);

    /* ICC: add Slot + EP0 */
    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << 1);

    /* Slot Context:
       - Context Entries = 1 (bit 27)
       - Speed in bits 23:20
       - Root Hub Port Number in DW1 bits 23:16  (<<16)  */
    slot[0] = (1u << 27) | ((spd & 0xFu) << 20);
    slot[1] = ((uint32_t)(G.port_id & 0xFF) << 16); /* <-- IMPORTANT */
    slot[2] = 0;
    slot[3] = 0;

    dot(22, 0x00FF00);
    dot(23, (G.port_id & 0xFF) << 16);

    /* Fresh EP0 ring + TR Dequeue (DCS=1) */
    uint32_t n;
    ring_init_xfer_or_cmd(&G.ctrl_tr, &n);
    G.ctrl_enq = 0;
    G.ctrl_cycle = 1;
    uint64_t dq = G.ctrl_tr | 1u;

    /* Endpoint 0 context:
       DW1: CErr[2:1], EP Type[5:3]=4(Control), MPS[31:16] */
    ep0[0] = 0;
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)mps0 << 16);
    ep0[2] = (uint32_t)dq;
    ep0[3] = (uint32_t)(dq >> 32);
    ep0[4] = 8u;
    ep0[5] = 0;
    ep0[6] = 0;
    ep0[7] = 0;

    dma_flush(dev, 4096);
    dma_flush(in, 4096);
    mmio_wmb();

    if (do_address_device(0))
        return -1;

    dot(10, C_YL);
    return 0;
}

/* ===== control transfer on EP0 → dots 11..13 ===== */
#define USB_DIR_IN 0x80
static inline uint64_t pack_setup(uint8_t bm, uint8_t bReq, uint16_t wValue, uint16_t wIndex, uint16_t wLen)
{
    uint64_t v = 0;
    v |= (uint64_t)bm;
    v |= (uint64_t)bReq << 8;
    v |= (uint64_t)wValue << 16;
    v |= (uint64_t)wIndex << 32;
    v |= (uint64_t)wLen << 48;
    return v;
}
/* ===== control transfer on EP0 → dots 40..45 ===== */
static int control_xfer(uint8_t bm, uint8_t br, uint16_t wValue, uint16_t wIndex,
                        void *data, uint16_t wLen)
{
    /* xHCI Setup Stage TRT encodings */
    const uint32_t TRT_NO_DATA = 0;
    const uint32_t TRT_OUT = 2;
    const uint32_t TRT_IN = 3;

    trb_t *ring = (trb_t *)(uintptr_t)G.ctrl_tr;

    /* ---------- SETUP STAGE ---------- */
    trb_t *st = ring_next(ring, 256, &G.ctrl_enq, &G.ctrl_cycle);

    uint64_t setup = pack_setup(bm, br, wValue, wIndex, wLen);
    st->d0 = (uint32_t)setup;
    st->d1 = (uint32_t)(setup >> 32);

    /* Setup Stage TRB: Transfer Length must be 8 */
    st->d2 = 8u;

    uint32_t trt = TRT_NO_DATA;
    if (wLen)
        trt = (bm & USB_DIR_IN) ? TRT_IN : TRT_OUT;

    /* d3: type + TRT + IDT + cycle */
    st->d3 = (TRB_TYPE_SETUP_STAGE << 10) |
             (trt << 16) |
             (1u << 6) | /* IDT=1: setup bytes are in d0/d1 */
             (G.ctrl_cycle & 1);

    dot(40, C_CY);

    /* ---------- DATA STAGE (optional) ---------- */
    if (wLen)
    {
        /* For OUT, flush payload before ringing.
           For IN, we’ll invalidate after completion. */
        if ((bm & USB_DIR_IN) == 0)
            dma_flush(data, wLen);

        trb_t *dt = ring_next(ring, 256, &G.ctrl_enq, &G.ctrl_cycle);
        uint64_t p = (uint64_t)(uintptr_t)data;

        dt->d0 = (uint32_t)p;
        dt->d1 = (uint32_t)(p >> 32);
        dt->d2 = (uint32_t)wLen;

        /* Data Stage: DIR (bit 16) = 1 for IN */
        dt->d3 = (TRB_TYPE_DATA_STAGE << 10) |
                 ((bm & USB_DIR_IN) ? (1u << 16) : 0u) |
                 (G.ctrl_cycle & 1);

        dot(41, C_CY);
    }

    /* ---------- STATUS STAGE ---------- */
    trb_t *ss = ring_next(ring, 256, &G.ctrl_enq, &G.ctrl_cycle);

    /* Status direction is opposite of data stage.
       If no data stage, status is IN. */
    uint32_t status_in = wLen ? (((bm & USB_DIR_IN) == 0)) : 1u;

    ss->d0 = 0;
    ss->d1 = 0;
    ss->d2 = 0;
    ss->d3 = (TRB_TYPE_STATUS_STAGE << 10) |
             (status_in ? (1u << 16) : 0u) |
             (1u << 5) | /* IOC=1 => Transfer Event */
             (G.ctrl_cycle & 1);

    dot(42, C_CY);

    /* Make TRBs visible */
    dma_flush((void *)(uintptr_t)G.ctrl_tr, 256 * sizeof(trb_t));
    mmio_wmb();

    /* Ring EP0 (DCI=1) */
    ring_ep_db((uint8_t)G.slot_id, 1);

    /* Wait for Transfer Event and decode CC */
    trb_t evt;
    if (wait_event_type(TRB_TYPE_XFER_EVT, 12000, &evt))
    {
        dot(43, C_ER); /* no event */
        return -1;
    }

    uint32_t cc = (evt.d2 >> 24) & 0xFF;

    /* paint CC nibble readout like before */
    dot(44, ((cc & 0xF0) << 16) | 0x00FF00); /* hi nibble */
    dot(45, ((cc & 0x0F) << 20) | 0x0000FF); /* lo nibble */

    if (cc != 1)
    {
        dot(43, C_ER); /* transfer completed but not SUCCESS */
        return -1;
    }

    /* For IN transfers, invalidate payload after success */
    if (wLen && (bm & USB_DIR_IN))
        dma_invalidate(data, wLen);

    dot(43, C_OK);
    return 0;
}

static void copy_bytes(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (!d || !s)
        return;

    for (uint32_t i = 0; i < n; ++i)
        d[i] = s[i];
}

int usbh_control_xfer(usbh_dev_t *d,
                      uint8_t bmRequestType, uint8_t bRequest,
                      uint16_t wValue, uint16_t wIndex,
                      void *data, uint16_t wLength)
{
    int rc;

    if (!d || !d->configured)
        return -1;

    if (dev_state_load(d) != 0)
        return -1;

    rc = control_xfer(bmRequestType, bRequest, wValue, wIndex, data, wLength);
    dev_state_save(d);
    return rc;
}

/* ===== parse MSC IF (accept 0x50 or 0x62), configure bulk → dot #14 ===== */
typedef struct
{
    uint8_t if_num;
    uint8_t ep_in_num, ep_out_num;
    uint16_t mps_in, mps_out;
} msc_eps_t;

typedef struct
{
    uint8_t if_num;
    uint8_t ep_in_num;
    uint16_t mps_in;
    uint8_t interval;
    uint8_t subclass;
    uint8_t protocol; // 1=boot keyboard, 2=boot mouse
    uint16_t report_desc_len;
    uint8_t mouse_valid;
    uint8_t mouse_has_report_id;
    uint8_t mouse_report_id;
    uint8_t mouse_buttons;
    uint16_t mouse_btn_bits;
    uint16_t mouse_x_bits;
    uint16_t mouse_y_bits;
    uint16_t mouse_wheel_bits;
    uint8_t mouse_btn_size;
    uint8_t mouse_x_size;
    uint8_t mouse_y_size;
    uint8_t mouse_wheel_size;
} hid_eps_t;

typedef struct
{
    uint8_t usage_page;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
    uint8_t has_report_id;
} hid_globals_t;

static uint32_t hid_item_u32(const uint8_t *p, uint8_t nbytes)
{
    uint32_t v = 0;

    if (!p)
        return 0;

    if (nbytes > 0)
        v |= (uint32_t)p[0];
    if (nbytes > 1)
        v |= (uint32_t)p[1] << 8;
    if (nbytes > 2)
        v |= (uint32_t)p[2] << 16;
    if (nbytes > 3)
        v |= (uint32_t)p[3] << 24;

    return v;
}

static uint32_t hid_usage_for_index(const uint32_t *usages, uint8_t usage_count,
                                    uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                    uint32_t index)
{
    if (index < usage_count)
        return usages[index];

    if (have_usage_range)
    {
        uint32_t range_count = 0;
        if (usage_max >= usage_min)
            range_count = usage_max - usage_min + 1u;

        if (index < range_count)
            return usage_min + index;
    }

    return 0;
}

static void hid_capture_mouse_input(hid_eps_t *out, const hid_globals_t *g,
                                    const uint32_t *usages, uint8_t usage_count,
                                    uint8_t have_usage_range, uint32_t usage_min, uint32_t usage_max,
                                    uint16_t bit_base)
{
    uint8_t report_id = 0;
    uint8_t buttons = 0;

    if (!out || !g || g->report_size == 0 || g->report_count == 0)
        return;

    report_id = g->has_report_id ? g->report_id : 0;
    if (out->mouse_valid || out->mouse_buttons || out->mouse_x_size || out->mouse_y_size || out->mouse_wheel_size)
    {
        uint8_t out_report_id = out->mouse_has_report_id ? out->mouse_report_id : 0;
        if (out_report_id != report_id)
            return;
    }
    else
    {
        out->mouse_has_report_id = g->has_report_id;
        out->mouse_report_id = report_id;
    }

    if (g->usage_page == 0x09u && out->mouse_buttons == 0)
    {
        if (have_usage_range && usage_max >= usage_min && usage_min <= 8u)
        {
            uint32_t count = usage_max - usage_min + 1u;
            if (count > g->report_count)
                count = g->report_count;
            if (count > 8u)
                count = 8u;
            buttons = (uint8_t)count;
        }
        else if (usage_count > 0)
        {
            buttons = usage_count;
            if (buttons > g->report_count)
                buttons = (uint8_t)g->report_count;
            if (buttons > 8u)
                buttons = 8u;
        }

        if (buttons > 0)
        {
            out->mouse_buttons = buttons;
            out->mouse_btn_bits = bit_base;
            out->mouse_btn_size = (uint8_t)g->report_size;
        }
    }

    if (g->usage_page == 0x01u)
    {
        for (uint32_t i = 0; i < g->report_count; ++i)
        {
            uint32_t usage = hid_usage_for_index(usages, usage_count, have_usage_range, usage_min, usage_max, i);
            uint16_t bit_off = (uint16_t)(bit_base + (uint16_t)(i * g->report_size));

            if (usage == 0x30u && out->mouse_x_size == 0)
            {
                out->mouse_x_bits = bit_off;
                out->mouse_x_size = (uint8_t)g->report_size;
            }
            else if (usage == 0x31u && out->mouse_y_size == 0)
            {
                out->mouse_y_bits = bit_off;
                out->mouse_y_size = (uint8_t)g->report_size;
            }
            else if (usage == 0x38u && out->mouse_wheel_size == 0)
            {
                out->mouse_wheel_bits = bit_off;
                out->mouse_wheel_size = (uint8_t)g->report_size;
            }
        }
    }

    if (out->mouse_buttons > 0 && out->mouse_x_size > 0 && out->mouse_y_size > 0)
        out->mouse_valid = 1;
}

static int hid_parse_mouse_report_desc(const uint8_t *desc, uint16_t len, hid_eps_t *out)
{
    hid_globals_t g;
    hid_globals_t g_stack[4];
    uint16_t bit_cursor[256];
    uint32_t usages[16];
    uint8_t usage_count = 0;
    uint8_t have_usage_range = 0;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
    uint8_t collection_mouse[16];
    uint8_t collection_depth = 0;
    uint8_t stack_depth = 0;
    uint16_t i = 0;

    if (!desc || !out)
        return -1;

    g.usage_page = 0;
    g.report_size = 0;
    g.report_count = 0;
    g.report_id = 0;
    g.has_report_id = 0;

    for (uint32_t k = 0; k < 256u; ++k)
        bit_cursor[k] = 0;
    for (uint32_t k = 0; k < 16u; ++k)
        collection_mouse[k] = 0;

    out->mouse_valid = 0;
    out->mouse_has_report_id = 0;
    out->mouse_report_id = 0;
    out->mouse_buttons = 0;
    out->mouse_btn_bits = 0;
    out->mouse_x_bits = 0;
    out->mouse_y_bits = 0;
    out->mouse_wheel_bits = 0;
    out->mouse_btn_size = 0;
    out->mouse_x_size = 0;
    out->mouse_y_size = 0;
    out->mouse_wheel_size = 0;

    while (i < len)
    {
        uint8_t b = desc[i++];
        uint8_t size_code;
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t val;
        uint8_t current_mouse = (collection_depth > 0) ? collection_mouse[collection_depth - 1] : 0;

        if (b == 0xFEu)
        {
            uint8_t long_size;
            if (i + 1 >= len)
                break;
            long_size = desc[i];
            i += 2;
            if (i + long_size > len)
                break;
            i += long_size;
            continue;
        }

        size_code = b & 0x03u;
        size = (size_code == 3u) ? 4u : size_code;
        type = (b >> 2) & 0x03u;
        tag = (b >> 4) & 0x0Fu;

        if ((uint32_t)i + size > len)
            break;

        val = hid_item_u32(&desc[i], size);

        if (type == 0u)
        {
            if (tag == 8u)
            {
                if (current_mouse && (val & 0x01u) == 0)
                    hid_capture_mouse_input(out, &g, usages, usage_count, have_usage_range, usage_min, usage_max, bit_cursor[g.report_id]);

                bit_cursor[g.report_id] = (uint16_t)(bit_cursor[g.report_id] + (uint16_t)(g.report_size * g.report_count));
            }
            else if (tag == 10u)
            {
                uint32_t usage = 0;
                uint8_t parent_mouse = current_mouse;
                uint8_t is_mouse = parent_mouse;

                if (usage_count > 0)
                    usage = usages[usage_count - 1];
                else if (have_usage_range)
                    usage = usage_min;

                if ((val & 0xFFu) == 1u && g.usage_page == 0x01u && usage == 0x02u)
                    is_mouse = 1;

                if (collection_depth < 16u)
                    collection_mouse[collection_depth++] = is_mouse;
            }
            else if (tag == 12u)
            {
                if (collection_depth > 0)
                    collection_depth--;
            }

            usage_count = 0;
            have_usage_range = 0;
        }
        else if (type == 1u)
        {
            if (tag == 0u)
                g.usage_page = (uint8_t)val;
            else if (tag == 7u)
                g.report_size = val;
            else if (tag == 8u)
            {
                g.report_id = (uint8_t)val;
                g.has_report_id = 1;
            }
            else if (tag == 9u)
                g.report_count = val;
            else if (tag == 10u)
            {
                if (stack_depth < 4u)
                    g_stack[stack_depth++] = g;
            }
            else if (tag == 11u)
            {
                if (stack_depth > 0)
                    g = g_stack[--stack_depth];
            }
        }
        else if (type == 2u)
        {
            if (tag == 0u)
            {
                if (usage_count < 16u)
                    usages[usage_count++] = val;
            }
            else if (tag == 1u)
            {
                usage_min = val;
                have_usage_range = 1;
            }
            else if (tag == 2u)
            {
                usage_max = val;
                have_usage_range = 1;
            }
        }

        i = (uint16_t)(i + size);
    }

    return out->mouse_valid ? 0 : -1;
}

static int find_next_hid_interface(const uint8_t *cfg, uint16_t len, uint16_t *scan_off, hid_eps_t *out)
{
    uint16_t off;

    if (!cfg || !scan_off || !out)
        return -1;

    off = *scan_off;

    while (off + 2 <= len)
    {
        uint8_t L = cfg[off];
        uint8_t T = cfg[off + 1];

        if (!L || off + L > len)
            break;

        if (T == DESC_INTERFACE && L >= 9 && cfg[off + 5] == 0x03u)
        {
            uint16_t o = (uint16_t)(off + L);

            memset(out, 0, sizeof(*out));
            out->if_num = cfg[off + 2];
            out->mps_in = 8;
            out->subclass = cfg[off + 6];
            out->protocol = cfg[off + 7];

            while (o + 2 <= len)
            {
                uint8_t L2 = cfg[o];
                uint8_t T2 = cfg[o + 1];

                if (!L2 || o + L2 > len)
                    break;

                if (T2 == DESC_HID && L2 >= 9 && cfg[o + 6] == DESC_REPORT)
                {
                    out->report_desc_len = (uint16_t)cfg[o + 7] | ((uint16_t)cfg[o + 8] << 8);
                }
                else if (T2 == DESC_ENDPOINT && L2 >= 7)
                {
                    uint8_t addr = cfg[o + 2];
                    uint8_t attr = cfg[o + 3] & 0x3u;
                    uint16_t mps = (uint16_t)cfg[o + 4] | ((uint16_t)cfg[o + 5] << 8);

                    if (attr == 3u && (addr & 0x80u) && out->ep_in_num == 0)
                    {
                        out->ep_in_num = addr & 0x0Fu;
                        out->mps_in = mps;
                        out->interval = cfg[o + 6];
                    }
                }
                else if (T2 == DESC_INTERFACE)
                {
                    break;
                }

                o = (uint16_t)(o + L2);
            }

            *scan_off = o;
            if (out->ep_in_num != 0)
                return 0;

            off = o;
            continue;
        }

        off = (uint16_t)(off + L);
    }

    *scan_off = off;
    return -1;
}

static void hid_copy_mouse_format_to_dev(usbh_dev_t *D, const hid_eps_t *ep)
{
    if (!D || !ep)
        return;

    D->hid_has_report_id = ep->mouse_has_report_id;
    D->hid_report_id = ep->mouse_report_id;
    D->hid_mouse_valid = ep->mouse_valid;
    D->hid_mouse_buttons = ep->mouse_buttons;
    D->hid_mouse_btn_bits = ep->mouse_btn_bits;
    D->hid_mouse_x_bits = ep->mouse_x_bits;
    D->hid_mouse_y_bits = ep->mouse_y_bits;
    D->hid_mouse_wheel_bits = ep->mouse_wheel_bits;
    D->hid_mouse_btn_size = ep->mouse_btn_size;
    D->hid_mouse_x_size = ep->mouse_x_size;
    D->hid_mouse_y_size = ep->mouse_y_size;
    D->hid_mouse_wheel_size = ep->mouse_wheel_size;
}

static int parse_config_for_hid_boot(const uint8_t *cfg, uint16_t len, uint8_t want_protocol, hid_eps_t *out)
{
    uint16_t scan_off = 0;
    hid_eps_t best_boot;

    memset(&best_boot, 0, sizeof(best_boot));
    memset(out, 0, sizeof(*out));
    best_boot.mps_in = 8;
    out->mps_in = 8;

    while (find_next_hid_interface(cfg, len, &scan_off, out) == 0)
    {
        if (out->subclass == 1u &&
            ((want_protocol == 1u && out->protocol == 1u) ||
             (want_protocol == 2u && out->protocol == 2u) ||
             (want_protocol == 0u && (out->protocol == 1u || out->protocol == 2u))))
        {
            if (best_boot.ep_in_num == 0)
                best_boot = *out;
        }

        if (want_protocol != 1u && out->report_desc_len > 0 && out->report_desc_len <= 1024u)
        {
            uint8_t *report_desc = (uint8_t *)alloc_dma(out->report_desc_len);
            if (report_desc)
            {
                for (uint32_t i = 0; i < out->report_desc_len; ++i)
                    report_desc[i] = 0;

                xhci_dbg("hid enum: get report desc");
                if (control_xfer(0x81, 6, (DESC_REPORT << 8) | 0, out->if_num, report_desc, out->report_desc_len) == 0)
                {
                    xhci_dbg("hid enum: parse report desc");
                    if (hid_parse_mouse_report_desc(report_desc, out->report_desc_len, out) == 0)
                    {
                        xhci_dbg("hid enum: mouse report ok");
                        out->protocol = 2u;
                        return 0;
                    }
                }
            }
        }
    }

    if (best_boot.ep_in_num != 0)
    {
        *out = best_boot;
        return 0;
    }

    return -1;
}

static int parse_config_for_msc(const uint8_t *cfg, uint16_t len, msc_eps_t *out)
{
    out->if_num = 0;
    out->ep_in_num = out->ep_out_num = 0;
    out->mps_in = out->mps_out = 512;

    uint16_t off = 0;
    while (off + 2 <= len)
    {
        uint8_t L = cfg[off], T = cfg[off + 1];
        if (!L || off + L > len)
            break;

        if (T == DESC_INTERFACE && L >= 9)
        {
            uint8_t ifnum = cfg[off + 2];
            uint8_t cls = cfg[off + 5], sub = cfg[off + 6], proto = cfg[off + 7];

            if (cls == 0x08 && sub == 0x06 && (proto == 0x50 || proto == 0x62))
            {
                out->if_num = ifnum;

                uint16_t o = off + L;
                while (o + 2 <= len)
                {
                    uint8_t L2 = cfg[o], T2 = cfg[o + 1];
                    if (!L2 || o + L2 > len)
                        break;

                    if (T2 == DESC_ENDPOINT && L2 >= 7)
                    {
                        uint8_t addr = cfg[o + 2];
                        uint8_t attr = cfg[o + 3] & 0x3;
                        uint16_t mps = (uint16_t)cfg[o + 4] | ((uint16_t)cfg[o + 5] << 8);

                        if (attr == 2) // bulk
                        {
                            if (addr & 0x80)
                            {
                                out->ep_in_num = addr & 0x0F;
                                out->mps_in = mps;
                            }
                            else
                            {
                                out->ep_out_num = addr & 0x0F;
                                out->mps_out = mps;
                            }
                        }
                    }
                    else if (T2 == DESC_INTERFACE)
                    {
                        break;
                    }

                    o += L2;
                }

                return (out->ep_in_num && out->ep_out_num) ? 0 : -1;
            }
        }

        off += L; // <-- important: always advance to next descriptor
    }

    return -1; // <-- ONLY after scanning entire config
}

static int configure_bulk_endpoints(uint8_t ep_in, uint16_t mps_in, uint8_t ep_out, uint16_t mps_out)
{
    uint8_t *in = (uint8_t *)(uintptr_t)G.input_ctx;
    uint8_t *dev = (uint8_t *)(uintptr_t)G.dev_ctx;
    if (!in || !dev)
        return -1;

    uint8_t dci_in = dci_from_ep(ep_in, 1);
    uint8_t dci_out = dci_from_ep(ep_out, 0);

    // debug: DCI must be nonzero and <=31
    dot(214, 0x00FFFFu);
    dot(215, (dci_in ? 0x00FF00u : 0xFF0000u));
    dot(216, (dci_out ? 0x00FF00u : 0xFF0000u));

    // Context Entries = max DCI used
    uint8_t ce = (dci_in > dci_out) ? dci_in : dci_out;

    // Clear the whole input context page
    for (uint32_t i = 0; i < 4096; i++)
        in[i] = 0;

    // ICC is 64 bytes (16 dwords)
    uint32_t *icc = (uint32_t *)(void *)(in + 0);

    // Slot context starts at +ICC_BYTES (64)
    uint32_t *slot_in = (uint32_t *)(void *)(in + ICC_BYTES);
    uint32_t *slot_dev = (uint32_t *)(void *)(dev + 0);

    // Add flags: slot + the two endpoint DCIs
    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << dci_in) | (1u << dci_out);

    // Copy current slot context from Device Context into Input Context
    for (uint32_t i = 0; i < (G_ctx_stride / 4); i++)
        slot_in[i] = slot_dev[i];

    // Patch Context Entries (DW0 bits 31:27)
    slot_in[0] &= ~(0x1Fu << 27);
    slot_in[0] |= ((uint32_t)(ce & 0x1Fu) << 27);

    // Allocate transfer rings (256 TRBs w/ link TRB)
    uint32_t n;
    ring_init_xfer_or_cmd(&G.bulk_in_tr, &n);
    G.bin_enq = 0;
    G.bin_cycle = 1;

    ring_init_xfer_or_cmd(&G.bulk_out_tr, &n);
    G.bout_enq = 0;
    G.bout_cycle = 1;

    if (!G.bulk_in_tr || !G.bulk_out_tr)
        return -1;

// Helper macro: EP ctx pointer for DCI 'dci' in input context
#define EP_CTX_PTR(_dci) ((uint32_t *)(void *)(in + ICC_BYTES + ((uint32_t)(_dci) * G_ctx_stride)))

    // Bulk OUT endpoint context (EP Type = 2)
    {
        uint32_t *ep = EP_CTX_PTR(dci_out);
        g_xhci_last_epout = (uint32_t)dci_out;
        for (int i = 0; i < 16; i++)
            ep[i] = 0;

        uint64_t dq = ((uint64_t)G.bulk_out_tr) | 1u; // DCS=1

        ep[1] = (3u << 1) | (2u << 3) | ((uint32_t)mps_out << 16); // CErr=3, Bulk OUT, MPS
        ep[2] = (uint32_t)dq;
        ep[3] = (uint32_t)(dq >> 32);
        ep[4] = 0x1000u;
    }

    // Bulk IN endpoint context (EP Type = 6)
    {
        uint32_t *ep = EP_CTX_PTR(dci_in);
        g_xhci_last_epin = (uint32_t)dci_in;
        for (int i = 0; i < 16; i++)
            ep[i] = 0;

        uint64_t dq = ((uint64_t)G.bulk_in_tr) | 1u; // DCS=1

        ep[1] = (3u << 1) | (6u << 3) | ((uint32_t)mps_in << 16); // CErr=3, Bulk IN, MPS
        ep[2] = (uint32_t)dq;
        ep[3] = (uint32_t)(dq >> 32);
        ep[4] = 0x1000u;
    }

#undef EP_CTX_PTR

    // Flush input ctx so controller sees it
    dma_flush(in, 4096);
    mmio_wmb();

    // Issue CONFIGURE_ENDPOINT command
    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);

    t->d0 = (uint32_t)G.input_ctx;
    t->d1 = (uint32_t)(G.input_ctx >> 32);
    t->d2 = 0;
    t->d3 = (TRB_TYPE_CONFIG_EP << 10) | (G.cmd.cycle & 1) | ((G.slot_id & 0xFF) << 24);

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    // Wait for command completion, and (optional) show CC on dot 116
    trb_t evt;
    if (wait_cmd_complete(12000, &evt))
        return -1;

    uint32_t cc = (evt.d2 >> 24) & 0xFF;
    dot(116, (cc == 1) ? 0x00FF00u : 0xFF0000u);

    return (cc == 1) ? 0 : -1;
}

static uint8_t xhci_intr_interval_from_binterval(uint8_t b_interval, uint32_t port_speed)
{
    uint8_t interval = b_interval;

    if (interval == 0)
        interval = 1;

    if (port_speed >= 3u)
        return (uint8_t)(interval - 1u);

    {
        uint8_t v = 0;
        uint8_t frames = interval;
        uint8_t log2_ceil = 0;

        while (v < frames)
        {
            v = (v == 0) ? 1u : (uint8_t)(v << 1);
            if (v < frames)
                ++log2_ceil;
        }

        return (uint8_t)(3u + log2_ceil);
    }
}

static uint8_t hid_intr_poll_timeout_ms(uint8_t b_interval, uint32_t port_speed)
{
    uint32_t ms = 0;
    uint8_t interval = b_interval;

    if (interval == 0)
        interval = 1;

    if (port_speed >= 3u)
    {
        uint32_t shift = (interval > 16u) ? 15u : (uint32_t)(interval - 1u);
        uint32_t microframes = 1u << shift;
        ms = (microframes + 7u) >> 3;
        if (ms == 0)
            ms = 1u;
    }
    else
    {
        ms = interval;
    }

    if (ms < 2u)
        ms = 2u;
    if (ms > 16u)
        ms = 16u;

    return (uint8_t)ms;
}

static int configure_interrupt_in_endpoint(uint8_t ep_in, uint16_t mps_in, uint8_t b_interval)
{
    uint8_t *in = (uint8_t *)(uintptr_t)G.input_ctx;
    uint8_t *dev = (uint8_t *)(uintptr_t)G.dev_ctx;
    uint32_t portsc = 0;
    uint32_t port_speed = 0;
    uint32_t avg_trb = 0;
    uint32_t max_esit = 0;
    uint8_t xhci_interval = 0;
    if (!in || !dev)
        return -1;

    uint8_t dci_in = dci_from_ep(ep_in, 1);
    if (!dci_in)
        return -1;

    portsc = G.R.ports[G.port_id - 1].PORTSC;
    port_speed = (portsc >> PORTSC_SPEED_SHIFT) & 0xFu;
    xhci_interval = xhci_intr_interval_from_binterval(b_interval, port_speed);
    avg_trb = mps_in ? mps_in : 8u;
    max_esit = mps_in ? mps_in : 8u;

    uint8_t ce = dci_in;

    for (uint32_t i = 0; i < 4096; i++)
        in[i] = 0;

    uint32_t *icc = (uint32_t *)(void *)(in + 0);
    uint32_t *slot_in = (uint32_t *)(void *)(in + ICC_BYTES);
    uint32_t *slot_dev = (uint32_t *)(void *)(dev + 0);

    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << dci_in);

    for (uint32_t i = 0; i < (G_ctx_stride / 4); i++)
        slot_in[i] = slot_dev[i];

    slot_in[0] &= ~(0x1Fu << 27);
    slot_in[0] |= ((uint32_t)(ce & 0x1Fu) << 27);

    uint32_t n;
    ring_init_xfer_or_cmd(&G.intr_in_tr, &n);
    G.iin_enq = 0;
    G.iin_cycle = 1;

    if (!G.intr_in_tr)
        return -1;

#define EP_CTX_PTR(_dci) ((uint32_t *)(void *)(in + ICC_BYTES + ((uint32_t)(_dci) * G_ctx_stride)))

    {
        uint32_t *ep = EP_CTX_PTR(dci_in);
        for (int i = 0; i < 16; i++)
            ep[i] = 0;

        uint64_t dq = ((uint64_t)G.intr_in_tr) | 1u; // DCS=1

        // EP Type = 7 (Interrupt IN)
        ep[0] = ((uint32_t)xhci_interval & 0xFFu) << 16;
        ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)mps_in << 16);
        ep[2] = (uint32_t)dq;
        ep[3] = (uint32_t)(dq >> 32);
        ep[4] = (avg_trb & 0xFFFFu) | ((max_esit & 0xFFFFu) << 16);
    }

#undef EP_CTX_PTR

    dma_flush(in, 4096);
    mmio_wmb();

    trb_t *cr = (trb_t *)(uintptr_t)G.cmd.base;
    trb_t *t = ring_next(cr, G.cmd.size, &G.cmd.enq, &G.cmd.cycle);

    t->d0 = (uint32_t)G.input_ctx;
    t->d1 = (uint32_t)(G.input_ctx >> 32);
    t->d2 = 0;
    t->d3 = (TRB_TYPE_CONFIG_EP << 10) | (G.cmd.cycle & 1) | ((G.slot_id & 0xFF) << 24);

    dma_flush((void *)(uintptr_t)G.cmd.base, (uint64_t)G.cmd.size * sizeof(trb_t));
    mmio_wmb();
    ring_cmd_db();

    trb_t evt;
    if (wait_cmd_complete(12000, &evt))
        return -1;

    {
        uint32_t cc = (evt.d2 >> 24) & 0xFF;
        if (cc != 1) // Success
        {
            terminal_print_inline("hid intr cfg cc=");
            terminal_print_inline_hex32(cc);
            terminal_print("");
            kgfx_render_all(black);
            return -1;
        }
    }

    return 0;
}

static int address_device_bsr1(void)
{
    uint8_t *in = (uint8_t *)pmem_alloc_pages_lowdma(1);
    void *dev = pmem_alloc_pages_lowdma(1);
    if (!in || !dev)
        return -1;

    for (uint32_t i = 0; i < 4096 / 4; ++i)
        ((uint32_t *)in)[i] = 0;
    for (uint32_t i = 0; i < 4096 / 4; ++i)
        ((uint32_t *)dev)[i] = 0;

    G.input_ctx = (uint64_t)(uintptr_t)in;
    G.dev_ctx = (uint64_t)(uintptr_t)dev;

    ((uint64_t *)(uintptr_t)G.dcbaa)[G.slot_id] = G.dev_ctx;
    dma_flush((void *)(uintptr_t)G.dcbaa, 256 * sizeof(uint64_t));
    mmio_wmb();

    uint32_t *icc = (uint32_t *)(in + 0);
    uint32_t *slot = (uint32_t *)(in + ICC_BYTES);
    uint32_t *ep0 = (uint32_t *)(in + ICC_BYTES + (1u * G_ctx_stride));

    uint32_t portsc = G.R.ports[G.port_id - 1].PORTSC;
    uint32_t spd = (portsc >> 10) & 0xF;
    uint16_t mps0 = (spd >= 4) ? 512 : (spd == 3) ? 64
                                                  : 8;

    /* ICC */
    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << 1);

    /* Slot ctx: CE=1, Speed, RH Port in bits 23:16 */
    slot[0] = (1u << 27) | ((spd & 0xFu) << 20);
    slot[1] = ((uint32_t)(G.port_id & 0xFF) << 16);
    slot[2] = 0;
    slot[3] = 0;

    /* EP0 ring + dequeue */
    uint32_t n;
    ring_init_xfer_or_cmd(&G.ctrl_tr, &n);
    G.ctrl_enq = 0;
    G.ctrl_cycle = 1;
    uint64_t dq = G.ctrl_tr | 1u;

    ep0[0] = 0;
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)mps0 << 16); /* CErr=3, Control, MPS */
    ep0[2] = (uint32_t)dq;
    ep0[3] = (uint32_t)(dq >> 32);
    ep0[4] = 8u;

    dma_flush(dev, 4096);
    dma_flush(in, 4096);
    mmio_wmb();

    /* BSR=1 is the whole point */
    return do_address_device(1);
}

static int enumerate_hid_boot_on_port(usbh_dev_t *D, uint8_t want_protocol, int port_id)
{
    xhci_dbg("hid enum: reset candidate port");
    G.port_id = port_id;
    if (reset_specific_connected_port(G.port_id))
    {
        xhci_dbg("hid enum: reset port failed");
        return -1;
    }

    xhci_dbg("hid enum: issue_enable_slot");
    if (issue_enable_slot(&G.slot_id))
    {
        xhci_dbg("hid enum: enable slot failed");
        return -1;
    }

    if (!(G.R.ports[G.port_id - 1].PORTSC & PORTSC_CCS))
    {
        xhci_dbg("hid enum: no device connected after reset");
        return -1;
    }

    xhci_dbg("hid enum: address_device");
    if (address_device())
    {
        xhci_dbg("hid enum: address_device failed");
        return -1;
    }

    uint8_t *dd8 = (uint8_t *)alloc_dma(8);
    if (!dd8)
    {
        xhci_dbg("hid enum: alloc dd8 failed");
        return -1;
    }

    for (int i = 0; i < 8; i++)
        dd8[i] = 0;

    xhci_dbg("hid enum: get device desc 8");
    if (control_xfer(0x80, 6, (DESC_DEV << 8) | 0, 0, dd8, 8))
    {
        xhci_dbg("hid enum: get device desc failed");
        return -1;
    }

    uint8_t mps0 = dd8[7];
    if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64)
        mps0 = 8;

    xhci_dbg("hid enum: set ep0 mps");
    if (ep0_evaluate_context_set_mps(mps0))
    {
        xhci_dbg("hid enum: set ep0 mps failed");
        return -1;
    }

    uint8_t *cfg9 = (uint8_t *)alloc_dma(9);
    if (!cfg9)
    {
        xhci_dbg("hid enum: alloc cfg9 failed");
        return -1;
    }

    for (int i = 0; i < 9; i++)
        cfg9[i] = 0;

    xhci_dbg("hid enum: get config 9");
    if (control_xfer(0x80, 6, (DESC_CONFIG << 8) | 0, 0, cfg9, 9))
    {
        xhci_dbg("hid enum: get config 9 failed");
        return -1;
    }

    uint16_t tot = (uint16_t)cfg9[2] | ((uint16_t)cfg9[3] << 8);
    if (tot < 9 || tot > 4096)
    {
        xhci_dbg("hid enum: config total length bad");
        return -1;
    }

    uint8_t *cfg = (uint8_t *)alloc_dma(tot);
    if (!cfg)
    {
        xhci_dbg("hid enum: alloc full config failed");
        return -1;
    }

    for (uint32_t i = 0; i < tot; i++)
        cfg[i] = 0;

    xhci_dbg("hid enum: get full config");
    if (control_xfer(0x80, 6, (DESC_CONFIG << 8) | 0, 0, cfg, tot))
    {
        xhci_dbg("hid enum: get full config failed");
        return -1;
    }

    xhci_dbg("hid enum: set configuration");
    {
        uint8_t cfgval = cfg9[5];
        if (control_xfer(0x00, 9, cfgval, 0, 0, 0))
        {
            xhci_dbg("hid enum: set configuration failed");
            return -1;
        }
    }

    xhci_dbg("hid enum: dumping config");
    xhci_dbg_dump_cfg_brief(cfg, tot);

    hid_eps_t ep;
    if (parse_config_for_hid_boot(cfg, tot, want_protocol, &ep))
    {
        xhci_dbg("hid enum: hid parse failed");
        return -1;
    }

    if (ep.protocol == 1u && ep.mouse_valid == 0)
    {
        xhci_dbg("hid enum: set boot protocol");
        if (control_xfer(0x21, 0x0B, 0, ep.if_num, 0, 0))
        {
            xhci_dbg("hid enum: set boot protocol failed");
            return -1;
        }
    }

    xhci_dbg("hid enum: set idle");
    if (control_xfer(0x21, 0x0A, 0, ep.if_num, 0, 0))
    {
        xhci_dbg("hid enum: set idle failed");
        // optional, continue anyway
    }

    xhci_dbg("hid enum: configure intr in ep");
    if (configure_interrupt_in_endpoint(ep.ep_in_num, ep.mps_in, ep.interval))
    {
        xhci_dbg("hid enum: configure intr ep failed");
        return -1;
    }

    D->configured = 1;
    D->addr = (uint8_t)G.slot_id;
    D->slot_id = (uint8_t)G.slot_id;
    D->port_id = (uint8_t)G.port_id;
    D->ep_intr_in = ep.ep_in_num;
    D->mps_intr_in = ep.mps_in;
    D->hid_if_num = ep.if_num;
    D->hid_protocol = ep.protocol;
    D->hid_poll_ms = hid_intr_poll_timeout_ms(ep.interval,
                                              (G.R.ports[G.port_id - 1].PORTSC >> PORTSC_SPEED_SHIFT) & 0xFu);
    hid_copy_mouse_format_to_dev(D, &ep);
    dev_state_save(D);
    port_claim((uint8_t)G.port_id);

    xhci_dbg("hid enum: success");
    return 0;
}

static int enumerate_first_hid_boot(usbh_dev_t *D, uint8_t want_protocol)
{
    xhci_dbg("hid enum: start");

    if (!D)
    {
        xhci_dbg("hid enum: null dev");
        return -1;
    }

    D->configured = 0;
    D->slot_id = 0;
    D->port_id = 0;
    D->ep_intr_in = 0;
    D->mps_intr_in = 0;
    D->hid_if_num = 0;
    D->hid_protocol = 0;
    D->hid_poll_ms = 0;
    D->hid_has_report_id = 0;
    D->hid_report_id = 0;
    D->hid_mouse_valid = 0;
    D->hid_mouse_buttons = 0;
    D->hid_mouse_btn_bits = 0;
    D->hid_mouse_x_bits = 0;
    D->hid_mouse_y_bits = 0;
    D->hid_mouse_wheel_bits = 0;
    D->hid_mouse_btn_size = 0;
    D->hid_mouse_x_size = 0;
    D->hid_mouse_y_size = 0;
    D->hid_mouse_wheel_size = 0;
    D->devctx = 0;
    D->input_ctx = 0;
    D->ctrl_tr = 0;
    D->bulk_in_tr = 0;
    D->bulk_out_tr = 0;
    D->intr_in_tr = 0;
    D->intr_buf = 0;
    D->intr_buf_len = 0;
    D->intr_pending_trbptr = 0;
    D->intr_pending_active = 0;

    for (int port_id = 1; port_id <= (int)G.n_ports; ++port_id)
    {
        if (port_is_claimed((uint8_t)port_id))
            continue;

        if (!(G.R.ports[port_id - 1].PORTSC & PORTSC_CCS))
            continue;

        if (enumerate_hid_boot_on_port(D, want_protocol, port_id) == 0)
            return 0;
    }

    xhci_dbg("hid enum: no matching HID boot device");
    return -1;
}

int usbh_enumerate_first_hid_keyboard(usbh_dev_t *D)
{
    return enumerate_first_hid_boot(D, 1);
}

int usbh_enumerate_first_hid(usbh_dev_t *D)
{
    return enumerate_first_hid_boot(D, 0);
}

int usbh_enumerate_first_hid_mouse(usbh_dev_t *D)
{
    return enumerate_first_hid_boot(D, 2);
}

/* ===== public enumeration (drives the dots) ===== */
static int enumerate_msc_on_port(usbh_dev_t *D, int port_id)
{
    if (port_id > 0)
    {
        G.port_id = port_id;
        if (reset_specific_connected_port(G.port_id))
        {
            dot(86, 0xFF0000u); /* failed before port reset success */
            return -1;
        }
    }
    else if (reset_first_connected_port(&G.port_id))
    {
        dot(86, 0xFF0000u); /* failed before port reset success */
        return -1;
    }

    dot(87, 0x00FFFFu); /* port reset succeeded (we have a port_id) */

    if (issue_enable_slot(&G.slot_id))
    {
        dot(88, 0xFF0000u); /* enable slot failed */
        return -1;
    }

    dot(89, 0x00FF00u); /* enable slot ok */

    if (!(G.R.ports[G.port_id - 1].PORTSC & PORTSC_CCS))
    {
        dot(10, 0xFF00FFu); /* no device connected, purple */
        return -1;
    }

    dot(90, 0xFFFF00u); /* CCS ok: device still connected */

    dot(83, 0xFFFF00u); /* about to ADDRESS_DEVICE (sets up EP0 + ctrl ring) */
    if (address_device())
    {
        dot(84, 0xFF0000u); /* ADDRESS_DEVICE failed */
        return -1;
    }
    dot(85, 0x00FF00u); /* ADDRESS_DEVICE ok, EP0 ready */

    /* ---- IMPORTANT: control transfer buffers must be low-DMA ---- */
    dot(91, 0xFFFF00u); /* about to alloc dd8 */
    uint8_t *dd8 = (uint8_t *)alloc_dma(8);
    if (!dd8)
        return -1;
    dot(92, 0x00FF00u); /* dd8 alloc ok */

    for (int i = 0; i < 8; i++)
        dd8[i] = 0;

    dot(93, 0xFFFF00u); /* GET_DESCRIPTOR dev(8) */
    if (control_xfer(0x80, 6, (DESC_DEV << 8) | 0, 0, dd8, 8))
        return -1;
    dot(94, 0x00FF00u); /* got dev(8) */

    uint8_t mps0 = dd8[7];
    if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64)
        mps0 = 8;

    dot(95, 0xFFFF00u); /* set EP0 MPS */
    if (ep0_evaluate_context_set_mps(mps0))
        return -1;
    dot(96, 0x00FF00u); /* EP0 MPS set ok */

    dot(99, 0xFFFF00u); /* about to alloc dev18 */
    uint8_t *dev = (uint8_t *)alloc_dma(18);
    if (!dev)
        return -1;
    dot(100, 0x00FF00u); /* dev18 alloc ok */

    for (int i = 0; i < 18; i++)
        dev[i] = 0;

    dot(101, 0xFFFF00u); /* GET_DESCRIPTOR dev(18) */
    if (control_xfer(0x80, 6, (DESC_DEV << 8) | 0, 0, dev, 18))
        return -1;
    dot(102, 0x00FF00u); /* got dev(18) */

    dot(103, 0xFFFF00u); /* about to alloc cfg9 */
    uint8_t *cfg9 = (uint8_t *)alloc_dma(9);
    if (!cfg9)
        return -1;
    dot(104, 0x00FF00u); /* cfg9 alloc ok */

    for (int i = 0; i < 9; i++)
        cfg9[i] = 0;

    dot(105, 0xFFFF00u); /* GET_DESCRIPTOR cfg(9) */
    if (control_xfer(0x80, 6, (DESC_CONFIG << 8) | 0, 0, cfg9, 9))
        return -1;
    dot(106, 0x00FF00u); /* got cfg(9) */

    uint16_t tot = (uint16_t)cfg9[2] | ((uint16_t)cfg9[3] << 8);
    if (tot < 9 || tot > 4096)
        return -1;

    dot(107, 0xFFFF00u); /* about to alloc full cfg */
    uint8_t *cfg = (uint8_t *)alloc_dma(tot);
    if (!cfg)
        return -1;
    dot(108, 0x00FF00u); /* full cfg alloc ok */

    for (uint32_t i = 0; i < tot; i++)
        cfg[i] = 0;

    dot(109, 0xFFFF00u); /* GET_DESCRIPTOR cfg(tot) */
    if (control_xfer(0x80, 6, (DESC_CONFIG << 8) | 0, 0, cfg, tot))
        return -1;
    dot(110, 0x00FF00u); /* got full cfg */

    dot(111, 0xFFFF00u); /* SET_CONFIGURATION */
    uint8_t cfgval = cfg9[5];
    if (control_xfer(0x00, 9, cfgval, 0, 0, 0))
        return -1;
    dot(112, 0x00FF00u); /* SET_CONFIGURATION ok */

    dot(113, 0xFFFF00u); /* parse_config_for_msc */
    msc_eps_t ep;
    if (parse_config_for_msc(cfg, tot, &ep))
        return -1;
    dot(114, 0x00FF00u); /* parse_config_for_msc ok */

    dot(115, 0xFFFF00u); /* configure_bulk_endpoints */
    if (configure_bulk_endpoints(ep.ep_in_num, ep.mps_in, ep.ep_out_num, ep.mps_out))
        return -1;
    dot(116, 0x00FF00u); /* bulk endpoints configured */

    dot(15, C_OK);

    D->configured = 1;
    D->addr = (uint8_t)G.slot_id;
    D->slot_id = (uint8_t)G.slot_id;
    D->port_id = (uint8_t)G.port_id;
    D->msc_if_num = ep.if_num;
    D->ep_bulk_in = ep.ep_in_num;
    D->ep_bulk_out = ep.ep_out_num;
    D->mps_bulk_in = ep.mps_in;
    D->mps_bulk_out = ep.mps_out;
    dev_state_save(D);
    port_claim((uint8_t)G.port_id);

    return 0;
}

int usbh_enumerate_first_msc(usbh_dev_t *D)
{
    dot(80, 0x00FF00u); /* entered usbh_------------enumerate_first_msc */

    if (!D)
        return -1;

    D->configured = 0;
    D->addr = 0;
    D->slot_id = 0;
    D->port_id = 0;
    D->msc_if_num = 0;
    D->ep_bulk_in = 0;
    D->ep_bulk_out = 0;
    D->mps_bulk_in = 0;
    D->mps_bulk_out = 0;
    D->devctx = 0;
    D->input_ctx = 0;
    D->ctrl_tr = 0;
    D->bulk_in_tr = 0;
    D->bulk_out_tr = 0;
    D->intr_in_tr = 0;
    D->intr_buf = 0;
    D->intr_buf_len = 0;
    D->intr_pending_trbptr = 0;
    D->intr_pending_active = 0;
    D->ctrl_enq = 0;
    D->bin_enq = 0;
    D->bout_enq = 0;
    D->iin_enq = 0;
    D->ctrl_cycle = 0;
    D->bin_cycle = 0;
    D->bout_cycle = 0;
    D->iin_cycle = 0;

    return enumerate_msc_on_port(D, 0);
}

/* ===== bulk helpers (used by MSC) ===== */
static trb_t *post_normal_trb(uint64_t ring_pa,
                              uint32_t *enq,
                              uint8_t *cycle,
                              void *buf,
                              uint32_t len)
{
    trb_t *ring = (trb_t *)(uintptr_t)ring_pa;
    trb_t *t = ring_next(ring, 256, enq, cycle);

    uint64_t p = (uint64_t)(uintptr_t)buf;

    t->d0 = (uint32_t)p;
    t->d1 = (uint32_t)(p >> 32);

    uint32_t xfer = (len & 0x1FFFFu); // 17-bit
    uint32_t td_size = 0u;            // single-TRB TD
    uint32_t intr = 0u;               // interrupter 0

    t->d2 = xfer | (td_size << 17) | (intr << 22);

    t->d3 = (TRB_TYPE_NORMAL << 10) |
            (1u << 5) |                // IOC
            ((uint32_t)(*cycle) & 1u); // Cycle

    dma_flush(t, sizeof(*t));
    return t;
}

// ============================================================
// Bulk OUT (with got) + legacy wrapper
// ============================================================

int usbh_bulk_out_got(usbh_dev_t *d, const void *buf, uint32_t len, uint32_t *got)
{
    int rc = -1;

    usbh_dbg_dot(160, 0x00FF00u); // bulk_out_got entered

    if (got)
        *got = 0;

    if (!d || !d->configured || !buf || len == 0)
    {
        usbh_dbg_dot(169, 0xFF0000u);
        return -1;
    }

    if (dev_state_load(d) != 0)
    {
        usbh_dbg_dot(169, 0xFF0000u);
        return -1;
    }

    dma_flush((void *)buf, len);
    usbh_dbg_dot(161, 0xFFFF00u);

    trb_t *t = post_normal_trb((uint64_t)G.bulk_out_tr, &G.bout_enq, &G.bout_cycle, (void *)buf, len);
    dma_flush((void *)(uintptr_t)G.bulk_out_tr, 256 * sizeof(trb_t));
    usbh_dbg_dot(162, 0xFFFF00u);

    // Expect exact Transfer Event for this TRB
    g_expect_trbptr = (uint64_t)(uintptr_t)t;
    g_expect_slot = (uint8_t)G.slot_id;
    g_expect_epid = dci_from_ep(d->ep_bulk_out, 0);

    g_expect_valid = 1;

    ring_ep_db((uint8_t)G.slot_id, g_expect_epid);

    int r = wait_xfer_complete(12000);

    // ALWAYS clear expectation before any return path
    g_expect_valid = 0;

    if (r != 0)
    {
        usbh_dbg_dot(176, 0xFF0000u);
        goto out;
    }

    // Compute "bytes done" from remaining.
    // Event remaining is "how many bytes of this TRB are left".
    uint32_t rem = g_last_xfer_rem;
    uint32_t done = (rem >= len) ? 0u : (len - rem);

    if (got)
        *got = done;

    // For Bulk OUT we *usually* require exact completion for correctness.
    // If done != len, treat as error so higher layers can recover.
    if (done != len)
    {
        usbh_dbg_dot(166, 0xFF0000u);
        goto out;
    }

    usbh_dbg_dot(165, 0x00FF00u);
    rc = 0;

out:
    dev_state_save(d);
    return rc;
}

// Legacy API: strict exact-length
int usbh_bulk_out(usbh_dev_t *d, const void *buf, uint32_t len)
{
    uint32_t got = 0;
    return usbh_bulk_out_got(d, buf, len, &got);
}

// ============================================================
// Bulk IN (with got) + legacy wrapper
// ============================================================

int usbh_bulk_in_got(usbh_dev_t *d, void *buf, uint32_t len, uint32_t *got)
{
    int rc = -1;

    usbh_dbg_dot(170, 0x00FF00u); // bulk_in_got entered

    if (got)
        *got = 0;

    if (!d || !d->configured || !buf || len == 0)
    {
        usbh_dbg_dot(179, 0xFF0000u);
        return -1;
    }

    if (dev_state_load(d) != 0)
    {
        usbh_dbg_dot(179, 0xFF0000u);
        return -1;
    }

    dma_invalidate(buf, len);
    usbh_dbg_dot(171, 0xFFFF00u);

    trb_t *t = post_normal_trb((uint64_t)G.bulk_in_tr, &G.bin_enq, &G.bin_cycle, buf, len);
    dma_flush((void *)(uintptr_t)G.bulk_in_tr, 256 * sizeof(trb_t));
    usbh_dbg_dot(172, 0xFFFF00u);

    g_expect_trbptr = (uint64_t)(uintptr_t)t;
    g_expect_slot = (uint8_t)G.slot_id;
    g_expect_epid = dci_from_ep(d->ep_bulk_in, 1);

    g_expect_valid = 1;

    ring_ep_db((uint8_t)G.slot_id, g_expect_epid);

    int r = wait_xfer_complete(12000);

    // ALWAYS clear expectation before any return path
    g_expect_valid = 0;

    if (r != 0)
    {
        usbh_dbg_dot(176, 0xFF0000u);
        goto out;
    }

    uint32_t rem = g_last_xfer_rem;

    /*
     * xHCI semantics:
     *  - rem = bytes NOT transferred
     *  - done = requested - remaining
     *  - rem == 0 means FULL transfer completed
     */
    uint32_t done = (rem <= len) ? (len - rem) : len;

    if (got)
        *got = done;

    /* Invalidate only what actually arrived */
    if (done)
        dma_invalidate(buf, done);

    /*
     * IMPORTANT:
     * Bulk IN transfers are allowed to be SHORT.
     * Do NOT treat short packets as errors.
     */
    usbh_dbg_dot(175, 0x00FF00u);
    rc = 0;

out:
    dev_state_save(d);
    return rc;
}

// Legacy API: strict exact-length
int usbh_bulk_in(usbh_dev_t *d, void *buf, uint32_t len)
{
    uint32_t got = 0;
    return usbh_bulk_in_got(d, buf, len, &got);
}

int usbh_intr_in_got(usbh_dev_t *d, void *buf, uint32_t len, uint32_t *got)
{
    int rc = -1;
    uint32_t xfer_len = 0;
    uint32_t timeout_ms = 0;
    uint32_t done = 0;

    if (got)
        *got = 0;

    if (!d || !d->configured || !buf || len == 0 || !d->ep_intr_in)
        return -1;

    if (dev_state_load(d) != 0)
        return -1;

    if (!d->intr_buf)
    {
        uint32_t alloc_len = d->mps_intr_in ? d->mps_intr_in : len;
        if (alloc_len < len)
            alloc_len = len;
        if (alloc_len < 16u)
            alloc_len = 16u;
        if (alloc_len > 64u)
            alloc_len = 64u;

        d->intr_buf = alloc_dma(alloc_len);
        if (!d->intr_buf)
            goto out;

        d->intr_buf_len = alloc_len;
        d->intr_pending_trbptr = 0;
        d->intr_pending_active = 0;
    }

    if (d->intr_buf_len == 0)
        d->intr_buf_len = len ? len : 16u;

    xfer_len = d->intr_buf_len;

    if (!d->intr_pending_active)
    {
        trb_t *t;

        dma_invalidate(d->intr_buf, xfer_len);
        t = post_normal_trb((uint64_t)G.intr_in_tr, &G.iin_enq, &G.iin_cycle, d->intr_buf, xfer_len);
        dma_flush((void *)(uintptr_t)G.intr_in_tr, 256 * sizeof(trb_t));

        d->intr_pending_trbptr = (uint64_t)(uintptr_t)t;
        d->intr_pending_active = 1;

        g_expect_trbptr = d->intr_pending_trbptr;
        g_expect_slot = (uint8_t)G.slot_id;
        g_expect_epid = dci_from_ep(d->ep_intr_in, 1);
        g_expect_valid = 1;

        ring_ep_db((uint8_t)G.slot_id, g_expect_epid);
    }
    else
    {
        g_expect_trbptr = d->intr_pending_trbptr;
        g_expect_slot = (uint8_t)G.slot_id;
        g_expect_epid = dci_from_ep(d->ep_intr_in, 1);
        g_expect_valid = 1;
    }

    timeout_ms = d->hid_poll_ms ? d->hid_poll_ms : 2u;
    {
        int r = wait_xfer_complete(timeout_ms);
        g_expect_valid = 0;

        if (r > 0)
        {
            // Treat as "no packet right now" rather than fatal.
            rc = 1;
            goto out;
        }

        if (r < 0)
        {
            /*
             * The controller completed this TRB with an error CC.
             * Drop the pending request so the next poll can post a fresh one
             * instead of waiting forever on a TRB that has already completed.
             */
            d->intr_pending_active = 0;
            d->intr_pending_trbptr = 0;
            rc = 1;
            goto out;
        }
    }

    d->intr_pending_active = 0;
    d->intr_pending_trbptr = 0;

    {
        uint32_t rem = g_last_xfer_rem;
        done = (rem <= xfer_len) ? (xfer_len - rem) : xfer_len;
    }

    if (done)
    {
        uint32_t copy_n = (done < len) ? done : len;

        dma_invalidate(d->intr_buf, done);
        copy_bytes(buf, d->intr_buf, copy_n);

        if (got)
            *got = copy_n;
    }

    rc = 0;

out:
    dev_state_save(d);
    return rc;
}

int usbh_intr_in(usbh_dev_t *d, void *buf, uint32_t len)
{
    uint32_t got = 0;
    return usbh_intr_in_got(d, buf, len, &got);
}

// -------- BOT Reset Recovery for USB MSC (Bulk-Only Transport) --------
// Returns 0 on success, -1 on failure.
int usbh_msc_bot_recover(usbh_dev_t *d)
{
    if (!d)
        return -1;

    if (usbh_control_xfer(d, 0x21, 0xFF, 0, (uint16_t)d->msc_if_num, 0, 0) != 0)
        return -1;

    // CLEAR_FEATURE(ENDPOINT_HALT) requires full endpoint address in wIndex
    uint16_t ep_in_addr = (uint16_t)(0x80u | (d->ep_bulk_in & 0x0Fu));
    uint16_t ep_out_addr = (uint16_t)(d->ep_bulk_out & 0x0Fu);

    if (usbh_control_xfer(d, 0x02, 0x01, 0, ep_in_addr, 0, 0) != 0)
        return -1;

    if (usbh_control_xfer(d, 0x02, 0x01, 0, ep_out_addr, 0, 0) != 0)
        return -1;

    return 0;
}

int usbh_poll(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++)
        __asm__ volatile("nop");
    return 0;
}
