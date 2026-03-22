// stage2.c — AArch64 UEFI Stage-2: find GOP, build BootInfo, load KERNEL.ELF, exit BS, jump.
#include <stdint.h>
#include <wchar.h>
#include "bootinfo.h"

/* -------------------------------------------------------------------------- */
/*  Basic UEFI definitions                                                    */
/* -------------------------------------------------------------------------- */
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

typedef struct
{
    uint32_t Data1;
    uint16_t Data2, Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef struct
{
    uint64_t Signature;
    uint32_t Revision, HeaderSize, CRC32, Reserved;
} EFI_TABLE_HEADER;

struct SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (*EFI_TEXT_STRING)(struct SIMPLE_TEXT_OUTPUT_PROTOCOL *, const wchar_t *);
typedef struct SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    void *Reset;
    EFI_TEXT_STRING OutputString;
    void *TestString, *QueryMode, *SetMode, *SetAttribute, *ClearScreen;
} SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_SYSTEM_TABLE
{
    EFI_TABLE_HEADER Hdr;
    wchar_t *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StdErrHandle;
    SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    void *BootServices;
    uint64_t NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* console helpers */
static void print(EFI_SYSTEM_TABLE *st, const wchar_t *s) { st->ConOut->OutputString(st->ConOut, s); }
static void println(EFI_SYSTEM_TABLE *st, const wchar_t *s)
{
    print(st, s);
    print(st, L"\r\n");
}
static void hex64(EFI_SYSTEM_TABLE *st, uint64_t v)
{
    wchar_t b[19];
    b[0] = L'0';
    b[1] = L'x';
    for (int i = 0; i < 16; i++)
    {
        int n = (int)((v >> ((15 - i) * 4)) & 0xF);
        b[2 + i] = (wchar_t)(n < 10 ? L'0' + n : L'A' + (n - 10));
    }
    b[18] = 0;
    println(st, b);
}

/* -------------------------------------------------------------------------- */
/*  Boot-service function pointer offsets (no headers)                        */
/* -------------------------------------------------------------------------- */
typedef EFI_STATUS (*EFI_OPEN_PROTOCOL)(EFI_HANDLE, const EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, uint32_t);
typedef EFI_STATUS (*EFI_LOCATE_PROTOCOL)(const EFI_GUID *, void *, void **);
typedef EFI_STATUS (*EFI_SET_WATCHDOG_TIMER)(UINTN, UINTN, UINTN, const wchar_t *);

static inline EFI_OPEN_PROTOCOL BsOpenProto(void *BS) { return *(EFI_OPEN_PROTOCOL *)((char *)BS + 0x118); }
static inline EFI_LOCATE_PROTOCOL BsLocate(void *BS) { return *(EFI_LOCATE_PROTOCOL *)((char *)BS + 0x140); }
static inline EFI_SET_WATCHDOG_TIMER BsWatchdog(void *BS) { return *(EFI_SET_WATCHDOG_TIMER *)((char *)BS + 0x100); }
enum
{
    EFI_OPEN_PROTOCOL_GET_PROTOCOL = 2
};

/* -------------------------------------------------------------------------- */
/*  GOP                                                                        */
/* -------------------------------------------------------------------------- */
typedef enum
{
    PixelRGBX = 0,
    PixelBGRX = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3
} EFI_GRAPHICS_PIXEL_FORMAT;
typedef struct
{
    uint32_t Version, HorizontalResolution, VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    struct
    {
        uint32_t RedMask, GreenMask, BlueMask, ReservedMask;
    } PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct
{
    uint32_t MaxMode, Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    void *QueryMode, *SetMode, *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;
static const EFI_GUID GOP_GUID = (EFI_GUID){0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

/* fwd decl so we can call fs_open_root() before its definition */
struct EFI_FILE_PROTOCOL;
EFI_STATUS fs_open_root(EFI_SYSTEM_TABLE *st,
                        EFI_HANDLE image,
                        struct EFI_FILE_PROTOCOL **out_root);

/* -------------------------------------------------------------------------- */
/*  ACPI helpers (RSDP→XSDT→MCFG)                                             */
/* -------------------------------------------------------------------------- */
typedef struct
{
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;
/* needed by stage2_load_kernel.c */
const EFI_GUID FILE_INFO_ID = (EFI_GUID){0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static const EFI_GUID ACPI_20_GUID = {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static const EFI_GUID ACPI_10_GUID = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    const uint64_t *pa = (const uint64_t *)a, *pb = (const uint64_t *)b;
    return pa[0] == pb[0] && pa[1] == pb[1];
}

#pragma pack(push, 1)
typedef struct
{
    char Sig[8];
    uint8_t Chk;
    char OEMID[6];
    uint8_t Rev;
    uint32_t Rsdt;
    uint32_t Length;
    uint64_t Xsdt;
} ACPI_RSDP;
typedef struct
{
    char Sig[4];
    uint32_t Length;
} ACPI_SDT;
typedef struct
{
    ACPI_SDT Hdr;
    uint64_t Entry[];
} ACPI_XSDT;
typedef struct
{
    ACPI_SDT Hdr;
    uint64_t Reserved;
    struct
    {
        uint64_t Base;
        uint16_t Seg;
        uint8_t BusStart, BusEnd;
        uint32_t _r;
    } entry[];
} ACPI_MCFG;
#pragma pack(pop)

static uint64_t find_rsdp(EFI_SYSTEM_TABLE *st)
{
    EFI_CONFIGURATION_TABLE *ct = (EFI_CONFIGURATION_TABLE *)st->ConfigurationTable;
    for (uint64_t i = 0; i < st->NumberOfTableEntries; i++)
    {
        if (guid_eq(&ct[i].VendorGuid, &ACPI_20_GUID) || guid_eq(&ct[i].VendorGuid, &ACPI_10_GUID))
            return (uint64_t)(uintptr_t)ct[i].VendorTable;
    }
    return 0;
}

/* --- tiny helpers to read \OS\xhci.txt with an ASCII hex address --- */
static int hex_nibble(wchar_t c)
{
    if (c >= L'0' && c <= L'9')
        return (int)(c - L'0');
    if (c >= L'a' && c <= L'f')
        return 10 + (int)(c - L'a');
    if (c >= L'A' && c <= L'F')
        return 10 + (int)(c - L'A');
    return -1;
}
static uint64_t parse_hex64_w(const wchar_t *s)
{
    /* accepts "0x..." or plain hex; stops on first non-hex */
    uint64_t v = 0;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X'))
        s += 2;
    for (; *s; ++s)
    {
        int n = hex_nibble(*s);
        if (n < 0)
            break;
        v = (v << 4) | (uint64_t)n;
    }
    return v;
}

/* Try to open \OS\xhci.txt and parse an MMIO address. Returns 0 if not found. */
static uint64_t try_xhci_override_from_file(EFI_SYSTEM_TABLE *st, EFI_HANDLE image)
{
    typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, const wchar_t *, UINTN, UINTN);
    typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);
    typedef EFI_STATUS (*T_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    typedef EFI_STATUS (*T_SET_POS)(EFI_FILE_PROTOCOL *, uint64_t);

    uint64_t result = 0;

    EFI_FILE_PROTOCOL *root = 0;
    if (fs_open_root(st, image, &root) != 0 || !root)
    {
        println(st, L"[S2:xHCI] override: root open failed");
        return 0;
    }

    /* vtbl slots (same scheme you used elsewhere) */
    T_OPEN FileOpen = *(void **)((char *)root + 0x08);
    T_CLOSE FileClose = *(void **)((char *)root + 0x10);

    /* open \OS\xhci.txt (matches your existing layout) */
    EFI_FILE_PROTOCOL *os = 0;
    if (FileOpen && !FileOpen(root, &os, L"OS", 0, 0) && os)
    {
        T_OPEN DirOpen = *(void **)((char *)os + 0x08);
        T_CLOSE DirClose = *(void **)((char *)os + 0x10);

        EFI_FILE_PROTOCOL *f = 0;
        if (DirOpen && !DirOpen(os, &f, L"xhci.txt", 0, 0) && f)
        {
            T_READ FileRead = *(void **)((char *)f + 0x20);
            T_SET_POS SetPos = *(void **)((char *)f + 0x18);

            unsigned char buf[128];
            UINTN sz = sizeof(buf) - 1;

            if (SetPos)
                SetPos(f, 0);

            if (FileRead && !FileRead(f, &sz, buf))
            {
                buf[sz] = 0;

                /* parse ASCII/UTF-8 hex: optional 0x, skips whitespace */
                const unsigned char *p = buf;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                    ++p;
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                    p += 2;

                uint64_t v = 0;
                int any = 0;
                for (;; ++p)
                {
                    unsigned char c = *p;
                    int n = -1;

                    if (c >= '0' && c <= '9')
                        n = (int)(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        n = (int)(c - 'a') + 10;
                    else if (c >= 'A' && c <= 'F')
                        n = (int)(c - 'A') + 10;
                    else
                        break;

                    v = (v << 4) | (uint64_t)n;
                    any = 1;
                }

                if (any && v)
                {
                    result = v;
                    println(st, L"[S2:xHCI] override: loaded from \\OS\\xhci.txt");
                    hex64(st, result);
                }
                else
                {
                    println(st, L"[S2:xHCI] override file present but parse failed");
                }
            }

            if (FileClose)
                FileClose(f);
        }

        if (DirClose)
            DirClose(os);
    }

    if (FileClose)
        FileClose(root);

    return result;
}

/* -------------------------------------------------------------------------- */
/*  xHCI discovery (PCI Root Bridge I/O first; falls back to MCFG/ECAM)       */
/* -------------------------------------------------------------------------- */
/* Set this to a known MMIO to bypass scanning (or pass -DXHCI_MMIO_OVERRIDE=0xXXXXXXXX) */
#ifndef XHCI_MMIO_OVERRIDE
#define XHCI_MMIO_OVERRIDE 0x000000000A600000ULL
#endif

static inline int sane(uint64_t p) { return (p >= 0x100000ULL) && (p < 0xFFFFFFFFFULL); }

/* ---------- xHCI capability header (just what we need) ---------- */
#pragma pack(push, 1)
typedef struct
{
    uint8_t CAPLENGTH; /* offset to operational regs */
    uint8_t _r0;
    uint16_t HCIVERSION; /* BCD */
    uint32_t HCSPARAMS1, HCSPARAMS2, HCSPARAMS3;
    uint32_t HCCPARAMS1;
    uint32_t DBOFF;  /* dword offset from base */
    uint32_t RTSOFF; /* dword offset from base */
    uint32_t HCCPARAMS2;
} XHCI_CAPS;
#pragma pack(pop)

static int xhci_caps_ok(EFI_SYSTEM_TABLE *st, uint64_t mmio)
{
    if (!sane(mmio))
        return 0;

    volatile const XHCI_CAPS *c = (volatile const XHCI_CAPS *)(uintptr_t)mmio;

    uint8_t cap = c->CAPLENGTH;
    uint16_t ver = c->HCIVERSION;
    uint32_t dboff = c->DBOFF & ~0x3u;
    uint32_t rtsoff = c->RTSOFF & ~0x1Fu;
    uint8_t max_ports = (uint8_t)(c->HCSPARAMS1 >> 24);

    /* xHCI 1.x uses major byte 0x01xx (e.g. 0x0100, 0x0110, 0x0120) */
    int ver_ok = ((ver & 0xFF00u) == 0x0100u);

    /* CAPLENGTH is normally 0x20..0x40; >0x80 is basically never real */
    int cap_ok = (cap >= 0x20u) && (cap <= 0x40u);

    /* Offsets must be aligned and reasonably small */
    int off_ok = (dboff >= 0x200u) && (dboff < 0x10000u) &&
                 (rtsoff >= 0x200u) && (rtsoff < 0x10000u);

    /* MaxPorts should be non-zero; >64 is highly unusual */
    int ports_ok = (max_ports != 0) && (max_ports <= 64);

    print(st, L"[S2:xHCI]   CAPLENGTH=");
    hex64(st, cap);
    print(st, L"[S2:xHCI]   HCIVERSION=");
    hex64(st, ver);
    print(st, L"[S2:xHCI]   DBOFF=");
    hex64(st, dboff);
    print(st, L"[S2:xHCI]   RTSOFF=");
    hex64(st, rtsoff);
    print(st, L"[S2:xHCI]   MaxPorts=");
    hex64(st, max_ports);

    if (!(ver_ok && cap_ok && off_ok && ports_ok))
    {
        println(st, L"[S2:xHCI]   rejected (caps sanity)");
        return 0;
    }

    println(st, L"[S2:xHCI]   accepted (caps sanity)");
    return 1;
}

static int bar_is_64_mmio(uint32_t bar0, uint32_t bar1)
{
    if (bar0 & 1)
        return 0; /* I/O space */
    if (((bar0 >> 1) & 3) != 2)
        return 0; /* not 64-bit mem BAR */
    if (bar0 == 0 && bar1 == 0)
        return 0; /* uninitialized */
    return 1;
}

/* ---------- Minimal PCI Root Bridge I/O protocol types ---------- */
/* GUID: EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL */
static const EFI_GUID PCI_ROOT_BRIDGE_IO_GUID =
    (EFI_GUID){0x2f707ebb, 0x4a1a, 0x11d4, {0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};

/* Width enum: we only use Uint32 (2) */
enum
{
    RB_WIDTH_U8 = 0,
    RB_WIDTH_U16 = 1,
    RB_WIDTH_U32 = 2,
    RB_WIDTH_U64 = 3
};

typedef struct EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;
typedef EFI_STATUS (*RB_READ_FN)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *, UINTN /*Width*/,
                                 uint64_t /*Address*/, UINTN /*Count*/, void * /*Buffer*/);
typedef struct
{
    RB_READ_FN Read;
    void *Write;
} RB_ACCESS;
struct EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
{
    void *PollMem, *PollIo;
    RB_ACCESS Mem;
    RB_ACCESS Io;
    RB_ACCESS Pci; /* <— we only need this .Pci.Read */
    /* the rest of the struct exists but we don’t touch it */
};

/* Encode a PCI config address for RBIO.Pci.Read */
#define EFI_PCI_ADDRESS(bus, dev, fn, reg) \
    ((((uint64_t)(bus)) << 24) | (((uint64_t)(dev)) << 16) | (((uint64_t)(fn)) << 8) | ((uint64_t)(reg)))

/* ---------- MCFG/ECAM structs (fallback) ---------- */
#pragma pack(push, 1)
typedef struct
{
    char Sig[4];
    uint32_t Length;
} ACPI_SDT_FBK;
typedef struct
{
    ACPI_SDT_FBK Hdr;
    uint64_t Entry[];
} ACPI_XSDT_FBK;
typedef struct
{
    ACPI_SDT_FBK Hdr;
    uint64_t Reserved;
    struct
    {
        uint64_t Base;
        uint16_t Seg;
        uint8_t BusStart, BusEnd;
        uint32_t _r;
    } entry[];
} ACPI_MCFG_FBK;
#pragma pack(pop)

/* Scan ACPI MCFG ECAM space for an xHCI controller BAR (returns MMIO base or 0) */
static uint64_t find_xhci_mmio_ecam(EFI_SYSTEM_TABLE *st, uint64_t rsdp_pa)
{
    (void)st;

    if (!rsdp_pa || !sane(rsdp_pa))
        return 0;

    ACPI_RSDP *R = (ACPI_RSDP *)(uintptr_t)rsdp_pa;
    if (!sane((uint64_t)R->Xsdt) || R->Xsdt == 0)
        return 0;

    ACPI_XSDT_FBK *X = (ACPI_XSDT_FBK *)(uintptr_t)R->Xsdt;
    if (!sane((uint64_t)(uintptr_t)X) || X->Hdr.Length < sizeof(ACPI_SDT_FBK))
        return 0;

    uint32_t entries = (X->Hdr.Length - (uint32_t)sizeof(ACPI_SDT_FBK)) / 8u;

    for (uint32_t i = 0; i < entries; ++i)
    {
        uint64_t spa = X->Entry[i];
        if (!sane(spa))
            continue;

        ACPI_SDT_FBK *H = (ACPI_SDT_FBK *)(uintptr_t)spa;
        if (!sane((uint64_t)(uintptr_t)H) || H->Length < sizeof(ACPI_SDT_FBK))
            continue;

        /* Look for "MCFG" */
        if (H->Sig[0] != 'M' || H->Sig[1] != 'C' || H->Sig[2] != 'F' || H->Sig[3] != 'G')
            continue;

        ACPI_MCFG_FBK *M = (ACPI_MCFG_FBK *)(uintptr_t)spa;
        if (M->Hdr.Length < sizeof(ACPI_MCFG_FBK))
            return 0;

        uint32_t seg_count = (M->Hdr.Length - (uint32_t)sizeof(ACPI_MCFG_FBK)) / (uint32_t)sizeof(M->entry[0]);

        for (uint32_t si = 0; si < seg_count; ++si)
        {
            uint64_t ecam = M->entry[si].Base;
            uint32_t bus_start = (uint32_t)M->entry[si].BusStart;
            uint32_t bus_end = (uint32_t)M->entry[si].BusEnd;

            if (!sane(ecam))
                continue;

            for (uint32_t bus = bus_start; bus <= bus_end; ++bus)
            {
                for (uint32_t dev = 0; dev < 32; ++dev)
                {
                    for (uint32_t fn = 0; fn < 8; ++fn)
                    {
                        uint64_t cfg = ecam + ((uint64_t)(bus - bus_start) << 20) + ((uint64_t)dev << 15) + ((uint64_t)fn << 12);

                        uint32_t id = *(volatile uint32_t *)(uintptr_t)(cfg + 0x00);
                        if (id == 0xFFFFFFFFu || id == 0x00000000u)
                            continue;

                        uint32_t cc = *(volatile uint32_t *)(uintptr_t)(cfg + 0x08);
                        uint8_t base = (uint8_t)(cc >> 24);
                        uint8_t sub = (uint8_t)(cc >> 16);
                        uint8_t prog = (uint8_t)(cc >> 8);

                        /* USB xHCI: class 0x0C, subclass 0x03, progIF 0x30 */
                        if (base != 0x0C || sub != 0x03 || prog != 0x30)
                            continue;

                        uint32_t bar0 = *(volatile uint32_t *)(uintptr_t)(cfg + 0x10);
                        uint32_t bar1 = *(volatile uint32_t *)(uintptr_t)(cfg + 0x14);

                        if (bar0 == 0 || bar0 == 0xFFFFFFFFu)
                            continue;

                        uint64_t mmio;
                        if (bar_is_64_mmio(bar0, bar1))
                            mmio = ((uint64_t)bar1 << 32) | (uint64_t)(bar0 & ~0xFu);
                        else
                            mmio = (uint64_t)(bar0 & ~0xFu);

                        if (mmio && sane(mmio))
                            return mmio;
                    }
                }
            }
        }

        return 0;
    }

    return 0;
}

/* ---------- main finder ---------- */
static uint64_t find_xhci_mmio(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, uint64_t rsdp_pa)
{
    uint64_t mmio = 0;

    /* ------------------------------------------------------------
       PRIMARY: Scan first (RBIO scan, then MCFG/ECAM scan)
       ------------------------------------------------------------ */

    /* 1) Try EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL scan (single instance) */
    println(st, L"[S2:xHCI] scan: RBIO");
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb = 0;
    EFI_LOCATE_PROTOCOL Locate = BsLocate(st->BootServices);

    if (Locate &&
        Locate(&PCI_ROOT_BRIDGE_IO_GUID, NULL, (void **)&rb) == 0 &&
        rb && rb->Pci.Read)
    {
        for (uint32_t bus = 0; bus < 256; ++bus)
        {
            for (uint32_t dev = 0; dev < 32; ++dev)
            {
                for (uint32_t fn = 0; fn < 8; ++fn)
                {
                    uint32_t id = 0xFFFFFFFFu;
                    if (rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x00), 1, &id))
                        continue;
                    if (id == 0xFFFFFFFFu || id == 0x00000000u)
                        continue;

                    uint32_t cc = 0;
                    if (rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x08), 1, &cc))
                        continue;

                    uint8_t base = (uint8_t)(cc >> 24);
                    uint8_t sub = (uint8_t)(cc >> 16);
                    uint8_t prog = (uint8_t)(cc >> 8);

                    /* USB xHCI: class 0x0C, subclass 0x03, progIF 0x30 */
                    if (base != 0x0C || sub != 0x03 || prog != 0x30)
                        continue;

                    uint32_t bar0 = 0, bar1 = 0;
                    (void)rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x10), 1, &bar0);
                    (void)rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x14), 1, &bar1);

                    if (bar0 == 0 || bar0 == 0xFFFFFFFFu)
                        continue;

                    if (bar_is_64_mmio(bar0, bar1))
                        mmio = ((uint64_t)bar1 << 32) | (uint64_t)(bar0 & ~0xFu);
                    else
                        mmio = (uint64_t)(bar0 & ~0xFu);

                    if (mmio)
                    {
                        hex64(st, mmio);
                        if (xhci_caps_ok(st, mmio))
                            return mmio;
                    }
                }
            }
        }
    }

    /* 2) Try ACPI MCFG/ECAM scan */
    println(st, L"[S2:xHCI] scan: MCFG/ECAM");
    mmio = find_xhci_mmio_ecam(st, rsdp_pa);
    if (mmio && xhci_caps_ok(st, mmio))
        return mmio;

    /* ------------------------------------------------------------
       FALLBACK: Only if scan failed, try file override then macro
       ------------------------------------------------------------ */

    /* file override (\OS\xhci.txt) */
    mmio = try_xhci_override_from_file(st, image);
    if (mmio)
    {
        println(st, L"[S2:xHCI] override(file):");
        hex64(st, mmio);
        if (xhci_caps_ok(st, mmio))
            return mmio;
        println(st, L"[S2:xHCI] override(file) rejected");
    }

    /* compile-time override */
    if (XHCI_MMIO_OVERRIDE)
    {
        mmio = XHCI_MMIO_OVERRIDE;
        println(st, L"[S2:xHCI] override(macro):");
        hex64(st, mmio);
        if (xhci_caps_ok(st, mmio))
            return mmio;
        println(st, L"[S2:xHCI] override(macro) rejected");
    }

    println(st, L"[S2:xHCI] no valid xHCI found");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  File-system root open (used by stage2_load_kernel.c)                      */
/* -------------------------------------------------------------------------- */
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct
{
    uint64_t pad;
    EFI_STATUS (*OpenVolume)(void *self, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
static const EFI_GUID SIMPLE_FS_GUID = {0x964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const EFI_GUID LOADED_IMAGE_GUID = {0x5B1B31A1, 0x9562, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static EFI_STATUS open_root_on_device_c(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, EFI_HANDLE dev, EFI_FILE_PROTOCOL **root_out)
{
    *root_out = 0;
    EFI_OPEN_PROTOCOL Open = BsOpenProto(st->BootServices);
    if (!Open)
        return 1;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = 0;
    EFI_STATUS s = Open(dev, &SIMPLE_FS_GUID, (void **)&sfs, image, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (s || !sfs)
        return s ? s : 1;
    return sfs->OpenVolume(sfs, root_out);
}

EFI_STATUS fs_open_root(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, EFI_FILE_PROTOCOL **out_root)
{
    *out_root = 0;
    void *BS = st->BootServices;
    EFI_OPEN_PROTOCOL Open = BsOpenProto(BS);
    EFI_LOCATE_PROTOCOL Locate = BsLocate(BS);
    if (!Open)
        return 1;

    EFI_STATUS s;
    void *loaded = 0;
    s = Open(image, &LOADED_IMAGE_GUID, &loaded, image, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (s || !loaded)
        return s ? s : 1;

    EFI_HANDLE device = *(EFI_HANDLE *)((char *)loaded + 0x18);
    if (!device)
    {
        EFI_HANDLE parent = *(EFI_HANDLE *)((char *)loaded + 0x08);
        if (parent)
        {
            void *pl = 0;
            if (!Open(parent, &LOADED_IMAGE_GUID, &pl, image, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL) && pl)
                device = *(EFI_HANDLE *)((char *)pl + 0x18);
        }
    }

    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, const wchar_t *, UINTN, UINTN);
    typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);

    if (device)
    {
        EFI_FILE_PROTOCOL *root = 0;
        s = open_root_on_device_c(st, image, device, &root);
        if (!s && root)
        {
            T_OPEN FileOpen = *(void **)((char *)root + 0x08);
            if (FileOpen)
            {
                EFI_FILE_PROTOCOL *probe = 0;
                if (!FileOpen(root, &probe, L"OS", 1, 0) && probe)
                {
                    T_CLOSE Close = *(void **)((char *)probe + 0x10);
                    if (Close)
                        Close(probe);
                    *out_root = root;
                    return 0;
                }
            }
            T_CLOSE CloseRoot = *(void **)((char *)root + 0x10);
            if (CloseRoot)
                CloseRoot(root);
        }
    }

    println(st, L"[S2] fallback: LocateProtocol(SimpleFS)...");
    if (Locate)
    {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs_any = 0;
        if (!Locate(&SIMPLE_FS_GUID, NULL, (void **)&sfs_any) && sfs_any)
        {
            EFI_FILE_PROTOCOL *root = 0;
            s = sfs_any->OpenVolume(sfs_any, &root);
            if (!s && root)
            {
                *out_root = root;
                return 0;
            }
        }
    }
    println(st, L"[S2] root open fail");
    return s ? s : (EFI_STATUS)~0ULL;
}

/* A helper some code may still call */
EFI_STATUS reopen_same_volume(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, void **out_root)
{
    EFI_OPEN_PROTOCOL Open = BsOpenProto(st->BootServices);
    void *loaded = 0;
    EFI_STATUS s = Open(image, &LOADED_IMAGE_GUID, &loaded, image, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (s)
        return s;
    EFI_HANDLE dev = *(EFI_HANDLE *)((char *)loaded + 0x18);
    if (!dev)
        return 1;
    void *sfs = 0;
    s = Open(dev, &SIMPLE_FS_GUID, &sfs, image, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (s)
        return s;
    EFI_STATUS (*OpenVol)(void *, void **) = *(void **)((char *)sfs + 8);
    return OpenVol(sfs, out_root);
}

/* -------------------------------------------------------------------------- */
/*  Externs from stage2_load_kernel.c                                         */
/* -------------------------------------------------------------------------- */
EFI_STATUS load_kernel_elf(EFI_SYSTEM_TABLE *, EFI_HANDLE, const wchar_t *, EFI_PHYSICAL_ADDRESS *, boot_info *);
EFI_STATUS exit_boot_and_jump(EFI_SYSTEM_TABLE *, EFI_HANDLE, const boot_info *, EFI_PHYSICAL_ADDRESS);

/* -------------------------------------------------------------------------- */
/*  Stage-2 entry                                                             */
/* -------------------------------------------------------------------------- */
EFI_STATUS EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    println(st, L"[CHAIN] Stage2 starting");

    EFI_SET_WATCHDOG_TIMER W = BsWatchdog(st->BootServices);
    if (W)
        W(0, 0, 0, 0);

    /* GOP */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS s = BsLocate(st->BootServices) ? BsLocate(st->BootServices)(&GOP_GUID, 0, (void **)&gop) : ~0ULL;
    if (s || !gop || !gop->Mode || !gop->Mode->Info)
    {
        println(st, L"GOP not found");
        for (;;)
        {
        }
    }

    boot_info bi = (boot_info){0};
    bi.version = 1;

    bi.fb.fb_base = (uint64_t)gop->Mode->FrameBufferBase;
    bi.fb.fb_size = (uint64_t)gop->Mode->FrameBufferSize;
    bi.fb.width = (uint32_t)gop->Mode->Info->HorizontalResolution;
    bi.fb.height = (uint32_t)gop->Mode->Info->VerticalResolution;
    bi.fb.pitch = (uint32_t)gop->Mode->Info->PixelsPerScanLine * 4u;
    bi.fb.pixel_format = (uint32_t)gop->Mode->Info->PixelFormat;
    bi.fb.rmask = gop->Mode->Info->PixelInformation.RedMask;
    bi.fb.gmask = gop->Mode->Info->PixelInformation.GreenMask;
    bi.fb.bmask = gop->Mode->Info->PixelInformation.BlueMask;

    println(st, L"[S2] GOP ok");

    /* ACPI + xHCI hint */
    bi.acpi_rsdp = find_rsdp(st);
    print(st, L"[S2] RSDP = ");
    hex64(st, bi.acpi_rsdp);

    bi.xhci_mmio_base = find_xhci_mmio(st, image, bi.acpi_rsdp);
    print(st, L"[S2] xHCI MMIO = ");
    hex64(st, bi.xhci_mmio_base);

    if (!bi.xhci_mmio_base)
    {
        println(st, L"[S2] WARNING: xHCI not found via MCFG/override (continuing without USB hint)");
    }
    else
    {
        /* Safe probe (only when MMIO is non-zero) */
        volatile uint8_t caplen = *(volatile uint8_t *)(uintptr_t)(bi.xhci_mmio_base + 0x00);
        volatile uint16_t hciver = *(volatile uint16_t *)(uintptr_t)(bi.xhci_mmio_base + 0x02);

        println(st, L"[S2:xHCI] probe:");
        print(st, L"  CAPLENGTH=");
        hex64(st, caplen);
        print(st, L"  HCIVERSION=");
        hex64(st, hciver);
    }

    /* Load kernel */
    EFI_PHYSICAL_ADDRESS entry = 0;
    s = load_kernel_elf(st, image, L"OS\\aa64\\KERNEL.ELF", &entry, &bi);
    if (s)
    {
        println(st, L"Kernel load failed");
        for (;;)
        {
        }
    }

    println(st, L"[CHAIN] ExitBootServices -> kernel");
    exit_boot_and_jump(st, image, &bi, entry);

    for (;;)
    {
    }
}
