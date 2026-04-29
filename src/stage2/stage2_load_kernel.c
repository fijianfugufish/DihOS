// src/stage2_load_kernel.c — PIE loader for AArch64 (ELF64 ET_DYN)
#include <stdint.h>
#include <wchar.h>
#include "bootinfo.h"

typedef wchar_t CHAR16;

/* --- minimal UEFI bits --- */
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
    EFI_HANDLE StandardErrorHandle;
    SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    void *BootServices;
    uint64_t NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

extern const EFI_GUID FILE_INFO_ID; // defined in stage2.c (not static)
extern EFI_STATUS reopen_same_volume(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, void **out_root);

/* print helpers */
static inline EFI_TEXT_STRING ConOutPrint(EFI_SYSTEM_TABLE *st) { return st->ConOut->OutputString; }
static void print(EFI_SYSTEM_TABLE *st, const wchar_t *s) { ConOutPrint(st)(st->ConOut, s); }
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

/* Boot Services slots */
typedef EFI_STATUS (*EFI_ALLOCATE_PAGES)(uint32_t, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (*EFI_ALLOCATE_POOL)(uint32_t, UINTN, void **);
typedef EFI_STATUS (*EFI_FREE_POOL)(void *);
typedef EFI_STATUS (*EFI_GET_MEMORY_MAP)(UINTN *, void *, UINTN *, UINTN *, uint32_t *);
typedef EFI_STATUS (*EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

static inline EFI_ALLOCATE_PAGES BsAllocPages(void *BS) { return *(EFI_ALLOCATE_PAGES *)((char *)BS + 0x28); }
static inline EFI_ALLOCATE_POOL BsAllocPool(void *BS) { return *(EFI_ALLOCATE_POOL *)((char *)BS + 0x40); }
static inline EFI_FREE_POOL BsFreePool(void *BS) { return *(EFI_FREE_POOL *)((char *)BS + 0x48); }
static inline EFI_GET_MEMORY_MAP BsGetMMap(void *BS) { return *(EFI_GET_MEMORY_MAP *)((char *)BS + 0x38); }
static inline EFI_EXIT_BOOT_SERVICES BsExitBS(void *BS) { return *(EFI_EXIT_BOOT_SERVICES *)((char *)BS + 0xE8); }

typedef EFI_STATUS (*EFI_SET_WATCHDOG_TIMER)(UINTN, UINTN, UINTN, const wchar_t *);
static inline EFI_SET_WATCHDOG_TIMER BsSetWatchdog(void *BS) { return *(EFI_SET_WATCHDOG_TIMER *)((char *)BS + 0x100); }

enum
{
    AllocateAnyPages = 0
};
enum
{
    EfiLoaderCode = 1,
    EfiLoaderData = 2
};

/* --- File helpers provided by stage2.c --- */
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
extern EFI_STATUS fs_open_root(EFI_SYSTEM_TABLE *, EFI_HANDLE, EFI_FILE_PROTOCOL **);

/* FILE_INFO GUID lives in stage2.c (must be non-static there) */
extern const EFI_GUID FILE_INFO_ID;

/* ADD THIS: reopen_same_volume is defined in stage2.c */
extern EFI_STATUS reopen_same_volume(EFI_SYSTEM_TABLE *st,
                                     EFI_HANDLE image,
                                     void **out_root);

/* ---------- forward declaration to avoid implicit-decl error ---------- */
static EFI_STATUS dbg_open_kernel(EFI_SYSTEM_TABLE *st,
                                  EFI_FILE_PROTOCOL *Root,
                                  EFI_FILE_PROTOCOL **out_file);

/* --- robust path-open helper (kept for reference; not used by dbg_…) --- */
static EFI_STATUS open_kernel_with_fallback(EFI_FILE_PROTOCOL *Root,
                                            const wchar_t *path,
                                            EFI_FILE_PROTOCOL **out)
{
    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                                 const wchar_t *, uint64_t, uint64_t);
    T_OPEN FileOpen = *(void **)((char *)Root + 0x08);

    EFI_STATUS s = FileOpen(Root, out, path, /*READ*/ 1, 0);
    if (!s && *out)
        return s;

    if (path && path[0] != L'\\')
    {
        wchar_t buf[260];
        buf[0] = L'\\';
        size_t i = 1;
        for (size_t j = 0; path[j] && i < 259; ++j, ++i)
            buf[i] = path[j];
        buf[i] = 0;
        s = FileOpen(Root, out, buf, /*READ*/ 1, 0);
        if (!s && *out)
            return s;
    }
    if (path && path[0] == L'\\')
    {
        s = FileOpen(Root, out, path + 1, /*READ*/ 1, 0);
        if (!s && *out)
            return s;
    }
    return s ? s : (EFI_STATUS)14; // NOT_FOUND-ish
}

/* Read whole file */
static EFI_STATUS file_read_all_local(EFI_SYSTEM_TABLE *st, EFI_FILE_PROTOCOL *root,
                                      const wchar_t *path, void **out_buf, UINTN *out_size)
{
    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, const wchar_t *, uint64_t, uint64_t);
    typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);
    typedef EFI_STATUS (*T_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    typedef EFI_STATUS (*T_GETINFO)(EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN *, void *);

    T_OPEN FileOpen = *(void **)((char *)root + 0x08);
    T_CLOSE FileClose = *(void **)((char *)root + 0x10);
    T_READ FileRead = *(void **)((char *)root + 0x20);
    T_GETINFO GetInfo = *(void **)((char *)root + 0x40);
    EFI_ALLOCATE_POOL AllocPool = BsAllocPool(st->BootServices);

    EFI_FILE_PROTOCOL *fp = 0;
    EFI_STATUS s = dbg_open_kernel(st, root, &fp);
    if (s)
    {
        println(st, L"[S2] read kernel fail");
        return s;
    }

    UINTN info_sz = 0;
    (void)GetInfo(fp, &FILE_INFO_ID, &info_sz, 0);
    void *info = 0;
    AllocPool(EfiLoaderData, info_sz, &info);
    s = GetInfo(fp, &FILE_INFO_ID, &info_sz, info);
    if (s)
    {
        print(st, L"[S2] GetInfo status=");
        hex64(st, s);
        FileClose(fp);
        return s;
    }

    uint64_t fsz = *(uint64_t *)((char *)info + 8); // FILE_INFO.FileSize
    void *buf = 0;
    AllocPool(EfiLoaderData, (UINTN)fsz, &buf);
    UINTN read = (UINTN)fsz;
    s = FileRead(fp, &read, buf);
    FileClose(fp);
    if (s)
    {
        print(st, L"[S2] FileRead status=");
        hex64(st, s);
        return s;
    }
    *out_buf = buf;
    *out_size = read;
    return 0;
}

/* Open \OS\aa64\KERNEL.ELF by walking the directory tree */
typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                             const wchar_t *, uint64_t, uint64_t);
typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);

// Try to open  \OS\aa64\KERNEL.ELF  from the volume root 'Root'.
// We try a few direct variants, then walk OS -> aa64 -> KERNEL.ELF with no leading '\'.
// Try to open  \OS\aa64\KERNEL.ELF  from Root.
// We try a few direct variants, then walk OS -> aa64 -> KERNEL.ELF.
static EFI_STATUS dbg_open_kernel(EFI_SYSTEM_TABLE *st,
                                  EFI_FILE_PROTOCOL *Root,
                                  EFI_FILE_PROTOCOL **out_file)
{
    *out_file = 0;

    typedef EFI_STATUS (*T_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                                 const wchar_t *, uint64_t, uint64_t);
    typedef EFI_STATUS (*T_CLOSE)(EFI_FILE_PROTOCOL *);

    T_OPEN FileOpen = *(void **)((char *)Root + 0x08);

    // 0) quick direct tries
    static const wchar_t *directs[] = {
        L"\\OS\\aa64\\KERNEL.ELF",
        L"OS\\aa64\\KERNEL.ELF",
        L"\\os\\aa64\\kernel.elf",
        L"os\\aa64\\kernel.elf"};
    for (unsigned i = 0; i < sizeof(directs) / sizeof(directs[0]); ++i)
    {
        EFI_FILE_PROTOCOL *fp = 0;
        EFI_STATUS s = FileOpen(Root, &fp, directs[i], /*READ*/ 1, 0);
        if (!s && fp)
        {
            *out_file = fp;
            return 0;
        }
    }

    // 1) walk OS -> aa64 (no leading '\')
    EFI_FILE_PROTOCOL *dir = 0;
    EFI_STATUS s = FileOpen(Root, &dir, L"OS", /*READ*/ 1, 0);
    if (s || !dir)
    {
        print(st, L"[S2] FileOpen(OS) status=");
        hex64(st, s);
        return s ? s : (EFI_STATUS)~0ULL;
    }

    EFI_FILE_PROTOCOL *dir2 = 0;
    s = FileOpen(dir, &dir2, L"aa64", /*READ*/ 1, 0);
    if (s || !dir2)
    {
        print(st, L"[S2] FileOpen(aa64) status=");
        hex64(st, s);
        T_CLOSE Close = *(void **)((char *)dir + 0x10);
        if (Close)
            Close(dir);
        return s ? s : (EFI_STATUS)~0ULL;
    }
    {
        T_CLOSE Close = *(void **)((char *)dir + 0x10);
        if (Close)
            Close(dir);
    }

    EFI_FILE_PROTOCOL *fp = 0;
    s = FileOpen(dir2, &fp, L"KERNEL.ELF", /*READ*/ 1, 0);
    if (s || !fp)
    {
        print(st, L"[S2] FileOpen(KERNEL.ELF) status=");
        hex64(st, s);
        T_CLOSE Close = *(void **)((char *)dir2 + 0x10);
        if (Close)
            Close(dir2);
        return s ? s : (EFI_STATUS)~0ULL;
    }
    {
        T_CLOSE Close = *(void **)((char *)dir2 + 0x10);
        if (Close)
            Close(dir2);
    }

    *out_file = fp;
    return 0;
}

/* --- ELF types --- */
typedef struct
{
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct
{
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;
typedef struct
{
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;
typedef struct
{
    uint64_t r_offset, r_info, r_addend;
} Elf64_Rela;

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define R_AARCH64_RELATIVE 1027
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xffffffff))

/* icache sync */
static void a64_sync_icache(void *start, void *end)
{
    uintptr_t p = ((uintptr_t)start) & ~63ull, e = (((uintptr_t)end) + 63ull) & ~63ull;
    for (uintptr_t q = p; q < e; q += 64)
        __asm__ volatile("dc cvau,%0" ::"r"(q) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (uintptr_t q = p; q < e; q += 64)
        __asm__ volatile("ic ivau,%0" ::"r"(q) : "memory");
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

/* ---------- PIE loader ---------- */
EFI_STATUS load_kernel_elf(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, const wchar_t *path,
                           EFI_PHYSICAL_ADDRESS *entry_out, boot_info *bi_out)
{
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS s = fs_open_root(st, image, &root);
    if (s || !root)
    {
        println(st, L"[S2] root open fail");
        return s ? s : (EFI_STATUS)~0ULL;
    }

    void *file_buf = 0;
    UINTN file_sz = 0;
    s = file_read_all_local(st, root, path, &file_buf, &file_sz);
    if (s)
    {
        println(st, L"[S2] read kernel fail");
        return s;
    }

    Elf64_Ehdr *eh = (Elf64_Ehdr *)file_buf;
    if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F'))
    {
        println(st, L"[S2] not ELF");
        return 1;
    }
    if (eh->e_machine != 0xB7)
    {
        println(st, L"[S2] wrong machine");
        return 1;
    }

    uint64_t lo = ~0ull, hi = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)file_buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_vaddr < lo)
            lo = ph->p_vaddr;
        uint64_t end = ph->p_vaddr + ph->p_memsz;
        if (end > hi)
            hi = end;
    }
    if (lo >= hi)
    {
        println(st, L"[S2] no loadable segs");
        return 1;
    }

    EFI_ALLOCATE_PAGES AllocPages = BsAllocPages(st->BootServices);
    UINTN pages = (UINTN)((hi - lo + 0xFFF) >> 12);
    EFI_PHYSICAL_ADDRESS base = 0;
    s = AllocPages(AllocateAnyPages, EfiLoaderCode, pages, &base);
    if (s)
    {
        println(st, L"[S2] AllocPages(any) failed");
        return s;
    }

    uint8_t *dst_base = (uint8_t *)(uintptr_t)base;
    uint64_t min_dst = (uint64_t)(uintptr_t)dst_base, max_dst = min_dst;

    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)file_buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        uint8_t *seg_dst = dst_base + (ph->p_vaddr - lo);
        uint8_t *seg_src = (uint8_t *)file_buf + ph->p_offset;
        for (uint64_t k = 0; k < ph->p_filesz; k++)
            seg_dst[k] = seg_src[k];
        for (uint64_t k = ph->p_filesz; k < ph->p_memsz; k++)
            seg_dst[k] = 0;
        uint64_t seg_end = (uint64_t)(uintptr_t)(seg_dst + ph->p_memsz);
        if ((uint64_t)(uintptr_t)seg_dst < min_dst)
            min_dst = (uint64_t)(uintptr_t)seg_dst;
        if (seg_end > max_dst)
            max_dst = seg_end;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)file_buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_DYNAMIC)
            continue;
        Elf64_Dyn *dyn = (Elf64_Dyn *)((uint8_t *)file_buf + ph->p_offset);
        uint64_t rela_off = 0, rela_sz = 0, rela_ent = 0;
        for (; dyn->d_tag != DT_NULL; ++dyn)
        {
            if (dyn->d_tag == DT_RELA)
                rela_off = dyn->d_val;
            else if (dyn->d_tag == DT_RELASZ)
                rela_sz = dyn->d_val;
            else if (dyn->d_tag == DT_RELAENT)
                rela_ent = dyn->d_val;
        }
        if (rela_off && rela_sz && rela_ent)
        {
            uint8_t *dyn_base = dst_base + (ph->p_vaddr - lo);
            Elf64_Rela *rela = (Elf64_Rela *)(dyn_base + (rela_off - ph->p_vaddr));
            uint64_t count = rela_sz / rela_ent;
            for (uint64_t r = 0; r < count; r++)
            {
                if (ELF64_R_TYPE(rela[r].r_info) == R_AARCH64_RELATIVE)
                {
                    uint64_t *where = (uint64_t *)(dst_base + (rela[r].r_offset - lo));
                    *where = base + rela[r].r_addend;
                }
            }
        }
    }

    a64_sync_icache((void *)(uintptr_t)min_dst, (void *)(uintptr_t)max_dst);
    *entry_out = (EFI_PHYSICAL_ADDRESS)(base + (eh->e_entry - lo));

    if (bi_out)
    {
        static const UINTN sacx_pool_page_options[] = {
            (UINTN)((32ull * 1024ull * 1024ull) >> 12),
            (UINTN)((16ull * 1024ull * 1024ull) >> 12),
            (UINTN)((8ull * 1024ull * 1024ull) >> 12),
        };
        bi_out->kernel_base_phys = (uint64_t)base;
        bi_out->kernel_size_bytes = (uint64_t)(max_dst - min_dst);
        bi_out->sacx_exec_pool_base_phys = 0;
        bi_out->sacx_exec_pool_size_bytes = 0;

        for (UINTN i = 0; i < (UINTN)(sizeof(sacx_pool_page_options) / sizeof(sacx_pool_page_options[0])); ++i)
        {
            EFI_PHYSICAL_ADDRESS sacx_base = 0;
            UINTN sacx_pages = sacx_pool_page_options[i];
            EFI_STATUS ps = AllocPages(AllocateAnyPages, EfiLoaderCode, sacx_pages, &sacx_base);
            if (!ps && sacx_base)
            {
                bi_out->sacx_exec_pool_base_phys = (uint64_t)sacx_base;
                bi_out->sacx_exec_pool_size_bytes = (uint64_t)sacx_pages << 12;
                print(st, L"[S2] SACX exec pool base = ");
                hex64(st, bi_out->sacx_exec_pool_base_phys);
                print(st, L"[S2] SACX exec pool size = ");
                hex64(st, bi_out->sacx_exec_pool_size_bytes);
                break;
            }
        }

        if (!bi_out->sacx_exec_pool_base_phys)
            println(st, L"[S2] WARNING: SACX exec pool allocation failed");
    }

    EFI_FREE_POOL FreePool = BsFreePool(st->BootServices);
    if (file_buf)
        FreePool(file_buf);
    return 0;
}

#ifndef EFI_ERROR
#define EFI_ERROR(x) ((x) != 0)
#endif

typedef void (*kernel_entry_t)(const boot_info *);

EFI_STATUS exit_boot_and_jump(EFI_SYSTEM_TABLE *st,
                              EFI_HANDLE image,
                              const boot_info *bi,
                              EFI_PHYSICAL_ADDRESS entry)
{
    EFI_GET_MEMORY_MAP GetMMap = BsGetMMap(st->BootServices);
    EFI_EXIT_BOOT_SERVICES ExitBS = BsExitBS(st->BootServices);
    EFI_ALLOCATE_POOL Alloc = BsAllocPool(st->BootServices);
    EFI_FREE_POOL Free = BsFreePool(st->BootServices);

    EFI_SET_WATCHDOG_TIMER SetWatchdog = BsSetWatchdog(st->BootServices);
    SetWatchdog(0, 0, 0, NULL);

    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)bi->fb.fb_base;
    uint32_t pitch_px = bi->fb.pitch ? (bi->fb.pitch / 4) : 0;
    if (fb)
    {
        fb[0] = 0x00FF0000;
        uintptr_t a = ((uintptr_t)&fb[0]) & ~63ull;
        __asm__ volatile("dc cvac,%0" ::"r"(a) : "memory");
        __asm__ volatile("dsb ish; isb" ::: "memory");
    }

    UINTN map_sz = 0, key = 0, dsz = 0;
    uint32_t dver = 0;
    EFI_STATUS s = GetMMap(&map_sz, 0, &key, &dsz, &dver);
    if (s == 5)
        map_sz += (dsz ? dsz : 56) * 8;
    else if (EFI_ERROR(s))
        map_sz = 8192;

    void *mmap = 0;
    if (Alloc(EfiLoaderData, map_sz, &mmap))
        for (;;)
        {
        }

    s = GetMMap(&map_sz, mmap, &key, &dsz, &dver);
    if (EFI_ERROR(s))
        for (;;)
        {
        }

    s = ExitBS(image, key);
    if (EFI_ERROR(s))
    {
        UINTN ms = map_sz;
        if (!GetMMap(&ms, mmap, &key, &dsz, &dver))
            s = ExitBS(image, key);
        if (EFI_ERROR(s))
            for (;;)
            {
            }
    }

    if (fb && pitch_px)
    {
        fb[pitch_px + 1] = 0x000000FF;
        uintptr_t b = ((uintptr_t)&fb[pitch_px + 1]) & ~63ull;
        __asm__ volatile("dc cvac,%0" ::"r"(b) : "memory");
        __asm__ volatile("dsb ish; isb" ::: "memory");
    }

    boot_info bi_copy = *bi;
    bi_copy.mmap = (uint64_t)(uintptr_t)mmap;
    bi_copy.mmap_size = map_sz;
    bi_copy.mmap_desc_size = dsz;
    bi_copy.mmap_desc_version = dver;

    ((kernel_entry_t)(uintptr_t)entry)(&bi_copy);
    for (;;)
    {
    }
}
