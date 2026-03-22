#pragma once
#include <stdint.h>

/* Minimal xHCI register map (offsets). See xHCI 1.2 spec. */
typedef volatile struct
{
    uint32_t CAPLENGTH_HCIVERSION; // 0x00: [7:0] CAPLENGTH, [31:16] HCIVERSION
    uint32_t HCSPARAMS1;           // 0x04
    uint32_t HCSPARAMS2;           // 0x08
    uint32_t HCSPARAMS3;           // 0x0C
    uint32_t HCCPARAMS1;           // 0x10
    uint32_t DBOFF;                // 0x14 Doorbell offset
    uint32_t RTSOFF;               // 0x18 Runtime regs offset
    uint32_t HCCPARAMS2;           // 0x1C
} xhci_caps_t;

typedef volatile struct
{
    uint32_t USBCMD;   // 0x00
    uint32_t USBSTS;   // 0x04
    uint32_t PAGESIZE; // 0x08
    uint32_t rsvd0[2]; // 0x0C..0x13
    uint32_t DNCTRL;   // 0x14
    uint64_t CRCR;     // 0x18
    uint32_t rsvd1[4]; // 0x20..0x2F
    uint64_t DCBAAP;   // 0x30
    uint32_t CONFIG;   // 0x38
} xhci_op_t;

typedef volatile struct
{
    uint32_t PORTSC;    // 0x00
    uint32_t PORTPMSC;  // 0x04
    uint32_t PORTLI;    // 0x08
    uint32_t PORTHLPMC; // 0x0C
} xhci_port_t;

typedef volatile struct
{
    uint32_t MFINDEX;  // 0x00
    uint32_t rsvd0[7]; // 0x04..0x1F
    uint64_t ERSTSZ;   // 0x20
    uint64_t ERSTBA;   // 0x28
    uint64_t ERDP;     // 0x30
} xhci_rt_int_t;

typedef volatile struct
{
    uint32_t IMAN;   // 0x00
    uint32_t IMOD;   // 0x04
    uint32_t ERSTSZ; // 0x08
    uint32_t rsvd0;  // 0x0C
    uint64_t ERSTBA; // 0x10
    uint64_t ERDP;   // 0x18
} xhci_intr_regs_t;

typedef volatile struct
{
    xhci_caps_t *cap;            // mapped pointer to CAPS
    xhci_op_t *op;               // pointer to operational regs
    xhci_rt_int_t *rt;           // pointer to runtime
    volatile uint32_t *doorbell; // DB array (32-bit each)
    xhci_port_t *ports;          // first port regs
    uint32_t caplen;
    uint32_t n_ports;
} xhci_regs_t;

/* USBCMD bits */
#define USBCMD_RS (1u << 0)
#define USBCMD_HCRST (1u << 1)

/* USBSTS bits */
#define USBSTS_HCH (1u << 0)
#define USBSTS_HSE (1u << 2)
#define USBSTS_EINT (1u << 3)
#define USBSTS_PCD (1u << 4)

/* PORTSC bits */
#define PORTSC_CCS (1u << 0)
#define PORTSC_PED (1u << 1)
#define PORTSC_OCA (1u << 3)
#define PORTSC_PR (1u << 4)
#define PORTSC_PLS_MASK (0xF << 5)
#define PORTSC_PP (1u << 9)
#define PORTSC_SPEED_SHIFT 10
#define PORTSC_CSC (1u << 17)
#define PORTSC_PEC (1u << 18)
#define PORTSC_WRC (1u << 19)
#define PORTSC_PRC (1u << 21)
#define PORTSC_PLC (1u << 22)
#define PORTSC_CEC (1u << 23)
#define PORTSC_CAS (1u << 24)
#define PORTSC_WCE (1u << 25)
#define PORTSC_WDE (1u << 26)
#define PORTSC_WOE (1u << 27)
#define PORTSC_DR (1u << 30)

#ifndef PORTSC_LWS
#define PORTSC_LWS (1u << 16) /* Link Write Strobe */
#endif

#ifndef PORTSC_WPR
#define PORTSC_WPR (1u << 31) /* Warm Port Reset (USB3) */
#endif

/* TRB types (bits 10..15 of DW3) */
#define TRB_TYPE_NORMAL 1
#define TRB_TYPE_SETUP_STAGE 2
#define TRB_TYPE_DATA_STAGE 3
#define TRB_TYPE_STATUS_STAGE 4
#define TRB_TYPE_EVENT_DATA 5
#define TRB_TYPE_NOOP_CMD 8
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_ADDR_DEVICE 11
#define TRB_TYPE_CONFIG_EP 12
#define TRB_TYPE_EVAL_CTX 13
#define TRB_TYPE_RESET_EP 14
#define TRB_TYPE_STOP_EP 15
#define TRB_TYPE_SET_TR_DEQ 16
#define TRB_TYPE_TR_EV 32
#define TRB_TYPE_CMD_CMPL 33
#define TRB_TYPE_PORT_STATUS 34

/* Helper macros */
static inline uint32_t xhci_read32(volatile uint32_t *p) { return *p; }
static inline void xhci_write32(volatile uint32_t *p, uint32_t v) { *p = v; }
static inline uint64_t xhci_read64(volatile uint64_t *p) { return *p; }
static inline void xhci_write64(volatile uint64_t *p, uint64_t v) { *p = v; }
