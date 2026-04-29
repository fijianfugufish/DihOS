#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
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
static constexpr uint32_t PT_LOAD = 1u;
static constexpr uint32_t PT_DYNAMIC = 2u;
static constexpr int64_t DT_NULL = 0;
static constexpr int64_t DT_RELA = 7;
static constexpr int64_t DT_RELASZ = 8;
static constexpr int64_t DT_RELAENT = 9;
static constexpr uint32_t R_AARCH64_RELATIVE = 1027u;

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

int main(int argc, char **argv)
{
    try
    {
        if (argc < 3 || argc > 4)
        {
            std::cerr << "usage: sacx_pack <input.elf> <output.sacx> [imports.txt]\n";
            return 2;
        }

        std::string input = argv[1];
        std::string output = argv[2];
        std::vector<std::string> imports = (argc >= 4) ? read_imports(argv[3]) : default_imports();
        if (imports.empty())
            imports = default_imports();

        std::vector<uint8_t> elf = read_file(input);
        const Elf64_Ehdr &eh = read_struct<Elf64_Ehdr>(elf, 0);

        if (!(eh.e_ident[0] == 0x7F && eh.e_ident[1] == 'E' && eh.e_ident[2] == 'L' && eh.e_ident[3] == 'F'))
            throw std::runtime_error("input is not ELF");
        if (eh.e_ident[4] != 2 || eh.e_ident[5] != 1)
            throw std::runtime_error("only ELF64 little-endian is supported");
        if (eh.e_machine != EM_AARCH64)
            throw std::runtime_error("input ELF is not AArch64");
        if (eh.e_phoff == 0 || eh.e_phnum == 0)
            throw std::runtime_error("ELF has no program headers");
        if (eh.e_phentsize != sizeof(Elf64_Phdr))
            throw std::runtime_error("unexpected ELF program header size");

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
            throw std::runtime_error("ELF has no loadable segments");

        std::sort(load_idx.begin(), load_idx.end(), [&](size_t a, size_t b) {
            return phdrs[a].p_vaddr < phdrs[b].p_vaddr;
        });

        if (eh.e_entry < lo || eh.e_entry >= hi)
            throw std::runtime_error("ELF entry is outside load span");

        uint32_t image_size = static_cast<uint32_t>(hi - lo);
        uint32_t entry_rva = static_cast<uint32_t>(eh.e_entry - lo);

        std::vector<sacx_segment> out_segments;
        std::vector<uint8_t> image_blob;
        out_segments.reserve(load_idx.size());
        for (size_t idx : load_idx)
        {
            const Elf64_Phdr &ph = phdrs[idx];
            if (ph.p_filesz > 0 && ph.p_offset + ph.p_filesz > elf.size())
                throw std::runtime_error("segment bytes exceed ELF size");
            if (ph.p_vaddr < lo || ph.p_vaddr + ph.p_memsz > hi)
                throw std::runtime_error("segment virtual address range is invalid");

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
                throw std::runtime_error("dynamic segment exceeds ELF size");

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
                throw std::runtime_error("unsupported RELA entry size");
            if ((rela_sz % rela_ent) != 0u)
                throw std::runtime_error("invalid RELA table size");

            uint64_t rela_file = va_to_file_off(phdrs, rela_va);
            uint64_t rela_count = rela_sz / rela_ent;
            if (rela_file + rela_count * sizeof(Elf64_Rela) > elf.size())
                throw std::runtime_error("RELA table exceeds ELF size");

            for (uint64_t ri = 0; ri < rela_count; ++ri)
            {
                const Elf64_Rela &rela = read_struct<Elf64_Rela>(elf, static_cast<size_t>(rela_file + ri * sizeof(Elf64_Rela)));
                uint32_t type = static_cast<uint32_t>(rela.r_info & 0xFFFFFFFFu);

                if (type != R_AARCH64_RELATIVE)
                    throw std::runtime_error("unsupported relocation type in app ELF");
                if (rela.r_offset < lo || rela.r_offset + 8u > hi)
                    throw std::runtime_error("relocation target is outside image span");

                uint64_t addend = 0;
                if (rela.r_addend >= static_cast<int64_t>(lo))
                    addend = static_cast<uint64_t>(rela.r_addend - static_cast<int64_t>(lo));
                else
                    addend = static_cast<uint64_t>(rela.r_addend);
                if (addend >= image_size)
                    throw std::runtime_error("relocation addend is outside image span");

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

        auto align_up = [](uint32_t value, uint32_t align) -> uint32_t {
            if (!align)
                return value;
            uint32_t mask = align - 1u;
            return (value + mask) & ~mask;
        };

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

        std::vector<uint8_t> out;
        out.resize(static_cast<size_t>(hdr.image_offset) + image_blob.size(), 0u);
        std::memcpy(out.data(), &hdr, sizeof(hdr));

        if (!out_segments.empty())
            std::memcpy(out.data() + hdr.segment_offset, out_segments.data(), out_segments.size() * sizeof(sacx_segment));
        if (!out_relocs.empty())
            std::memcpy(out.data() + hdr.reloc_offset, out_relocs.data(), out_relocs.size() * sizeof(sacx_reloc));
        if (!out_imports.empty())
            std::memcpy(out.data() + hdr.import_offset, out_imports.data(), out_imports.size() * sizeof(sacx_import));
        if (!strings.empty())
            std::memcpy(out.data() + hdr.strings_offset, strings.data(), strings.size());
        if (!image_blob.empty())
            std::memcpy(out.data() + hdr.image_offset, image_blob.data(), image_blob.size());

        uint32_t crc = crc32_zero_range(out.data(), out.size(), offsetof(sacx_header, crc32), 4u);
        reinterpret_cast<sacx_header *>(out.data())->crc32 = crc;

        write_file(output, out);

        std::cout << "packed " << output << "\n";
        std::cout << "segments=" << hdr.segment_count
                  << " relocs=" << hdr.reloc_count
                  << " imports=" << hdr.import_count
                  << " image_size=" << hdr.image_size << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "sacx_pack error: " << e.what() << "\n";
        return 1;
    }
}
