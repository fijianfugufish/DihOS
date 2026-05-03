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

static boot_info *g_stage2_report_bi = 0;

static void stage2_report_append_wide(const wchar_t *s)
{
    if (!g_stage2_report_bi || !s)
        return;

    while (*s)
    {
        uint32_t len = g_stage2_report_bi->stage2_report_len;
        if (len + 1u >= BOOTINFO_STAGE2_REPORT_MAX)
            return;

        wchar_t wc = *s++;
        char c = (wc >= 32 && wc <= 126) ? (char)wc : '?';
        if (wc == L'\r' || wc == L'\n' || wc == L'\t')
            c = (char)wc;

        g_stage2_report_bi->stage2_report[len] = c;
        g_stage2_report_bi->stage2_report[len + 1u] = 0;
        g_stage2_report_bi->stage2_report_len = len + 1u;
    }
}

/* console helpers */
static void print(EFI_SYSTEM_TABLE *st, const wchar_t *s)
{
    stage2_report_append_wide(s);
    st->ConOut->OutputString(st->ConOut, s);
}
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
typedef EFI_STATUS (*EFI_LOCATE_HANDLE_BUFFER)(UINTN, const EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
typedef EFI_STATUS (*EFI_FREE_POOL)(void *);
typedef EFI_STATUS (*EFI_SET_WATCHDOG_TIMER)(UINTN, UINTN, UINTN, const wchar_t *);

static inline EFI_OPEN_PROTOCOL BsOpenProto(void *BS) { return *(EFI_OPEN_PROTOCOL *)((char *)BS + 0x118); }
static inline EFI_FREE_POOL BsFreePool(void *BS) { return *(EFI_FREE_POOL *)((char *)BS + 0x48); }
static inline EFI_LOCATE_HANDLE_BUFFER BsLocateHandles(void *BS) { return *(EFI_LOCATE_HANDLE_BUFFER *)((char *)BS + 0x138); }
static inline EFI_LOCATE_PROTOCOL BsLocate(void *BS) { return *(EFI_LOCATE_PROTOCOL *)((char *)BS + 0x140); }
static inline EFI_SET_WATCHDOG_TIMER BsWatchdog(void *BS) { return *(EFI_SET_WATCHDOG_TIMER *)((char *)BS + 0x100); }
enum
{
    EFI_OPEN_PROTOCOL_GET_PROTOCOL = 2,
    EFI_LOCATE_BY_PROTOCOL = 2
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

#if defined(__x86_64__) || defined(_M_X64)
#define DIHOS_STAGE2_BANNER L"[CHAIN] Stage2 x64 starting"
#define DIHOS_KERNEL_PATH L"OS\\x64\\KERNEL.ELF"
#else
#define DIHOS_STAGE2_BANNER L"[CHAIN] Stage2 aa64 starting"
#define DIHOS_KERNEL_PATH L"OS\\aa64\\KERNEL.ELF"
#endif

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

#ifndef TLMM_MMIO_OVERRIDE_BASE
#if defined(__x86_64__) || defined(_M_X64)
#define TLMM_MMIO_OVERRIDE_BASE 0ULL
#else
#define TLMM_MMIO_OVERRIDE_BASE 0x000000000F100000ULL
#endif
#endif

#ifndef TLMM_MMIO_OVERRIDE_SIZE
#if defined(__x86_64__) || defined(_M_X64)
#define TLMM_MMIO_OVERRIDE_SIZE 0ULL
#else
#define TLMM_MMIO_OVERRIDE_SIZE 0x0000000000F00000ULL
#endif
#endif

static uint64_t try_tlmm_override_from_file(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, uint64_t *size_out)
{
    typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, const wchar_t *, UINTN, UINTN);
    typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);
    typedef EFI_STATUS (*T_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    typedef EFI_STATUS (*T_SET_POS)(EFI_FILE_PROTOCOL *, uint64_t);

    uint64_t base = 0;
    uint64_t size = 0;

    if (size_out)
        *size_out = 0;

    EFI_FILE_PROTOCOL *root = 0;
    if (fs_open_root(st, image, &root) != 0 || !root)
    {
        println(st, L"[S2:TLMM] override: root open failed");
        return 0;
    }

    T_OPEN FileOpen = *(void **)((char *)root + 0x08);
    T_CLOSE FileClose = *(void **)((char *)root + 0x10);

    EFI_FILE_PROTOCOL *os = 0;
    if (FileOpen && !FileOpen(root, &os, L"OS", 0, 0) && os)
    {
        T_OPEN DirOpen = *(void **)((char *)os + 0x08);
        T_CLOSE DirClose = *(void **)((char *)os + 0x10);

        EFI_FILE_PROTOCOL *f = 0;
        if (DirOpen && !DirOpen(os, &f, L"tlmm.txt", 0, 0) && f)
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

                /*
                  Format:
                    <base>
                  or
                    <base> <size>
                  e.g.
                    0x0F100000 0x00F00000
                */
                const unsigned char *p = buf;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                    ++p;

                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                    p += 2;

                int any = 0;
                for (;; ++p)
                {
                    unsigned char c = *p;
                    int n = -1;

                    if (c >= '0' && c <= '9') n = (int)(c - '0');
                    else if (c >= 'a' && c <= 'f') n = (int)(c - 'a') + 10;
                    else if (c >= 'A' && c <= 'F') n = (int)(c - 'A') + 10;
                    else break;

                    base = (base << 4) | (uint64_t)n;
                    any = 1;
                }

                while (*p == ' ' || *p == '\t')
                    ++p;

                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                    p += 2;

                for (;; ++p)
                {
                    unsigned char c = *p;
                    int n = -1;

                    if (c >= '0' && c <= '9') n = (int)(c - '0');
                    else if (c >= 'a' && c <= 'f') n = (int)(c - 'a') + 10;
                    else if (c >= 'A' && c <= 'F') n = (int)(c - 'A') + 10;
                    else break;

                    size = (size << 4) | (uint64_t)n;
                }

                if (any && base)
                {
                    if (!size)
                        size = TLMM_MMIO_OVERRIDE_SIZE;

                    println(st, L"[S2:TLMM] override: loaded from \\OS\\tlmm.txt");
                    print(st, L"[S2:TLMM] base = ");
                    hex64(st, base);
                    print(st, L"[S2:TLMM] size = ");
                    hex64(st, size);
                }
                else
                {
                    base = 0;
                    size = 0;
                    println(st, L"[S2:TLMM] override file present but parse failed");
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

    if (size_out)
        *size_out = size;

    return base;
}

static void find_tlmm_mmio(EFI_SYSTEM_TABLE *st,
                           EFI_HANDLE image,
                           uint64_t *base_out,
                           uint64_t *size_out)
{
    uint64_t base = 0;
    uint64_t size = 0;

    if (base_out)
        *base_out = 0;
    if (size_out)
        *size_out = 0;

    /*
      We do NOT have a real runtime TLMM discovery path here yet.
      So for now:
      1) try file override
      2) fall back to compile-time override
    */
    base = try_tlmm_override_from_file(st, image, &size);
    if (base)
    {
        if (base_out) *base_out = base;
        if (size_out) *size_out = size;
        return;
    }

    if (TLMM_MMIO_OVERRIDE_BASE)
    {
        base = TLMM_MMIO_OVERRIDE_BASE;
        size = TLMM_MMIO_OVERRIDE_SIZE;

        println(st, L"[S2:TLMM] override(macro):");
        print(st, L"[S2:TLMM] base = ");
        hex64(st, base);
        print(st, L"[S2:TLMM] size = ");
        hex64(st, size);

        if (base_out) *base_out = base;
        if (size_out) *size_out = size;
        return;
    }

    println(st, L"[S2:TLMM] no TLMM MMIO provided");
}

typedef struct
{
    uint64_t base[BOOTINFO_XHCI_MMIO_MAX];
    uint32_t source[BOOTINFO_XHCI_MMIO_MAX];
    uint32_t count;
} XHCI_MMIO_LIST;

typedef struct
{
    uint16_t segment;
    uint8_t bus;
    uint8_t dev;
    uint8_t fn;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bar0_mmio_base;
} PCI_NIC_HINT;

typedef struct
{
    PCI_NIC_HINT item[BOOTINFO_PCI_NIC_MAX];
    uint32_t count;
} PCI_NIC_LIST;

static int xhci_list_try_add(EFI_SYSTEM_TABLE *st,
                             XHCI_MMIO_LIST *list,
                             uint64_t mmio,
                             const wchar_t *source_label,
                             uint32_t source,
                             int validate_caps);

/* -------------------------------------------------------------------------- */
/*  xHCI discovery (PCI Root Bridge I/O first; falls back to MCFG/ECAM)       */
/* -------------------------------------------------------------------------- */
static const uint64_t XHCI_BUILTIN_FALLBACK_MMIO[] = {
    0x000000000A600000ULL,
    0x000000000A000000ULL,
    0x000000000A800000ULL,
};

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

static int xhci_list_contains(const XHCI_MMIO_LIST *list, uint64_t mmio)
{
    for (uint32_t i = 0; list && i < list->count; ++i)
    {
        if ((list->base[i] & ~0xFULL) == (mmio & ~0xFULL))
            return 1;
    }
    return 0;
}

static int xhci_list_try_add(EFI_SYSTEM_TABLE *st,
                             XHCI_MMIO_LIST *list,
                             uint64_t mmio,
                             const wchar_t *source_label,
                             uint32_t source,
                             int validate_caps)
{
    if (!list || !mmio || !sane(mmio))
        return 0;

    mmio &= ~0xFULL;

    if (xhci_list_contains(list, mmio))
    {
        print(st, L"[S2:xHCI] duplicate ");
        print(st, source_label);
        print(st, L" MMIO = ");
        hex64(st, mmio);
        return 0;
    }

    print(st, L"[S2:xHCI] candidate ");
    print(st, source_label);
    print(st, L" MMIO = ");
    hex64(st, mmio);

    if (validate_caps && !xhci_caps_ok(st, mmio))
        return 0;
    if (!validate_caps)
        println(st, L"[S2:xHCI]   accepted (fallback, caps not probed)");

    if (list->count >= BOOTINFO_XHCI_MMIO_MAX)
    {
        println(st, L"[S2:xHCI] candidate accepted but list is full");
        return 0;
    }

    list->base[list->count] = mmio;
    list->source[list->count] = source;
    list->count++;
    print(st, L"[S2:xHCI] stored MMIO = ");
    hex64(st, mmio);
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

/* Scan ACPI MCFG ECAM space for xHCI controller BARs. */
static uint32_t find_xhci_mmio_ecam(EFI_SYSTEM_TABLE *st, uint64_t rsdp_pa, XHCI_MMIO_LIST *out)
{
    uint32_t before = out ? out->count : 0;

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
            continue;

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

                        (void)xhci_list_try_add(st, out, mmio, L"MCFG/ECAM", BOOTINFO_XHCI_SOURCE_DISCOVERED, 1);
                    }
                }
            }
        }
    }

    return out ? (out->count - before) : 0;
}

static uint32_t scan_xhci_mmio_acpi_raw(EFI_SYSTEM_TABLE *st,
                                        uint64_t rsdp_pa,
                                        XHCI_MMIO_LIST *out)
{
    uint32_t before = out ? out->count : 0;

    if (!out || !rsdp_pa || !sane(rsdp_pa))
        return 0;

    ACPI_RSDP *R = (ACPI_RSDP *)(uintptr_t)rsdp_pa;
    if (!R->Xsdt || !sane(R->Xsdt))
        return 0;

    ACPI_XSDT_FBK *X = (ACPI_XSDT_FBK *)(uintptr_t)R->Xsdt;
    if (!sane((uint64_t)(uintptr_t)X) || X->Hdr.Length < sizeof(ACPI_SDT_FBK))
        return 0;

    uint32_t entries = (X->Hdr.Length - (uint32_t)sizeof(ACPI_SDT_FBK)) / 8u;

    println(st, L"[S2:xHCI] scan: ACPI raw MMIO candidates");

    for (uint32_t i = 0; i < entries; ++i)
    {
        uint64_t spa = X->Entry[i];
        if (!sane(spa))
            continue;

        ACPI_SDT_FBK *H = (ACPI_SDT_FBK *)(uintptr_t)spa;
        if (!sane((uint64_t)(uintptr_t)H))
            continue;

        if (H->Length < sizeof(ACPI_SDT_FBK) || H->Length > (1024u * 1024u))
            continue;

        const uint8_t *b = (const uint8_t *)(uintptr_t)spa;

        /*
          Scan raw ACPI table bytes for little-endian 32-bit addresses.
          This is not full AML/_CRS parsing, but it catches platform MMIO
          constants embedded in DSDT/SSDT resource templates.
        */
        for (uint32_t off = 0; off + 4u <= H->Length; ++off)
        {
            uint32_t v32 =
                ((uint32_t)b[off + 0u]) |
                ((uint32_t)b[off + 1u] << 8) |
                ((uint32_t)b[off + 2u] << 16) |
                ((uint32_t)b[off + 3u] << 24);

            uint64_t mmio = (uint64_t)v32;

            /*
              Qualcomm-ish low MMIO window. Keep this tight so we do not
              randomly probe dangerous garbage.
            */
            if (mmio < 0x08000000ull || mmio > 0x0FFFFFFFull)
                continue;

            if ((mmio & 0xFFFu) != 0)
                continue;

            (void)xhci_list_try_add(st,
                                    out,
                                    mmio,
                                    L"ACPI/raw",
                                    BOOTINFO_XHCI_SOURCE_DISCOVERED,
                                    1);
        }
    }

    return out->count - before;
}

static uint64_t xhci_mmio_from_bars(uint32_t bar0, uint32_t bar1)
{
    if (bar0 == 0 || bar0 == 0xFFFFFFFFu)
        return 0;
    if (bar0 & 1u)
        return 0;

    if (bar_is_64_mmio(bar0, bar1))
        return ((uint64_t)bar1 << 32) | (uint64_t)(bar0 & ~0xFULL);

    return (uint64_t)(bar0 & ~0xFULL);
}

static uint64_t pci_bar0_mmio_from_bars(uint32_t bar0, uint32_t bar1)
{
    if (!bar0 || bar0 == 0xFFFFFFFFu)
        return 0;
    if (bar0 & 1u)
        return 0;
    if (bar_is_64_mmio(bar0, bar1))
        return ((uint64_t)bar1 << 32) | (uint64_t)(bar0 & ~0xFULL);
    return (uint64_t)(bar0 & ~0xFULL);
}

static int nic_list_contains(const PCI_NIC_LIST *list, uint8_t bus, uint8_t dev, uint8_t fn)
{
    if (!list)
        return 0;
    for (uint32_t i = 0; i < list->count; ++i)
    {
        const PCI_NIC_HINT *n = &list->item[i];
        if (n->bus == bus && n->dev == dev && n->fn == fn)
            return 1;
    }
    return 0;
}

static void nic_list_try_add(EFI_SYSTEM_TABLE *st,
                             PCI_NIC_LIST *list,
                             uint8_t bus,
                             uint8_t dev,
                             uint8_t fn,
                             uint16_t vendor,
                             uint16_t device,
                             uint8_t class_code,
                             uint8_t subclass,
                             uint8_t prog_if,
                             uint64_t bar0_mmio_base)
{
    if (!list)
        return;

    if (nic_list_contains(list, bus, dev, fn))
        return;

    if (list->count >= BOOTINFO_PCI_NIC_MAX)
    {
        println(st, L"[S2:NIC] list full");
        return;
    }

    PCI_NIC_HINT *n = &list->item[list->count++];
    n->segment = 0;
    n->bus = bus;
    n->dev = dev;
    n->fn = fn;
    n->class_code = class_code;
    n->subclass = subclass;
    n->prog_if = prog_if;
    n->vendor_id = vendor;
    n->device_id = device;
    n->bar0_mmio_base = bar0_mmio_base;

    print(st, L"[S2:NIC] found bdf=");
    hex64(st, ((uint64_t)bus << 16) | ((uint64_t)dev << 8) | fn);
    print(st, L"[S2:NIC] vendor=");
    hex64(st, vendor);
    print(st, L"[S2:NIC] device=");
    hex64(st, device);
    print(st, L"[S2:NIC] class=");
    hex64(st, class_code);
    print(st, L"[S2:NIC] subclass=");
    hex64(st, subclass);
    print(st, L"[S2:NIC] progif=");
    hex64(st, prog_if);
    print(st, L"[S2:NIC] bar0(mmio)=");
    hex64(st, bar0_mmio_base);
}

static void scan_xhci_rbio_instance(EFI_SYSTEM_TABLE *st,
                                    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb,
                                    XHCI_MMIO_LIST *out)
{
    if (!rb || !rb->Pci.Read)
        return;

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

                (void)xhci_list_try_add(st, out, xhci_mmio_from_bars(bar0, bar1), L"RBIO", BOOTINFO_XHCI_SOURCE_DISCOVERED, 1);
            }
        }
    }
}

static uint32_t scan_xhci_mmio_rbio(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, XHCI_MMIO_LIST *out)
{
    uint32_t before = out ? out->count : 0;
    void *BS = st->BootServices;
    EFI_LOCATE_HANDLE_BUFFER LocateHandles = BsLocateHandles(BS);
    EFI_LOCATE_PROTOCOL Locate = BsLocate(BS);
    EFI_OPEN_PROTOCOL Open = BsOpenProto(BS);
    EFI_FREE_POOL FreePool = BsFreePool(BS);
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0;

    if (LocateHandles &&
        LocateHandles(EFI_LOCATE_BY_PROTOCOL,
                      &PCI_ROOT_BRIDGE_IO_GUID,
                      NULL,
                      &handle_count,
                      &handles) == 0 &&
        handles &&
        handle_count)
    {
        for (UINTN i = 0; i < handle_count; ++i)
        {
            EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb = 0;
            if (Open &&
                Open(handles[i],
                     &PCI_ROOT_BRIDGE_IO_GUID,
                     (void **)&rb,
                     image,
                     0,
                     EFI_OPEN_PROTOCOL_GET_PROTOCOL) == 0)
            {
                scan_xhci_rbio_instance(st, rb, out);
            }
        }

        if (FreePool)
            FreePool(handles);
    }
    else
    {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb = 0;
        if (Locate &&
            Locate(&PCI_ROOT_BRIDGE_IO_GUID, NULL, (void **)&rb) == 0)
        {
            scan_xhci_rbio_instance(st, rb, out);
        }
    }

    return out ? (out->count - before) : 0;
}

static void scan_nics_rbio_instance(EFI_SYSTEM_TABLE *st,
                                    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb,
                                    PCI_NIC_LIST *out,
                                    uint32_t *visible_count,
                                    uint32_t *sample_count)
{
    if (!rb || !rb->Pci.Read || !out)
        return;

    for (uint32_t bus = 0; bus < 256; ++bus)
    {
        for (uint32_t dev = 0; dev < 32; ++dev)
        {
            for (uint32_t fn = 0; fn < 8; ++fn)
            {
                uint32_t id = 0xFFFFFFFFu;
                uint32_t cc = 0;
                uint32_t bar0 = 0;
                uint32_t bar1 = 0;
                uint16_t vendor;
                uint16_t device;
                uint8_t class_code;
                uint8_t subclass;
                uint8_t prog_if;
                uint64_t bar0_mmio;

                if (rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x00), 1, &id))
                    continue;
                if (id == 0xFFFFFFFFu || id == 0x00000000u)
                    continue;

                if (visible_count)
                    (*visible_count)++;

                if (rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x08), 1, &cc))
                    continue;

                class_code = (uint8_t)(cc >> 24);
                subclass = (uint8_t)(cc >> 16);
                prog_if = (uint8_t)(cc >> 8);

                if (sample_count && *sample_count < 12u)
                {
                    (*sample_count)++;
                    print(st, L"[S2:PCI] RBIO vis bdf=");
                    hex64(st, ((uint64_t)bus << 16) | ((uint64_t)dev << 8) | fn);
                    print(st, L"[S2:PCI] class=");
                    hex64(st, class_code);
                    print(st, L"[S2:PCI] sub=");
                    hex64(st, subclass);
                    print(st, L"[S2:PCI] if=");
                    hex64(st, prog_if);
                    print(st, L"[S2:PCI] vid=");
                    hex64(st, (uint16_t)(id & 0xFFFFu));
                    print(st, L"[S2:PCI] did=");
                    hex64(st, (uint16_t)((id >> 16) & 0xFFFFu));
                }

                if (class_code != 0x02u)
                    continue;

                vendor = (uint16_t)(id & 0xFFFFu);
                device = (uint16_t)((id >> 16) & 0xFFFFu);

                (void)rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x10), 1, &bar0);
                (void)rb->Pci.Read(rb, RB_WIDTH_U32, EFI_PCI_ADDRESS(bus, dev, fn, 0x14), 1, &bar1);
                bar0_mmio = pci_bar0_mmio_from_bars(bar0, bar1);

                nic_list_try_add(st, out,
                                 (uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                 vendor, device,
                                 class_code, subclass, prog_if,
                                 bar0_mmio);
            }
        }
    }
}

static uint32_t find_pci_nics_rbio(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, PCI_NIC_LIST *out)
{
    uint32_t before;
    void *BS;
    EFI_LOCATE_HANDLE_BUFFER LocateHandles;
    EFI_LOCATE_PROTOCOL Locate;
    EFI_OPEN_PROTOCOL Open;
    EFI_FREE_POOL FreePool;
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0;
    uint32_t visible_count = 0;
    uint32_t sample_count = 0;

    if (!out)
        return 0;

    before = out->count;
    BS = st->BootServices;
    LocateHandles = BsLocateHandles(BS);
    Locate = BsLocate(BS);
    Open = BsOpenProto(BS);
    FreePool = BsFreePool(BS);

    println(st, L"[S2:NIC] scan: RBIO");

    if (LocateHandles &&
        LocateHandles(EFI_LOCATE_BY_PROTOCOL,
                      &PCI_ROOT_BRIDGE_IO_GUID,
                      NULL,
                      &handle_count,
                      &handles) == 0 &&
        handles &&
        handle_count)
    {
        for (UINTN i = 0; i < handle_count; ++i)
        {
            EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb = 0;
            if (Open &&
                Open(handles[i],
                     &PCI_ROOT_BRIDGE_IO_GUID,
                     (void **)&rb,
                     image,
                     0,
                     EFI_OPEN_PROTOCOL_GET_PROTOCOL) == 0)
            {
                scan_nics_rbio_instance(st, rb, out, &visible_count, &sample_count);
            }
        }

        if (FreePool)
            FreePool(handles);
    }
    else
    {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *rb = 0;
        if (Locate &&
            Locate(&PCI_ROOT_BRIDGE_IO_GUID, NULL, (void **)&rb) == 0)
        {
            scan_nics_rbio_instance(st, rb, out, &visible_count, &sample_count);
        }
    }

    print(st, L"[S2:PCI] RBIO visible fn count = ");
    hex64(st, visible_count);
    print(st, L"[S2:NIC] discovered count = ");
    hex64(st, out->count);
    return out->count - before;
}

static uint32_t find_pci_nics_ecam(EFI_SYSTEM_TABLE *st, uint64_t rsdp_pa, PCI_NIC_LIST *out)
{
    uint32_t before = out ? out->count : 0;
    uint32_t visible_count = 0;
    uint32_t sample_count = 0;

    if (!out || !rsdp_pa || !sane(rsdp_pa))
        return 0;

    ACPI_RSDP *R = (ACPI_RSDP *)(uintptr_t)rsdp_pa;
    if (!R->Xsdt || !sane(R->Xsdt))
        return 0;

    ACPI_XSDT_FBK *X = (ACPI_XSDT_FBK *)(uintptr_t)R->Xsdt;
    if (!sane((uint64_t)(uintptr_t)X) || X->Hdr.Length < sizeof(ACPI_SDT_FBK))
        return 0;

    println(st, L"[S2:NIC] scan: MCFG/ECAM");

    uint32_t entries = (X->Hdr.Length - (uint32_t)sizeof(ACPI_SDT_FBK)) / 8u;
    for (uint32_t i = 0; i < entries; ++i)
    {
        uint64_t spa = X->Entry[i];
        if (!sane(spa))
            continue;

        ACPI_SDT_FBK *H = (ACPI_SDT_FBK *)(uintptr_t)spa;
        if (!sane((uint64_t)(uintptr_t)H) || H->Length < sizeof(ACPI_SDT_FBK))
            continue;

        if (H->Sig[0] != 'M' || H->Sig[1] != 'C' || H->Sig[2] != 'F' || H->Sig[3] != 'G')
            continue;

        ACPI_MCFG_FBK *M = (ACPI_MCFG_FBK *)(uintptr_t)spa;
        if (M->Hdr.Length < sizeof(ACPI_MCFG_FBK))
            continue;

        uint32_t seg_count = (M->Hdr.Length - (uint32_t)sizeof(ACPI_MCFG_FBK)) / (uint32_t)sizeof(M->entry[0]);
        for (uint32_t si = 0; si < seg_count; ++si)
        {
            uint64_t ecam = M->entry[si].Base;
            uint32_t bus_start = (uint32_t)M->entry[si].BusStart;
            uint32_t bus_end = (uint32_t)M->entry[si].BusEnd;
            uint16_t seg = M->entry[si].Seg;

            if (!sane(ecam))
                continue;

            for (uint32_t bus = bus_start; bus <= bus_end; ++bus)
            {
                for (uint32_t dev = 0; dev < 32; ++dev)
                {
                    for (uint32_t fn = 0; fn < 8; ++fn)
                    {
                        uint64_t cfg = ecam + ((uint64_t)(bus - bus_start) << 20) +
                                       ((uint64_t)dev << 15) + ((uint64_t)fn << 12);
                        uint32_t id = *(volatile uint32_t *)(uintptr_t)(cfg + 0x00);
                        uint32_t cc;
                        uint8_t class_code;
                        uint8_t subclass;
                        uint8_t prog_if;
                        uint32_t bar0, bar1;
                        uint64_t bar0_mmio;

                        if (id == 0xFFFFFFFFu || id == 0x00000000u)
                            continue;

                        visible_count++;

                        cc = *(volatile uint32_t *)(uintptr_t)(cfg + 0x08);
                        class_code = (uint8_t)(cc >> 24);
                        subclass = (uint8_t)(cc >> 16);
                        prog_if = (uint8_t)(cc >> 8);

                        if (sample_count < 12u)
                        {
                            sample_count++;
                            print(st, L"[S2:PCI] ECAM vis seg=");
                            hex64(st, seg);
                            print(st, L"[S2:PCI] bdf=");
                            hex64(st, ((uint64_t)bus << 16) | ((uint64_t)dev << 8) | fn);
                            print(st, L"[S2:PCI] class=");
                            hex64(st, class_code);
                            print(st, L"[S2:PCI] sub=");
                            hex64(st, subclass);
                            print(st, L"[S2:PCI] if=");
                            hex64(st, prog_if);
                            print(st, L"[S2:PCI] vid=");
                            hex64(st, (uint16_t)(id & 0xFFFFu));
                            print(st, L"[S2:PCI] did=");
                            hex64(st, (uint16_t)((id >> 16) & 0xFFFFu));
                        }

                        if (class_code != 0x02u)
                            continue;

                        bar0 = *(volatile uint32_t *)(uintptr_t)(cfg + 0x10);
                        bar1 = *(volatile uint32_t *)(uintptr_t)(cfg + 0x14);
                        bar0_mmio = pci_bar0_mmio_from_bars(bar0, bar1);

                        if (nic_list_contains(out, (uint8_t)bus, (uint8_t)dev, (uint8_t)fn))
                            continue;

                        if (out->count >= BOOTINFO_PCI_NIC_MAX)
                        {
                            println(st, L"[S2:NIC] list full");
                            return out->count - before;
                        }

                        PCI_NIC_HINT *n = &out->item[out->count++];
                        n->segment = seg;
                        n->bus = (uint8_t)bus;
                        n->dev = (uint8_t)dev;
                        n->fn = (uint8_t)fn;
                        n->class_code = class_code;
                        n->subclass = subclass;
                        n->prog_if = prog_if;
                        n->vendor_id = (uint16_t)(id & 0xFFFFu);
                        n->device_id = (uint16_t)((id >> 16) & 0xFFFFu);
                        n->bar0_mmio_base = bar0_mmio;

                        print(st, L"[S2:NIC] found(ECAM) seg=");
                        hex64(st, seg);
                        print(st, L"[S2:NIC] bdf=");
                        hex64(st, ((uint64_t)bus << 16) | ((uint64_t)dev << 8) | fn);
                        print(st, L"[S2:NIC] vendor=");
                        hex64(st, n->vendor_id);
                        print(st, L"[S2:NIC] device=");
                        hex64(st, n->device_id);
                        print(st, L"[S2:NIC] bar0(mmio)=");
                        hex64(st, bar0_mmio);
                    }
                }
            }
        }
    }

    print(st, L"[S2:PCI] ECAM visible fn count = ");
    hex64(st, visible_count);
    return out->count - before;
}

/* ---------- main finder ---------- */
static uint32_t find_xhci_mmios(EFI_SYSTEM_TABLE *st,
                                EFI_HANDLE image,
                                uint64_t rsdp_pa,
                                XHCI_MMIO_LIST *out)
{
    if (!out)
        return 0;

    out->count = 0;
    for (uint32_t i = 0; i < BOOTINFO_XHCI_MMIO_MAX; ++i)
    {
        out->base[i] = 0;
        out->source[i] = 0;
    }

    /* PRIMARY: search first. Built-ins are used only when discovery fails. */
    println(st, L"[S2:xHCI] scan: RBIO");
    (void)scan_xhci_mmio_rbio(st, image, out);

    println(st, L"[S2:xHCI] scan: MCFG/ECAM");
    (void)find_xhci_mmio_ecam(st, rsdp_pa, out);

    (void)scan_xhci_mmio_acpi_raw(st, rsdp_pa, out);

    if (out->count)
    {
        print(st, L"[S2:xHCI] discovered count = ");
        hex64(st, out->count);
        return out->count;
    }

    println(st, L"[S2:xHCI] scan found none; trying built-in fallback bases");
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(XHCI_BUILTIN_FALLBACK_MMIO) / sizeof(XHCI_BUILTIN_FALLBACK_MMIO[0])); ++i)
        {
            (void)xhci_list_try_add(st,
                                    out,
                                    XHCI_BUILTIN_FALLBACK_MMIO[i],
                                    L"fallback(builtin)",
                                    BOOTINFO_XHCI_SOURCE_FALLBACK_BUILTIN,
                                    0);
        }
    }

    if (!out->count)
        println(st, L"[S2:xHCI] no valid xHCI found");

    return out->count;
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
    println(st, DIHOS_STAGE2_BANNER);

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
    bi.stage2_report_len = 0;
    bi.stage2_report[0] = 0;
    g_stage2_report_bi = &bi;

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

    XHCI_MMIO_LIST xhci_mmios;
    PCI_NIC_LIST nic_list = {0};
    bi.xhci_mmio_count = find_xhci_mmios(st, image, bi.acpi_rsdp, &xhci_mmios);
    if (bi.xhci_mmio_count > BOOTINFO_XHCI_MMIO_MAX)
        bi.xhci_mmio_count = BOOTINFO_XHCI_MMIO_MAX;

    for (uint32_t i = 0; i < bi.xhci_mmio_count; ++i)
    {
        bi.xhci_mmio_bases[i] = xhci_mmios.base[i];
        bi.xhci_mmio_sources[i] = xhci_mmios.source[i];
    }

    bi.xhci_mmio_base = bi.xhci_mmio_count ? bi.xhci_mmio_bases[0] : 0;

    print(st, L"[S2] xHCI MMIO count = ");
    hex64(st, bi.xhci_mmio_count);
    for (uint32_t i = 0; i < bi.xhci_mmio_count; ++i)
    {
        print(st, L"[S2] xHCI MMIO = ");
        hex64(st, bi.xhci_mmio_bases[i]);
        print(st, L"[S2] xHCI source = ");
        hex64(st, bi.xhci_mmio_sources[i]);
    }

    if (!bi.xhci_mmio_count)
    {
        println(st, L"[S2] WARNING: xHCI not found via search/override (continuing without USB hint)");
    }
    else if (bi.xhci_mmio_sources[0] == BOOTINFO_XHCI_SOURCE_DISCOVERED)
    {
        /* Safe probe (only when MMIO is non-zero) */
        volatile uint8_t caplen = *(volatile uint8_t *)(uintptr_t)(bi.xhci_mmio_bases[0] + 0x00);
        volatile uint16_t hciver = *(volatile uint16_t *)(uintptr_t)(bi.xhci_mmio_bases[0] + 0x02);

        println(st, L"[S2:xHCI] probe:");
        print(st, L"  CAPLENGTH=");
        hex64(st, caplen);
        print(st, L"  HCIVERSION=");
        hex64(st, hciver);
    }
    else
    {
        println(st, L"[S2:xHCI] probe skipped for fallback MMIO");
    }

    (void)find_pci_nics_rbio(st, image, &nic_list);
    (void)find_pci_nics_ecam(st, bi.acpi_rsdp, &nic_list);
    print(st, L"[S2:NIC] total discovered = ");
    hex64(st, nic_list.count);
    bi.pci_nic_count = nic_list.count;
    if (bi.pci_nic_count > BOOTINFO_PCI_NIC_MAX)
        bi.pci_nic_count = BOOTINFO_PCI_NIC_MAX;

    for (uint32_t i = 0; i < bi.pci_nic_count; ++i)
    {
        bi.pci_nics[i].segment = nic_list.item[i].segment;
        bi.pci_nics[i].bus = nic_list.item[i].bus;
        bi.pci_nics[i].dev = nic_list.item[i].dev;
        bi.pci_nics[i].fn = nic_list.item[i].fn;
        bi.pci_nics[i].class_code = nic_list.item[i].class_code;
        bi.pci_nics[i].subclass = nic_list.item[i].subclass;
        bi.pci_nics[i].prog_if = nic_list.item[i].prog_if;
        bi.pci_nics[i].vendor_id = nic_list.item[i].vendor_id;
        bi.pci_nics[i].device_id = nic_list.item[i].device_id;
        bi.pci_nics[i].bar0_mmio_base = nic_list.item[i].bar0_mmio_base;
    }

    find_tlmm_mmio(st, image, &bi.tlmm_mmio_base, &bi.tlmm_mmio_size);
    print(st, L"[S2] TLMM MMIO base = ");
    hex64(st, bi.tlmm_mmio_base);
    print(st, L"[S2] TLMM MMIO size = ");
    hex64(st, bi.tlmm_mmio_size);

    /* Load kernel */
    EFI_PHYSICAL_ADDRESS entry = 0;
    s = load_kernel_elf(st, image, DIHOS_KERNEL_PATH, &entry, &bi);
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
