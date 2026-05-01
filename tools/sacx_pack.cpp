#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../kernel/include/apps/sacx_format.h"

#pragma pack(push, 1)
struct Elf64_Ehdr
{
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct Elf64_Dyn
{
    int64_t d_tag;
    uint64_t d_val;
};

struct Elf64_Rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};
#pragma pack(pop)

static constexpr uint16_t EM_AARCH64 = 183u;
static constexpr uint16_t EM_X86_64 = 62u;
static constexpr uint32_t PT_LOAD = 1u;
static constexpr uint32_t PT_DYNAMIC = 2u;
static constexpr int64_t DT_NULL = 0;
static constexpr int64_t DT_RELA = 7;
static constexpr int64_t DT_RELASZ = 8;
static constexpr int64_t DT_RELAENT = 9;
static constexpr uint32_t R_AARCH64_RELATIVE = 1027u;
static constexpr uint32_t R_X86_64_RELATIVE = 8u;
static constexpr uint32_t MAX_SACX_SLICES = 8u;

static uint32_t crc32_zero_range(const uint8_t *data, size_t size, size_t zero_off, size_t zero_len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < size; ++i)
    {
        uint8_t byte = data[i];
        if (i >= zero_off && i < zero_off + zero_len)
            byte = 0u;

        crc ^= static_cast<uint32_t>(byte);
        for (uint32_t k = 0; k < 8u; ++k)
        {
            if (crc & 1u)
                crc = (crc >> 1u) ^ 0xEDB88320u;
            else
                crc >>= 1u;
        }
    }

    return ~crc;
}

static std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("unable to open input file: " + path);

    in.seekg(0, std::ios::end);
    std::streamoff end = in.tellg();
    if (end <= 0)
        throw std::runtime_error("input file is empty: " + path);
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in)
        throw std::runtime_error("failed to read input file: " + path);
    return bytes;
}

static void write_file(const std::string &path, const std::vector<uint8_t> &data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("unable to open output file: " + path);
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out)
        throw std::runtime_error("failed to write output file: " + path);
}

template <typename T>
static const T &read_struct(const std::vector<uint8_t> &bytes, size_t off)
{
    if (off + sizeof(T) > bytes.size())
        throw std::runtime_error("unexpected EOF while reading structure");
    return *reinterpret_cast<const T *>(bytes.data() + off);
}

static uint64_t va_to_file_off(const std::vector<Elf64_Phdr> &phdrs, uint64_t va)
{
    for (const Elf64_Phdr &ph : phdrs)
    {
        if (ph.p_type != PT_LOAD || ph.p_filesz == 0)
            continue;

        if (va >= ph.p_vaddr && va < ph.p_vaddr + ph.p_filesz)
            return ph.p_offset + (va - ph.p_vaddr);
    }

    throw std::runtime_error("unable to map virtual address to file offset");
}

static std::string trim(const std::string &s)
{
    size_t lo = 0;
    size_t hi = s.size();

    while (lo < hi && (s[lo] == ' ' || s[lo] == '\t' || s[lo] == '\r' || s[lo] == '\n'))
        ++lo;
    while (hi > lo && (s[hi - 1] == ' ' || s[hi - 1] == '\t' || s[hi - 1] == '\r' || s[hi - 1] == '\n'))
        --hi;

    return s.substr(lo, hi - lo);
}

static std::vector<std::string> default_imports()
{
    return {
        "app_set_update",
        "app_exit",
        "app_yield",
        "app_sleep_ticks",
        "app_set_console_visible",
        "time_ticks",
        "time_seconds",
        "log",
        "file_open",
        "file_read",
        "file_write",
        "file_seek",
        "file_size",
        "file_close",
        "file_unlink",
        "file_rename",
        "file_mkdir",
        "window_create",
        "window_destroy",
        "window_set_visible",
        "window_set_title",
        "gfx_fill_rgb",
        "gfx_rect_rgb",
        "gfx_flush",
        "input_key_down",
        "input_key_pressed",
        "input_key_released",
        "dir_open",
        "dir_next",
        "dir_close",
        "window_create_ex",
        "window_visible",
        "window_raise",
        "window_set_work_area_bottom_inset",
        "window_root",
        "window_point_can_receive_input",
        "gfx_obj_add_rect",
        "gfx_obj_add_circle",
        "gfx_obj_add_text",
        "gfx_obj_add_image_from_img",
        "gfx_obj_destroy",
        "gfx_obj_set_visible",
        "gfx_obj_visible",
        "gfx_obj_set_z",
        "gfx_obj_z",
        "gfx_obj_set_parent",
        "gfx_obj_clear_parent",
        "gfx_obj_set_clip_to_parent",
        "gfx_obj_set_fill_rgb",
        "gfx_obj_set_alpha",
        "gfx_obj_set_outline_rgb",
        "gfx_obj_set_outline_width",
        "gfx_obj_set_outline_alpha",
        "gfx_obj_set_rect",
        "gfx_obj_get_rect",
        "gfx_obj_set_rotation_deg",
        "gfx_obj_rotation_deg",
        "gfx_obj_set_rotation_pivot",
        "gfx_obj_clear_rotation_pivot",
        "gfx_obj_set_circle",
        "gfx_text_set",
        "gfx_text_set_align",
        "gfx_text_set_spacing",
        "gfx_text_set_scale",
        "gfx_text_set_pos",
        "gfx_image_set_size",
        "gfx_image_set_pos",
        "gfx_image_set_scale_pct",
        "gfx_image_set_sample_mode",
        "button_add_rect",
        "button_destroy",
        "button_root",
        "button_set_callback",
        "button_set_style",
        "button_set_enabled",
        "button_enabled",
        "button_hovered",
        "button_pressed",
        "textbox_add_rect",
        "textbox_destroy",
        "textbox_root",
        "textbox_set_callback",
        "textbox_set_enabled",
        "textbox_enabled",
        "textbox_set_focus",
        "textbox_clear_focus",
        "textbox_focused",
        "textbox_set_bounds",
        "textbox_set_text",
        "textbox_clear",
        "textbox_text_copy",
        "input_mouse_dx",
        "input_mouse_dy",
        "input_mouse_wheel",
        "input_mouse_buttons",
        "input_mouse_consume",
        "mouse_set_cursor",
        "mouse_current_cursor",
        "mouse_set_sensitivity_pct",
        "mouse_sensitivity_pct",
        "mouse_x",
        "mouse_y",
        "mouse_dx",
        "mouse_dy",
        "mouse_wheel",
        "mouse_buttons",
        "mouse_visible",
        "mouse_get_state",
        "text_draw",
        "text_draw_align",
        "text_draw_outline_align",
        "text_measure_line_px",
        "text_line_height",
        "text_scale_mul_px",
        "img_load",
        "img_load_bmp",
        "img_load_png",
        "img_load_jpg",
        "img_draw",
        "img_destroy",
        "img_size",
        "sched_preempt_guard_enter",
        "sched_preempt_guard_leave",
        "sched_quantum_ticks",
        "sched_preemptions",
        "app_arg_raw_path",
        "app_arg_friendly_path",
        "dialog_open_file",
        "dialog_active",
        "window_focused",
    };
}

static std::vector<std::string> read_imports(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("unable to open imports file: " + path);

    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line))
    {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;
        out.push_back(t);
    }
    return out;
}

static uint32_t align_up(uint32_t value, uint32_t align)
{
    if (!align)
        return value;
    uint32_t mask = align - 1u;
    return (value + mask) & ~mask;
}

static uint32_t arch_from_elf_machine(uint16_t machine)
{
    if (machine == EM_AARCH64)
        return SACX_ARCH_AA64;
    if (machine == EM_X86_64)
        return SACX_ARCH_X64;
    return SACX_ARCH_UNKNOWN;
}

static const char *arch_name(uint32_t arch)
{
    if (arch == SACX_ARCH_AA64)
        return "aa64";
    if (arch == SACX_ARCH_X64)
        return "x64";
    return "unknown";
}

static uint32_t relative_reloc_for_arch(uint32_t arch)
{
    if (arch == SACX_ARCH_AA64)
        return R_AARCH64_RELATIVE;
    if (arch == SACX_ARCH_X64)
        return R_X86_64_RELATIVE;
    return 0u;
}

struct PackedImage
{
    std::vector<uint8_t> bytes;
    std::string input;
    uint32_t arch;
    uint32_t segment_count;
    uint32_t reloc_count;
    uint32_t import_count;
    uint32_t image_size;
};

static PackedImage pack_elf_image(const std::string &input,
                                  const std::vector<std::string> &imports,
                                  uint32_t expected_arch)
{
    std::vector<uint8_t> elf = read_file(input);
    const Elf64_Ehdr &eh = read_struct<Elf64_Ehdr>(elf, 0);

    if (!(eh.e_ident[0] == 0x7F && eh.e_ident[1] == 'E' && eh.e_ident[2] == 'L' && eh.e_ident[3] == 'F'))
        throw std::runtime_error("input is not ELF: " + input);
    if (eh.e_ident[4] != 2 || eh.e_ident[5] != 1)
        throw std::runtime_error("only ELF64 little-endian is supported: " + input);

    uint32_t arch = arch_from_elf_machine(eh.e_machine);
    if (arch == SACX_ARCH_UNKNOWN)
        throw std::runtime_error("unsupported ELF machine in " + input);
    if (expected_arch != SACX_ARCH_UNKNOWN && arch != expected_arch)
        throw std::runtime_error("input ELF does not match requested " + std::string(arch_name(expected_arch)) + " slice: " + input);

    uint32_t relative_reloc = relative_reloc_for_arch(arch);
    if (!relative_reloc)
        throw std::runtime_error("no relative relocation support for " + std::string(arch_name(arch)));

    if (eh.e_phoff == 0 || eh.e_phnum == 0)
        throw std::runtime_error("ELF has no program headers: " + input);
    if (eh.e_phentsize != sizeof(Elf64_Phdr))
        throw std::runtime_error("unexpected ELF program header size: " + input);

    std::vector<Elf64_Phdr> phdrs;
    phdrs.reserve(eh.e_phnum);
    for (uint16_t i = 0; i < eh.e_phnum; ++i)
    {
        size_t off = static_cast<size_t>(eh.e_phoff) + static_cast<size_t>(i) * sizeof(Elf64_Phdr);
        phdrs.push_back(read_struct<Elf64_Phdr>(elf, off));
    }

    uint64_t lo = UINT64_MAX;
    uint64_t hi = 0;
    std::vector<size_t> load_idx;
    for (size_t i = 0; i < phdrs.size(); ++i)
    {
        const Elf64_Phdr &ph = phdrs[i];
        if (ph.p_type != PT_LOAD)
            continue;
        if (ph.p_vaddr < lo)
            lo = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > hi)
            hi = ph.p_vaddr + ph.p_memsz;
        load_idx.push_back(i);
    }
    if (load_idx.empty() || lo >= hi)
        throw std::runtime_error("ELF has no loadable segments: " + input);
    if ((hi - lo) > UINT32_MAX)
        throw std::runtime_error("ELF load span is too large for SACX: " + input);

    std::sort(load_idx.begin(), load_idx.end(), [&](size_t a, size_t b) {
        return phdrs[a].p_vaddr < phdrs[b].p_vaddr;
    });

    if (eh.e_entry < lo || eh.e_entry >= hi)
        throw std::runtime_error("ELF entry is outside load span: " + input);

    uint32_t image_size = static_cast<uint32_t>(hi - lo);
    uint32_t entry_rva = static_cast<uint32_t>(eh.e_entry - lo);

    std::vector<sacx_segment> out_segments;
    std::vector<uint8_t> image_blob;
    out_segments.reserve(load_idx.size());
    for (size_t idx : load_idx)
    {
        const Elf64_Phdr &ph = phdrs[idx];
        if (ph.p_filesz > 0 && ph.p_offset + ph.p_filesz > elf.size())
            throw std::runtime_error("segment bytes exceed ELF size: " + input);
        if (ph.p_vaddr < lo || ph.p_vaddr + ph.p_memsz > hi)
            throw std::runtime_error("segment virtual address range is invalid: " + input);
        if (ph.p_memsz > UINT32_MAX || ph.p_filesz > UINT32_MAX || image_blob.size() > UINT32_MAX)
            throw std::runtime_error("segment is too large for SACX: " + input);
        if (ph.p_filesz > (static_cast<uint64_t>(UINT32_MAX) - image_blob.size()))
            throw std::runtime_error("image blob is too large for SACX: " + input);

        sacx_segment seg{};
        seg.rva = static_cast<uint32_t>(ph.p_vaddr - lo);
        seg.file_offset = static_cast<uint32_t>(image_blob.size());
        seg.file_size = static_cast<uint32_t>(ph.p_filesz);
        seg.mem_size = static_cast<uint32_t>(ph.p_memsz);
        seg.flags = ph.p_flags;

        if (seg.file_size > 0)
        {
            size_t start = static_cast<size_t>(ph.p_offset);
            image_blob.insert(image_blob.end(), elf.begin() + static_cast<std::ptrdiff_t>(start),
                              elf.begin() + static_cast<std::ptrdiff_t>(start + ph.p_filesz));
        }

        out_segments.push_back(seg);
    }

    std::vector<sacx_reloc> out_relocs;
    for (const Elf64_Phdr &ph : phdrs)
    {
        if (ph.p_type != PT_DYNAMIC || ph.p_filesz == 0)
            continue;

        if (ph.p_offset + ph.p_filesz > elf.size())
            throw std::runtime_error("dynamic segment exceeds ELF size: " + input);

        uint64_t rela_va = 0;
        uint64_t rela_sz = 0;
        uint64_t rela_ent = 0;

        uint64_t count = ph.p_filesz / sizeof(Elf64_Dyn);
        for (uint64_t i = 0; i < count; ++i)
        {
            const Elf64_Dyn &dyn = read_struct<Elf64_Dyn>(elf, static_cast<size_t>(ph.p_offset + i * sizeof(Elf64_Dyn)));
            if (dyn.d_tag == DT_NULL)
                break;
            if (dyn.d_tag == DT_RELA)
                rela_va = dyn.d_val;
            else if (dyn.d_tag == DT_RELASZ)
                rela_sz = dyn.d_val;
            else if (dyn.d_tag == DT_RELAENT)
                rela_ent = dyn.d_val;
        }

        if (!rela_va || !rela_sz)
            continue;
        if (!rela_ent)
            rela_ent = sizeof(Elf64_Rela);
        if (rela_ent != sizeof(Elf64_Rela))
            throw std::runtime_error("unsupported RELA entry size: " + input);
        if ((rela_sz % rela_ent) != 0u)
            throw std::runtime_error("invalid RELA table size: " + input);

        uint64_t rela_file = va_to_file_off(phdrs, rela_va);
        uint64_t rela_count = rela_sz / rela_ent;
        if (rela_file + rela_count * sizeof(Elf64_Rela) > elf.size())
            throw std::runtime_error("RELA table exceeds ELF size: " + input);

        for (uint64_t ri = 0; ri < rela_count; ++ri)
        {
            const Elf64_Rela &rela = read_struct<Elf64_Rela>(elf, static_cast<size_t>(rela_file + ri * sizeof(Elf64_Rela)));
            uint32_t type = static_cast<uint32_t>(rela.r_info & 0xFFFFFFFFu);

            if (type != relative_reloc)
                throw std::runtime_error("unsupported relocation type in " + std::string(arch_name(arch)) + " app ELF: " + input);
            if (rela.r_offset < lo || rela.r_offset + 8u > hi)
                throw std::runtime_error("relocation target is outside image span: " + input);

            uint64_t addend = 0;
            if (rela.r_addend >= static_cast<int64_t>(lo))
                addend = static_cast<uint64_t>(rela.r_addend - static_cast<int64_t>(lo));
            else
                addend = static_cast<uint64_t>(rela.r_addend);
            if (addend >= image_size)
                throw std::runtime_error("relocation addend is outside image span: " + input);

            sacx_reloc rel{};
            rel.type = SACX_RELOC_RELATIVE64;
            rel.target_rva = static_cast<uint32_t>(rela.r_offset - lo);
            rel.addend = addend;
            out_relocs.push_back(rel);
        }
    }

    std::vector<sacx_import> out_imports;
    std::vector<uint8_t> strings;
    out_imports.reserve(imports.size());
    for (const std::string &name : imports)
    {
        if (strings.size() + name.size() + 1u > UINT32_MAX)
            throw std::runtime_error("import string table is too large: " + input);
        sacx_import imp{};
        imp.name_offset = static_cast<uint32_t>(strings.size());
        out_imports.push_back(imp);
        strings.insert(strings.end(), name.begin(), name.end());
        strings.push_back(0u);
    }

    sacx_header hdr{};
    hdr.magic = SACX_MAGIC;
    hdr.version = SACX_VERSION;
    hdr.header_size = static_cast<uint16_t>(sizeof(sacx_header));
    hdr.flags = 0u;
    hdr.crc32 = 0u;
    hdr.entry_rva = entry_rva;
    hdr.image_size = image_size;

    uint32_t off = sizeof(sacx_header);
    hdr.segment_offset = off;
    hdr.segment_count = static_cast<uint32_t>(out_segments.size());
    off += hdr.segment_count * sizeof(sacx_segment);

    off = align_up(off, 8u);
    hdr.reloc_offset = off;
    hdr.reloc_count = static_cast<uint32_t>(out_relocs.size());
    off += hdr.reloc_count * sizeof(sacx_reloc);

    off = align_up(off, 4u);
    hdr.import_offset = off;
    hdr.import_count = static_cast<uint32_t>(out_imports.size());
    off += hdr.import_count * sizeof(sacx_import);

    hdr.strings_offset = off;
    hdr.strings_size = static_cast<uint32_t>(strings.size());
    off += hdr.strings_size;

    hdr.image_offset = off;
    hdr.reserved0 = 0u;

    PackedImage packed{};
    packed.input = input;
    packed.arch = arch;
    packed.segment_count = hdr.segment_count;
    packed.reloc_count = hdr.reloc_count;
    packed.import_count = hdr.import_count;
    packed.image_size = hdr.image_size;
    packed.bytes.resize(static_cast<size_t>(hdr.image_offset) + image_blob.size(), 0u);
    std::memcpy(packed.bytes.data(), &hdr, sizeof(hdr));

    if (!out_segments.empty())
        std::memcpy(packed.bytes.data() + hdr.segment_offset, out_segments.data(), out_segments.size() * sizeof(sacx_segment));
    if (!out_relocs.empty())
        std::memcpy(packed.bytes.data() + hdr.reloc_offset, out_relocs.data(), out_relocs.size() * sizeof(sacx_reloc));
    if (!out_imports.empty())
        std::memcpy(packed.bytes.data() + hdr.import_offset, out_imports.data(), out_imports.size() * sizeof(sacx_import));
    if (!strings.empty())
        std::memcpy(packed.bytes.data() + hdr.strings_offset, strings.data(), strings.size());
    if (!image_blob.empty())
        std::memcpy(packed.bytes.data() + hdr.image_offset, image_blob.data(), image_blob.size());

    uint32_t crc = crc32_zero_range(packed.bytes.data(), packed.bytes.size(), offsetof(sacx_header, crc32), 4u);
    reinterpret_cast<sacx_header *>(packed.bytes.data())->crc32 = crc;
    return packed;
}

static std::vector<uint8_t> build_fat_container(const std::vector<PackedImage> &images)
{
    if (images.empty())
        throw std::runtime_error("no SACX slices were provided");
    if (images.size() > MAX_SACX_SLICES)
        throw std::runtime_error("too many SACX slices");

    uint64_t table_end = sizeof(sacx_fat_header) + images.size() * sizeof(sacx_slice);
    if (table_end > UINT32_MAX)
        throw std::runtime_error("SACX slice table is too large");

    std::vector<uint8_t> out;
    std::vector<sacx_slice> slices(images.size());
    out.resize(align_up(static_cast<uint32_t>(table_end), 8u), 0u);

    for (size_t i = 0; i < images.size(); ++i)
    {
        const PackedImage &image = images[i];
        for (size_t j = 0; j < i; ++j)
        {
            if (images[j].arch == image.arch)
                throw std::runtime_error("duplicate " + std::string(arch_name(image.arch)) + " SACX slice");
        }

        if (image.bytes.empty() || image.bytes.size() > UINT32_MAX)
            throw std::runtime_error("SACX slice is empty or too large: " + image.input);

        uint32_t file_offset = align_up(static_cast<uint32_t>(out.size()), 8u);
        if (file_offset > out.size())
            out.resize(file_offset, 0u);
        if (image.bytes.size() > (static_cast<uint64_t>(UINT32_MAX) - file_offset))
            throw std::runtime_error("SACX container is too large");

        slices[i].arch = image.arch;
        slices[i].kind = SACX_SLICE_KIND_NATIVE;
        slices[i].file_offset = file_offset;
        slices[i].file_size = static_cast<uint32_t>(image.bytes.size());
        slices[i].flags = 0u;
        slices[i].reserved0 = 0u;

        out.insert(out.end(), image.bytes.begin(), image.bytes.end());
    }

    sacx_fat_header fat{};
    fat.magic = SACX_MAGIC;
    fat.version = SACX_FAT_VERSION;
    fat.header_size = static_cast<uint16_t>(sizeof(sacx_fat_header));
    fat.flags = 0u;
    fat.crc32 = 0u;
    fat.slice_offset = sizeof(sacx_fat_header);
    fat.slice_count = static_cast<uint32_t>(images.size());
    fat.reserved0 = 0u;
    fat.reserved1 = 0u;

    std::memcpy(out.data(), &fat, sizeof(fat));
    std::memcpy(out.data() + fat.slice_offset, slices.data(), slices.size() * sizeof(sacx_slice));

    uint32_t crc = crc32_zero_range(out.data(), out.size(), offsetof(sacx_fat_header, crc32), 4u);
    reinterpret_cast<sacx_fat_header *>(out.data())->crc32 = crc;
    return out;
}

static void print_image_summary(const PackedImage &image)
{
    std::cout << "  " << arch_name(image.arch)
              << ": segments=" << image.segment_count
              << " relocs=" << image.reloc_count
              << " imports=" << image.import_count
              << " image_size=" << image.image_size
              << " input=" << image.input << "\n";
}

static void print_usage(void)
{
    std::cerr << "usage:\n";
    std::cerr << "  sacx_pack <input-aa64.elf> <output.sacx> [imports.txt]\n";
    std::cerr << "  sacx_pack --aa64 <aa64.elf> --x64 <x64.elf> -o <output.sacx> [--imports imports.txt]\n";
}

int main(int argc, char **argv)
{
    try
    {
        if (argc >= 2)
        {
            std::string first = argv[1];
            if (first == "--help" || first == "-h")
            {
                print_usage();
                return 0;
            }
        }

        if (argc >= 3 && argc <= 4 && argv[1][0] != '-')
        {
            std::string input = argv[1];
            std::string output = argv[2];
            std::vector<std::string> imports = (argc >= 4) ? read_imports(argv[3]) : default_imports();
            if (imports.empty())
                imports = default_imports();

            PackedImage image = pack_elf_image(input, imports, SACX_ARCH_AA64);
            write_file(output, image.bytes);

            std::cout << "packed legacy " << output << "\n";
            print_image_summary(image);
            return 0;
        }

        std::string aa64_input;
        std::string x64_input;
        std::string output;
        std::string imports_path;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            auto need_value = [&](const char *name) -> std::string {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("missing value for ") + name);
                return argv[++i];
            };

            if (arg == "--aa64" || arg == "--aarch64")
                aa64_input = need_value(arg.c_str());
            else if (arg == "--x64" || arg == "--x86_64")
                x64_input = need_value(arg.c_str());
            else if (arg == "-o" || arg == "--output")
                output = need_value(arg.c_str());
            else if (arg == "--imports")
                imports_path = need_value(arg.c_str());
            else
                throw std::runtime_error("unknown argument: " + arg);
        }

        if (output.empty() || (aa64_input.empty() && x64_input.empty()))
        {
            print_usage();
            return 2;
        }

        std::vector<std::string> imports = imports_path.empty() ? default_imports() : read_imports(imports_path);
        if (imports.empty())
            imports = default_imports();

        std::vector<PackedImage> images;
        if (!aa64_input.empty())
            images.push_back(pack_elf_image(aa64_input, imports, SACX_ARCH_AA64));
        if (!x64_input.empty())
            images.push_back(pack_elf_image(x64_input, imports, SACX_ARCH_X64));

        std::vector<uint8_t> fat = build_fat_container(images);
        write_file(output, fat);

        std::cout << "packed fat " << output << "\n";
        std::cout << "slices=" << images.size() << "\n";
        for (const PackedImage &image : images)
            print_image_summary(image);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "sacx_pack error: " << e.what() << "\n";
        return 1;
    }
}
