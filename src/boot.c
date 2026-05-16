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
typedef EFI_STATUS (*EFI_GET_VARIABLE)(const wchar_t *, const EFI_GUID *, uint32_t *, UINTN *, void *);           // RT +0x48
typedef EFI_STATUS (*EFI_SET_VARIABLE)(const wchar_t *, const EFI_GUID *, uint32_t, UINTN, void *);               // RT +0x58

static inline EFI_SET_WATCHDOG_TIMER BsWatchdog(void *BS) { return *(EFI_SET_WATCHDOG_TIMER *)((char *)BS + 0x100); }
static inline EFI_OPEN_PROTOCOL BsOpenProtocol(void *BS) { return *(EFI_OPEN_PROTOCOL *)((char *)BS + 0x118); }
static inline EFI_ALLOCATE_POOL BsAllocatePool(void *BS) { return *(EFI_ALLOCATE_POOL *)((char *)BS + 0x40); }
static inline EFI_LOAD_IMAGE BsLoadImage(void *BS) { return *(EFI_LOAD_IMAGE *)((char *)BS + 0xC8); }
static inline EFI_START_IMAGE BsStartImage(void *BS) { return *(EFI_START_IMAGE *)((char *)BS + 0xD0); }
static inline EFI_GET_VARIABLE RtGetVariable(void *RT) { return *(EFI_GET_VARIABLE *)((char *)RT + 0x48); }
static inline EFI_SET_VARIABLE RtSetVariable(void *RT) { return *(EFI_SET_VARIABLE *)((char *)RT + 0x58); }

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
static const EFI_GUID EFI_GLOBAL_VARIABLE_GUID = {0x8be4df61, 0x93ca, 0x11d2, {0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c}};
static const EFI_GUID DEVICE_PATH_GUID = {0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

#define EFI_VARIABLE_NON_VOLATILE 0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS 0x00000004U
#define LOAD_OPTION_ACTIVE 0x00000001U

#if defined(__x86_64__) || defined(_M_X64)
#define DIHOS_BOOT_BANNER L"[CHAIN] BOOTX64 -> \\OS\\x64\\STAGE2.EFI"
#define DIHOS_STAGE2_PATH L"\\OS\\x64\\STAGE2.EFI"
#else
#define DIHOS_BOOT_BANNER L"[CHAIN] BOOTAA64 -> \\OS\\aa64\\STAGE2.EFI"
#define DIHOS_STAGE2_PATH L"\\OS\\aa64\\STAGE2.EFI"
#endif

/* ---------- Loaded Image (trimmed view) ---------- */
typedef struct
{
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct
{
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

static uint16_t dp_len(const EFI_DEVICE_PATH_PROTOCOL *dp)
{
    return (uint16_t)(dp->Length[0] | ((uint16_t)dp->Length[1] << 8));
}

static int dp_is_end(const EFI_DEVICE_PATH_PROTOCOL *dp)
{
    return dp->Type == 0x7f && dp->SubType == 0xff;
}

static UINTN device_path_size_no_end(const EFI_DEVICE_PATH_PROTOCOL *dp)
{
    UINTN total = 0;
    while (!dp_is_end(dp))
    {
        UINTN len = dp_len(dp);
        total += len;
        dp = (const EFI_DEVICE_PATH_PROTOCOL *)((const char *)dp + len);
    }
    return total;
}

static void println(EFI_SYSTEM_TABLE *st, const wchar_t *s)
{
    if (st && st->ConOut && st->ConOut->OutputString)
    {
        st->ConOut->OutputString(st->ConOut, s);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
    }
}

static UINTN wlen(const wchar_t *s)
{
    UINTN n = 0;
    while (s[n])
        n++;
    return n;
}

static void wcopy(wchar_t *dst, const wchar_t *src)
{
    while ((*dst++ = *src++))
    {
    }
}

static void boot_var_name(wchar_t out[9], uint16_t n)
{
    static const wchar_t hex[] = L"0123456789ABCDEF";
    out[0] = L'B';
    out[1] = L'o';
    out[2] = L'o';
    out[3] = L't';
    out[4] = hex[(n >> 12) & 0xF];
    out[5] = hex[(n >> 8) & 0xF];
    out[6] = hex[(n >> 4) & 0xF];
    out[7] = hex[n & 0xF];
    out[8] = 0;
}

static int boot_entry_is_dihos(void *buf, UINTN size)
{
    if (size < 8)
        return 0;
    wchar_t *desc = (wchar_t *)((char *)buf + 6);
    return desc[0] == L'D' &&
           desc[1] == L'i' &&
           desc[2] == L'h' &&
           desc[3] == L'O' &&
           desc[4] == L'S' &&
           desc[5] == 0;
}

static wchar_t ascii_wupper(wchar_t c)
{
    if (c >= L'a' && c <= L'z')
        return (wchar_t)(c - (L'a' - L'A'));
    return c;
}

static int wcmp_ci(const wchar_t *a, const wchar_t *b)
{
    UINTN i = 0;
    for (;; i++)
    {
        wchar_t ca = ascii_wupper(a[i]);
        wchar_t cb = ascii_wupper(b[i]);
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
    }
}

static int boot_entry_targets_path(void *buf, UINTN size, const wchar_t *boot_path)
{
    if (size < 8)
        return 0;

    uint16_t fp_len = *(uint16_t *)((char *)buf + 4);
    UINTN off = 6;

    while (off + sizeof(wchar_t) <= size)
    {
        wchar_t ch = *(wchar_t *)((char *)buf + off);
        off += sizeof(wchar_t);
        if (ch == 0)
            break;
    }

    if (off > size || off + fp_len > size)
        return 0;

    UINTN remain = fp_len;
    uint8_t *p = (uint8_t *)buf + off;
    while (remain >= sizeof(EFI_DEVICE_PATH_PROTOCOL))
    {
        EFI_DEVICE_PATH_PROTOCOL *node = (EFI_DEVICE_PATH_PROTOCOL *)p;
        UINTN nlen = dp_len(node);
        if (nlen < sizeof(EFI_DEVICE_PATH_PROTOCOL) || nlen > remain)
            break;

        if (node->Type == 0x04 && node->SubType == 0x04 && nlen > sizeof(EFI_DEVICE_PATH_PROTOCOL))
        {
            wchar_t *node_path = (wchar_t *)(p + sizeof(EFI_DEVICE_PATH_PROTOCOL));
            if (wcmp_ci(node_path, boot_path))
                return 1;
        }

        p += nlen;
        remain -= nlen;
    }

    return 0;
}

static int dihos_entry_exists(EFI_GET_VARIABLE GetVariable)
{
    wchar_t name[9];
    uint8_t tmp[512];
    for (uint32_t i = 0; i <= 0x0FFF; i++)
    {
        UINTN sz = sizeof(tmp);
        uint32_t attrs = 0;
        boot_var_name(name, (uint16_t)i);
        EFI_STATUS s = GetVariable(name, &EFI_GLOBAL_VARIABLE_GUID, &attrs, &sz, tmp);
        if (!s && boot_entry_is_dihos(tmp, sz))
            return 1;
    }
    return 0;
}

static uint16_t find_free_bootnum(EFI_GET_VARIABLE GetVariable)
{
    wchar_t name[9];
    uint8_t tmp[8];
    for (uint32_t i = 0; i <= 0x0FFF; i++)
    {
        UINTN sz = sizeof(tmp);
        uint32_t attrs = 0;
        boot_var_name(name, (uint16_t)i);
        EFI_STATUS s = GetVariable(name, &EFI_GLOBAL_VARIABLE_GUID, &attrs, &sz, tmp);
        if (s)
            return (uint16_t)i;
    }
    return 0xFFFF;
}

static void prepend_boot_order(EFI_SYSTEM_TABLE *st, EFI_GET_VARIABLE GetVariable, EFI_SET_VARIABLE SetVariable, uint16_t bootnum)
{
    void *BS = st->BootServices;
    UINTN old_sz = 0;
    uint32_t attrs = 0;
    GetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, &attrs, &old_sz, 0);

    if (old_sz >= sizeof(uint16_t))
    {
        uint16_t *existing = 0;
        if (!BsAllocatePool(BS)(4, old_sz, (void **)&existing) && existing)
        {
            UINTN tmp_sz = old_sz;
            if (!GetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, &attrs, &tmp_sz, existing))
            {
                UINTN count = tmp_sz / sizeof(uint16_t);
                for (UINTN i = 0; i < count; i++)
                {
                    if (existing[i] == bootnum)
                        return;
                }
            }
        }
    }

    UINTN new_sz = old_sz + sizeof(uint16_t);
    uint16_t *order = 0;
    if (BsAllocatePool(BS)(4, new_sz, (void **)&order) || !order)
        return;
    order[0] = bootnum;
    if (old_sz)
        GetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, &attrs, &old_sz, &order[1]);
    SetVariable(
        L"BootOrder",
        &EFI_GLOBAL_VARIABLE_GUID,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        new_sz,
        order);
}

static int bootnum_in_list(const uint16_t *list, UINTN count, uint16_t n)
{
    for (UINTN i = 0; i < count; i++)
    {
        if (list[i] == n)
            return 1;
    }
    return 0;
}

static void remove_bootnums_from_boot_order(EFI_SYSTEM_TABLE *st, EFI_GET_VARIABLE GetVariable, EFI_SET_VARIABLE SetVariable, const uint16_t *removed, UINTN removed_count)
{
    if (!removed_count)
        return;

    void *BS = st->BootServices;
    UINTN old_sz = 0;
    uint32_t attrs = 0;
    if (GetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, &attrs, &old_sz, 0) || old_sz < sizeof(uint16_t))
        return;

    uint16_t *old_order = 0;
    if (BsAllocatePool(BS)(4, old_sz, (void **)&old_order) || !old_order)
        return;

    if (GetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, &attrs, &old_sz, old_order))
        return;

    UINTN old_count = old_sz / sizeof(uint16_t);
    UINTN new_count = 0;
    for (UINTN i = 0; i < old_count; i++)
    {
        if (!bootnum_in_list(removed, removed_count, old_order[i]))
            new_count++;
    }

    UINTN new_sz = new_count * sizeof(uint16_t);
    if (new_count == old_count)
        return;

    if (new_count == 0)
    {
        SetVariable(L"BootOrder", &EFI_GLOBAL_VARIABLE_GUID, 0, 0, 0);
        return;
    }

    uint16_t *new_order = 0;
    if (BsAllocatePool(BS)(4, new_sz, (void **)&new_order) || !new_order)
        return;

    UINTN j = 0;
    for (UINTN i = 0; i < old_count; i++)
    {
        if (!bootnum_in_list(removed, removed_count, old_order[i]))
            new_order[j++] = old_order[i];
    }

    SetVariable(
        L"BootOrder",
        &EFI_GLOBAL_VARIABLE_GUID,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        new_sz,
        new_order);
}

static void cleanup_fallback_aliases(EFI_SYSTEM_TABLE *st, EFI_GET_VARIABLE GetVariable, EFI_SET_VARIABLE SetVariable)
{
    uint16_t removed[32];
    UINTN removed_count = 0;
    wchar_t name[9];
    uint8_t tmp[2048];

#if defined(__x86_64__) || defined(_M_X64)
    static const wchar_t boot_path[] = L"\\EFI\\BOOT\\BOOTX64.EFI";
#else
    static const wchar_t boot_path[] = L"\\EFI\\BOOT\\BOOTAA64.EFI";
#endif

    for (uint32_t i = 0; i <= 0x0FFF; i++)
    {
        UINTN sz = sizeof(tmp);
        uint32_t attrs = 0;
        boot_var_name(name, (uint16_t)i);
        EFI_STATUS s = GetVariable(name, &EFI_GLOBAL_VARIABLE_GUID, &attrs, &sz, tmp);
        if (s)
            continue;

        if (!boot_entry_is_dihos(tmp, sz) && boot_entry_targets_path(tmp, sz, boot_path))
        {
            if (!SetVariable(name, &EFI_GLOBAL_VARIABLE_GUID, 0, 0, 0))
            {
                if (removed_count < (sizeof(removed) / sizeof(removed[0])))
                    removed[removed_count++] = (uint16_t)i;
            }
        }
    }

    remove_bootnums_from_boot_order(st, GetVariable, SetVariable, removed, removed_count);
    if (removed_count)
        println(st, L"[NVRAM] Removed fallback alias entries");
}

static void register_dihos_boot_entry(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, EFI_LOADED_IMAGE_PROTOCOL *li)
{
    if (!st || !st->RuntimeServices || !st->BootServices || !li)
        return;
    void *RT = st->RuntimeServices;
    void *BS = st->BootServices;
    EFI_GET_VARIABLE GetVariable = RtGetVariable(RT);
    EFI_SET_VARIABLE SetVariable = RtSetVariable(RT);
    if (!GetVariable || !SetVariable)
    {
        println(st, L"[NVRAM] RuntimeServices variable funcs missing");
        return;
    }
    println(st, L"[NVRAM] Checking DihOS boot entry...");
    if (dihos_entry_exists(GetVariable))
    {
        println(st, L"[NVRAM] DihOS boot entry already exists");
        cleanup_fallback_aliases(st, GetVariable, SetVariable);
        return;
    }

    EFI_OPEN_PROTOCOL OpenProtocol = BsOpenProtocol(BS);
    EFI_DEVICE_PATH_PROTOCOL *devpath = 0;
    EFI_STATUS s = OpenProtocol(li->DeviceHandle, &DEVICE_PATH_GUID, (void **)&devpath, image, 0, 0x2);
    if (s || !devpath)
    {
        println(st, L"[NVRAM] DevicePath open failed");
        return;
    }

#if defined(__x86_64__) || defined(_M_X64)
    static const wchar_t boot_path[] = L"\\EFI\\BOOT\\BOOTX64.EFI";
#else
    static const wchar_t boot_path[] = L"\\EFI\\BOOT\\BOOTAA64.EFI";
#endif

    UINTN base_dp_sz = device_path_size_no_end(devpath);
    UINTN path_chars = wlen(boot_path) + 1;
    UINTN file_node_sz = sizeof(EFI_DEVICE_PATH_PROTOCOL) + path_chars * sizeof(wchar_t);
    UINTN end_node_sz = sizeof(EFI_DEVICE_PATH_PROTOCOL);
    UINTN full_dp_sz = base_dp_sz + file_node_sz + end_node_sz;
    UINTN desc_sz = (wlen(L"DihOS") + 1) * sizeof(wchar_t);
    UINTN load_option_sz = sizeof(uint32_t) + sizeof(uint16_t) + desc_sz + full_dp_sz;

    uint8_t *opt = 0;
    if (BsAllocatePool(BS)(4, load_option_sz, (void **)&opt) || !opt)
    {
        println(st, L"[NVRAM] alloc load option failed");
        return;
    }

    uint8_t *p = opt;
    *(uint32_t *)p = LOAD_OPTION_ACTIVE;
    p += sizeof(uint32_t);
    *(uint16_t *)p = (uint16_t)full_dp_sz;
    p += sizeof(uint16_t);
    wcopy((wchar_t *)p, L"DihOS");
    p += desc_sz;

    for (UINTN i = 0; i < base_dp_sz; i++)
        p[i] = ((uint8_t *)devpath)[i];
    p += base_dp_sz;

    EFI_DEVICE_PATH_PROTOCOL *fp = (EFI_DEVICE_PATH_PROTOCOL *)p;
    fp->Type = 0x04;
    fp->SubType = 0x04;
    fp->Length[0] = (uint8_t)(file_node_sz & 0xFF);
    fp->Length[1] = (uint8_t)((file_node_sz >> 8) & 0xFF);
    wcopy((wchar_t *)(p + sizeof(EFI_DEVICE_PATH_PROTOCOL)), boot_path);
    p += file_node_sz;

    EFI_DEVICE_PATH_PROTOCOL *end = (EFI_DEVICE_PATH_PROTOCOL *)p;
    end->Type = 0x7f;
    end->SubType = 0xff;
    end->Length[0] = 4;
    end->Length[1] = 0;

    uint16_t bootnum = find_free_bootnum(GetVariable);
    if (bootnum == 0xFFFF)
    {
        println(st, L"[NVRAM] no free Boot####");
        return;
    }

    wchar_t varname[9];
    boot_var_name(varname, bootnum);
    s = SetVariable(
        varname,
        &EFI_GLOBAL_VARIABLE_GUID,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        load_option_sz,
        opt);
    if (s)
    {
        println(st, L"[NVRAM] Boot#### create failed");
        return;
    }

    prepend_boot_order(st, GetVariable, SetVariable, bootnum);
    cleanup_fallback_aliases(st, GetVariable, SetVariable);
    println(st, L"[NVRAM] Registered DihOS boot entry");
}

EFI_STATUS EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    /* Make text visible & disable watchdog if possible */
    if (st && st->ConOut)
    {
        st->ConOut->Reset(st->ConOut, 1);
        st->ConOut->ClearScreen(st->ConOut);
    }
    println(st, DIHOS_BOOT_BANNER);

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
    register_dihos_boot_entry(st, image, li);

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

    /* 4) Root->Open stage2 for this firmware architecture. */
    static const wchar_t path[] = DIHOS_STAGE2_PATH;
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
