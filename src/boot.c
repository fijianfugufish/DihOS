// boot_chain.c — AA64 UEFI chainloader: \EFI\BOOT\BOOTAA64.EFI -> \OS\aa64\STAGE2.EFI
#include <stdint.h>
#include <wchar.h>

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

/* Text output (only what we call) */
struct SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (*EFI_TEXT_RESET)(struct SIMPLE_TEXT_OUTPUT_PROTOCOL *, int);
typedef EFI_STATUS (*EFI_TEXT_STRING)(struct SIMPLE_TEXT_OUTPUT_PROTOCOL *, const wchar_t *);
typedef EFI_STATUS (*EFI_TEXT_CLEAR)(struct SIMPLE_TEXT_OUTPUT_PROTOCOL *);
typedef struct SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    EFI_TEXT_RESET Reset;         // +0x00
    EFI_TEXT_STRING OutputString; // +0x08
    void *TestString;             // keep layout
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    EFI_TEXT_CLEAR ClearScreen; // +0x30
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
    EFI_HANDLE StandardErrorHandle;
    SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    void *BootServices; // opaque; we use slot offsets
    uint64_t NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---------- Boot Services slots (64-bit layout, same as EDK2) ---------- */
typedef EFI_STATUS (*EFI_SET_WATCHDOG_TIMER)(UINTN, UINTN, UINTN, const wchar_t *);                               // +0x100
typedef EFI_STATUS (*EFI_OPEN_PROTOCOL)(EFI_HANDLE, const EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, uint32_t); // +0x118
typedef EFI_STATUS (*EFI_ALLOCATE_POOL)(uint32_t, UINTN, void **);                                                // +0x40
typedef EFI_STATUS (*EFI_LOAD_IMAGE)(int, EFI_HANDLE, void *, void *, UINTN, EFI_HANDLE *);                       // +0xC8
typedef EFI_STATUS (*EFI_START_IMAGE)(EFI_HANDLE, UINTN *, wchar_t *);                                            // +0xD0

static inline EFI_SET_WATCHDOG_TIMER BsWatchdog(void *BS) { return *(EFI_SET_WATCHDOG_TIMER *)((char *)BS + 0x100); }
static inline EFI_OPEN_PROTOCOL BsOpenProtocol(void *BS) { return *(EFI_OPEN_PROTOCOL *)((char *)BS + 0x118); }
static inline EFI_ALLOCATE_POOL BsAllocatePool(void *BS) { return *(EFI_ALLOCATE_POOL *)((char *)BS + 0x40); }
static inline EFI_LOAD_IMAGE BsLoadImage(void *BS) { return *(EFI_LOAD_IMAGE *)((char *)BS + 0xC8); }
static inline EFI_START_IMAGE BsStartImage(void *BS) { return *(EFI_START_IMAGE *)((char *)BS + 0xD0); }

/* ---------- File protocols (trimmed) ---------- */
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    void *_pad0;
    EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, void ** /*EFI_FILE_PROTOCOL**/);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL
{
    uint64_t _pad0[1];
    EFI_STATUS (*Open)(struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **,
                       const wchar_t *, uint64_t, uint64_t);
    EFI_STATUS (*Close)(struct EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Delete)(struct EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Read)(struct EFI_FILE_PROTOCOL *, UINTN * /*IN OUT*/, void * /*buffer*/);
    uint64_t _pad1[3];
    EFI_STATUS (*GetInfo)(struct EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN * /*IN OUT*/, void * /*buffer*/);
    // ... (we don't need more)
} EFI_FILE_PROTOCOL;

/* ---------- GUIDs ---------- */
static const EFI_GUID LOADED_IMAGE_GUID = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID SIMPLE_FS_GUID = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID EFI_FILE_INFO_GUID = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

/* ---------- Loaded Image (trimmed view) ---------- */
typedef struct
{
    uint32_t _padA[2]; // ignore signature/revision (we won't read)
    uint64_t _padB[2];
    EFI_HANDLE DeviceHandle; // at offset +0x18 on 64-bit
} EFI_LOADED_IMAGE_PROTOCOL;

static void println(EFI_SYSTEM_TABLE *st, const wchar_t *s)
{
    if (st && st->ConOut && st->ConOut->OutputString)
    {
        st->ConOut->OutputString(st->ConOut, s);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
    }
}

EFI_STATUS EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    /* Make text visible & disable watchdog if possible */
    if (st && st->ConOut)
    {
        st->ConOut->Reset(st->ConOut, 1);
        st->ConOut->ClearScreen(st->ConOut);
    }
    println(st, L"[CHAIN] BOOTAA64 -> \\OS\\aa64\\STAGE2.EFI");

    void *BS = st ? st->BootServices : 0;
    if (!BS)
    {
        println(st, L"no BootServices");
        for (;;)
        {
        }
    }
    if (BsWatchdog(BS))
        BsWatchdog(BS)(0, 0, 0, 0);

    /* 1) OpenProtocol(Image, LOADED_IMAGE) */
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_OPEN_PROTOCOL OpenProtocol = BsOpenProtocol(BS);
    if (!OpenProtocol)
    {
        println(st, L"OpenProtocol NULL");
        for (;;)
        {
        }
    }
    EFI_STATUS s = OpenProtocol(image, &LOADED_IMAGE_GUID, (void **)&li, image, 0, 0x2 /*GET_PROTOCOL*/);
    if (s)
    {
        println(st, L"OpenProtocol(LoadedImage) failed");
        for (;;)
        {
        }
    }

    /* 2) OpenProtocol(DeviceHandle, SIMPLE_FS) */
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = 0;
    s = OpenProtocol(li->DeviceHandle, &SIMPLE_FS_GUID, (void **)&sfs, image, 0, 0x2);
    if (s || !sfs || !sfs->OpenVolume)
    {
        println(st, L"OpenProtocol(SimpleFS) failed");
        for (;;)
        {
        }
    }

    /* 3) OpenVolume -> root */
    EFI_FILE_PROTOCOL *root = 0;
    s = sfs->OpenVolume(sfs, (void **)&root);
    if (s || !root)
    {
        println(st, L"OpenVolume failed");
        for (;;)
        {
        }
    }

    /* 4) Root->Open file \\OS\\aa64\\STAGE2.EFI */
    static const wchar_t path[] = L"\\OS\\aa64\\STAGE2.EFI";
    EFI_FILE_PROTOCOL *file = 0;
    s = root->Open(root, &file, path, /*READ*/ 1, 0);
    if (s || !file)
    {
        println(st, L"Open(file) failed");
        for (;;)
        {
        }
    }

    /* 5) Query size: GetInfo(NULL) first to get required size */
    UINTN info_sz = 0;
    s = file->GetInfo(file, &EFI_FILE_INFO_GUID, &info_sz, 0);
    if (s == 0)
    {
        println(st, L"GetInfo expected BUFFER_TOO_SMALL");
    }
    void *info_buf = 0;
    if (BsAllocatePool(BS)(4 /*EfiLoaderData*/, info_sz, &info_buf) || !info_buf)
    {
        println(st, L"Allocate FileInfo fail");
        for (;;)
        {
        }
    }
    s = file->GetInfo(file, &EFI_FILE_INFO_GUID, &info_sz, info_buf);
    if (s)
    {
        println(st, L"GetInfo real failed");
        for (;;)
        {
        }
    }
    /* EFI_FILE_INFO: UINT64 FileSize is at offset +8 on 64-bit builds */
    uint64_t file_size = *(uint64_t *)((char *)info_buf + 8);

    /* 6) Allocate buffer + Read */
    void *file_buf = 0;
    if (BsAllocatePool(BS)(4 /*EfiLoaderData*/, (UINTN)file_size, &file_buf) || !file_buf)
    {
        println(st, L"Allocate file_buf fail");
        for (;;)
        {
        }
    }
    UINTN read_sz = (UINTN)file_size;
    s = file->Read(file, &read_sz, file_buf);
    if (s || read_sz != (UINTN)file_size)
    {
        println(st, L"Read failed/short");
        for (;;)
        {
        }
    }

    /* Close file */
    file->Close(file);

    /* 7) LoadImage + StartImage */
    EFI_HANDLE image2 = 0;
    EFI_LOAD_IMAGE LoadImage = BsLoadImage(BS);
    EFI_START_IMAGE StartImage = BsStartImage(BS);
    if (!LoadImage || !StartImage)
    {
        println(st, L"Load/StartImage NULL");
        for (;;)
        {
        }
    }

    s = LoadImage(0 /*BootPolicy FALSE*/, image, 0 /*DevicePath*/, file_buf, (UINTN)file_size, &image2);
    if (s || !image2)
    {
        println(st, L"LoadImage failed");
        for (;;)
        {
        }
    }

    println(st, L"[CHAIN] Starting STAGE2…");
    s = StartImage(image2, 0, 0);
    /* If stage-2 ever returns, just hang here so the platform doesn’t reboot */
    println(st, L"[CHAIN] Returned from STAGE2");
    for (;;)
    {
    }
}
