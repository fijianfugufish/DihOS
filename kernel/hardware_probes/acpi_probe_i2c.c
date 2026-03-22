#include "hardware_probes/acpi_probe_i2c.h"
#include "terminal/terminal_api.h"

#pragma pack(push, 1)

typedef struct
{
    char sig[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

typedef struct
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

#pragma pack(pop)

static int memeq_n(const char *a, const char *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static uint8_t checksum_ok(const void *ptr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    uint32_t i;

    for (i = 0; i < len; ++i)
        sum = (uint8_t)(sum + p[i]);

    return (sum == 0);
}

static void copy_sig4(char out[5], const char in[4])
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = 0;
}

static int is_printable_ascii(uint8_t c)
{
    return (c >= 32 && c <= 126);
}

static int nameseg_char_ok(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    if (c == '_')
        return 1;
    return 0;
}

static void hex_byte_to_str(uint8_t v, char *out)
{
    static const char *hex = "0123456789ABCDEF";
    out[0] = hex[(v >> 4) & 0xF];
    out[1] = hex[v & 0xF];
    out[2] = 0;
}

static void print_nameseg_line(const char *label, const uint8_t *p)
{
    char s[5];
    s[0] = (char)p[0];
    s[1] = (char)p[1];
    s[2] = (char)p[2];
    s[3] = (char)p[3];
    s[4] = 0;

    terminal_print(label);
    terminal_print(s);
}

static void print_string_line(const char *label, const uint8_t *p)
{
    char s[65];
    uint32_t i = 0;

    while (i < 64 && p[i] && p[i] >= 32 && p[i] <= 126)
    {
        s[i] = (char)p[i];
        ++i;
    }
    s[i] = 0;

    terminal_print(label);
    terminal_print(s);
}

static void print_hex_bytes_line(const uint8_t *p, uint32_t len)
{
    char line[16 * 3 + 1];
    uint32_t i;
    uint32_t n = 0;

    for (i = 0; i < len && i < 16; ++i)
    {
        char hx[3];
        hex_byte_to_str(p[i], hx);
        line[n++] = hx[0];
        line[n++] = hx[1];
        if (i + 1 < len && i + 1 < 16)
            line[n++] = ' ';
    }

    line[n] = 0;
    terminal_print(line);
}

static void print_ascii_chunk_line(const uint8_t *p, uint32_t len)
{
    char line[17];
    uint32_t i;
    uint32_t n = 0;

    for (i = 0; i < len && i < 16; ++i)
    {
        uint8_t c = p[i];
        line[n++] = is_printable_ascii(c) ? (char)c : '.';
    }
    line[n] = 0;
    terminal_print(line);
}

static uint64_t read_qword(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; ++i)
        v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

static uint32_t read_dword(const uint8_t *p)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; ++i)
        v |= ((uint32_t)p[i]) << (i * 8);
    return v;
}

/* AML package length decode */
static int aml_read_pkglen(const uint8_t *aml, uint32_t aml_len, uint32_t off,
                           uint32_t *pkglen_out, uint32_t *pkglen_bytes_out)
{
    uint8_t lead;
    uint32_t follow_count;
    uint32_t len;
    uint32_t i;

    if (!aml || off >= aml_len || !pkglen_out || !pkglen_bytes_out)
        return -1;

    lead = aml[off];
    follow_count = (uint32_t)((lead >> 6) & 0x3);

    if (off + 1 + follow_count > aml_len)
        return -1;

    if (follow_count == 0)
    {
        /* single-byte PkgLength uses low 6 bits */
        len = (uint32_t)(lead & 0x3F);
    }
    else
    {
        /* multi-byte PkgLength uses low 4 bits in lead, then following bytes */
        len = (uint32_t)(lead & 0x0F);

        for (i = 0; i < follow_count; ++i)
            len |= ((uint32_t)aml[off + 1 + i]) << (4 + 8 * i);
    }

    *pkglen_out = len;
    *pkglen_bytes_out = 1 + follow_count;
    return 0;
}

static const acpi_sdt_header_t *find_xsdt_or_rsdt(const acpi_rsdp_t *rsdp)
{
    if (!rsdp)
        return 0;

    if (rsdp->revision >= 2 && rsdp->xsdt_address)
        return (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;

    if (rsdp->rsdt_address)
        return (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)rsdp->rsdt_address;

    return 0;
}

static const acpi_sdt_header_t *find_fadt_from_root(const acpi_sdt_header_t *root)
{
    uint32_t i;

    if (!root)
        return 0;

    if (memeq_n(root->signature, "XSDT", 4))
    {
        uint32_t entries = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8u;
        const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));

        for (i = 0; i < entries; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)ptrs[i];
            if (hdr && memeq_n(hdr->signature, "FACP", 4))
                return hdr;
        }
    }
    else if (memeq_n(root->signature, "RSDT", 4))
    {
        uint32_t entries = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4u;
        const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));

        for (i = 0; i < entries; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)ptrs[i];
            if (hdr && memeq_n(hdr->signature, "FACP", 4))
                return hdr;
        }
    }

    return 0;
}

static const acpi_sdt_header_t *find_dsdt_from_fadt(const acpi_sdt_header_t *fadt)
{
    const uint8_t *p;
    uint32_t dsdt32 = 0;
    uint64_t xdsdt = 0;

    if (!fadt)
        return 0;

    p = (const uint8_t *)fadt;

    if (fadt->length >= 44)
        dsdt32 = *(const uint32_t *)(p + 40);

    if (fadt->length >= 148)
        xdsdt = *(const uint64_t *)(p + 140);

    if (xdsdt)
        return (const acpi_sdt_header_t *)(uintptr_t)xdsdt;

    if (dsdt32)
        return (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)dsdt32;

    return 0;
}

static int match_deviceop_nameseg(const uint8_t *aml, uint32_t aml_len, uint32_t devop_off, const char *name)
{
    uint32_t i = devop_off + 2;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;

    if (i >= aml_len)
        return 0;

    if (aml_read_pkglen(aml, aml_len, i, &pkglen, &pkglen_bytes) != 0)
        return 0;

    name_off = i + pkglen_bytes;

    if (name_off + 4 > aml_len)
        return 0;

    if (!nameseg_char_ok(aml[name_off + 0]) ||
        !nameseg_char_ok(aml[name_off + 1]) ||
        !nameseg_char_ok(aml[name_off + 2]) ||
        !nameseg_char_ok(aml[name_off + 3]))
        return 0;

    return (aml[name_off + 0] == (uint8_t)name[0] &&
            aml[name_off + 1] == (uint8_t)name[1] &&
            aml[name_off + 2] == (uint8_t)name[2] &&
            aml[name_off + 3] == (uint8_t)name[3]);
}

static int get_device_bounds(const uint8_t *aml, uint32_t aml_len, uint32_t devop_off,
                             uint32_t *name_off_out, uint32_t *body_start_out, uint32_t *end_out)
{
    uint32_t pkglen, pkglen_bytes;
    uint32_t pkglen_off;
    uint32_t name_off;
    uint32_t body_start;
    uint32_t end;

    if (!aml || devop_off + 2 >= aml_len)
        return -1;

    pkglen_off = devop_off + 2;
    if (aml_read_pkglen(aml, aml_len, pkglen_off, &pkglen, &pkglen_bytes) != 0)
        return -1;

    name_off = pkglen_off + pkglen_bytes;
    body_start = name_off + 4;
    end = body_start + (pkglen - 4);

    if (name_off + 4 > aml_len || body_start > aml_len)
        return -1;
    if (end > aml_len)
        end = aml_len;
    if (end < body_start)
        return -1;

    if (name_off_out)
        *name_off_out = name_off;
    if (body_start_out)
        *body_start_out = body_start;
    if (end_out)
        *end_out = end;

    return 0;
}

static int find_name_object(const uint8_t *buf, uint32_t start, uint32_t end, const char *name)
{
    uint32_t i;

    if (end <= start + 5)
        return -1;

    for (i = start; i + 5 <= end; ++i)
    {
        if (buf[i] == 0x08 &&
            buf[i + 1] == (uint8_t)name[0] &&
            buf[i + 2] == (uint8_t)name[1] &&
            buf[i + 3] == (uint8_t)name[2] &&
            buf[i + 4] == (uint8_t)name[3])
            return (int)i;
    }

    return -1;
}

static int find_method_object(const uint8_t *buf, uint32_t start, uint32_t end, const char *name)
{
    uint32_t i;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;

    if (!buf || !name)
        return -1;

    for (i = start; i < end; ++i)
    {
        if (buf[i] != 0x14) /* MethodOp */
            continue;

        if (aml_read_pkglen(buf, end, i + 1, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = i + 1 + pkglen_bytes;

        if (name_off + 4 > end)
            continue;

        if (buf[name_off + 0] == (uint8_t)name[0] &&
            buf[name_off + 1] == (uint8_t)name[1] &&
            buf[name_off + 2] == (uint8_t)name[2] &&
            buf[name_off + 3] == (uint8_t)name[3])
        {
            return (int)i;
        }
    }

    return -1;
}

static int find_named_buffer_in_range(const uint8_t *aml, uint32_t start, uint32_t end,
                                      const char *name, uint32_t *name_off_out)
{
    uint32_t i;

    if (!aml || !name || !name_off_out)
        return -1;

    if (end <= start + 6)
        return -1;

    for (i = start; i + 6 <= end; ++i)
    {
        /* NameOp + 4-char nameseg */
        if (aml[i] == 0x08 &&
            aml[i + 1] == (uint8_t)name[0] &&
            aml[i + 2] == (uint8_t)name[1] &&
            aml[i + 3] == (uint8_t)name[2] &&
            aml[i + 4] == (uint8_t)name[3])
        {
            /* expect BufferOp after the nameseg */
            if (i + 5 < end && aml[i + 5] == 0x11)
            {
                *name_off_out = i;
                return 0;
            }
        }
    }

    return -1;
}

static int get_method_bounds(const uint8_t *aml, uint32_t aml_len, uint32_t method_off,
                             uint32_t *name_off_out, uint32_t *body_start_out, uint32_t *body_end_out)
{
    uint32_t pkglen, pkglen_bytes;
    uint32_t pkglen_off;
    uint32_t pkg_start;
    uint32_t pkg_end;
    uint32_t name_off;
    uint32_t flags_off;
    uint32_t body_start;

    if (!aml || method_off >= aml_len)
        return -1;

    if (aml[method_off] != 0x14)
        return -1;

    pkglen_off = method_off + 1;

    if (aml_read_pkglen(aml, aml_len, pkglen_off, &pkglen, &pkglen_bytes) != 0)
        return -1;

    pkg_start = pkglen_off + pkglen_bytes;
    pkg_end = pkg_start + pkglen;

    if (pkg_end > aml_len)
        pkg_end = aml_len;

    name_off = pkg_start;
    flags_off = name_off + 4;
    body_start = flags_off + 1;

    if (name_off + 4 > aml_len || body_start > pkg_end)
        return -1;

    if (name_off_out)
        *name_off_out = name_off;
    if (body_start_out)
        *body_start_out = body_start;
    if (body_end_out)
        *body_end_out = pkg_end;

    return 0;
}

static int try_extract_named_buffer(const uint8_t *aml, uint32_t aml_len, uint32_t name_off,
                                    const uint8_t **buf_out, uint32_t *len_out)
{
    uint32_t p;
    uint32_t pkglen, pkglen_bytes;
    uint32_t bytecount;

    if (!aml || !buf_out || !len_out)
        return -1;

    /* skip NameOp + 4-char nameseg */
    p = name_off + 5;

    if (p >= aml_len)
        return -1;

    /* BufferOp */
    if (aml[p] != 0x11)
        return -1;
    p++;

    if (aml_read_pkglen(aml, aml_len, p, &pkglen, &pkglen_bytes) != 0)
        return -1;
    p += pkglen_bytes;

    if (p >= aml_len)
        return -1;

    /* PkgLength payload starts with buffer size termarg */
    if (aml[p] == 0x0A) /* BytePrefix */
    {
        if (p + 2 > aml_len)
            return -1;
        bytecount = aml[p + 1];
        p += 2;
    }
    else if (aml[p] <= 0x3F) /* small integer shortcut */
    {
        bytecount = aml[p];
        p += 1;
    }
    else
    {
        return -1;
    }

    if (p + bytecount > aml_len)
        return -1;

    *buf_out = aml + p;
    *len_out = bytecount;
    return 0;
}

static int find_string_bytes(const uint8_t *buf, uint32_t len, const char *needle, uint32_t needle_len, uint32_t start_off)
{
    uint32_t i, j;

    if (!buf || !needle || needle_len == 0 || start_off >= len)
        return -1;

    for (i = start_off; i + needle_len <= len; ++i)
    {
        for (j = 0; j < needle_len; ++j)
        {
            if (buf[i + j] != (uint8_t)needle[j])
                break;
        }
        if (j == needle_len)
            return (int)i;
    }

    return -1;
}

static void dump_window_hex_ascii(const uint8_t *buf, uint32_t len, uint32_t start, uint32_t bytes)
{
    uint32_t end = start + bytes;
    uint32_t pos = start;

    if (start >= len)
        return;
    if (end > len)
        end = len;

    while (pos < end)
    {
        uint32_t remain = end - pos;
        uint32_t take = (remain > 16) ? 16 : remain;

        terminal_print("hex:");
        print_hex_bytes_line(buf + pos, take);
        terminal_print("asc:");
        print_ascii_chunk_line(buf + pos, take);

        pos += take;
    }
}

static void dump_named_hit_window(const uint8_t *aml, uint32_t aml_len, const char *label, uint32_t hit_off, uint32_t pre, uint32_t post)
{
    uint32_t start = (hit_off > pre) ? (hit_off - pre) : 0;
    uint32_t bytes = pre + post;

    terminal_print(label);
    terminal_print_hex32(hit_off);
    dump_window_hex_ascii(aml, aml_len, start, bytes);
}

static void search_and_dump_global_string(const uint8_t *aml, uint32_t aml_len, const char *needle)
{
    uint32_t off = 0;
    uint32_t hits = 0;
    uint32_t needle_len = 0;

    while (needle[needle_len])
        ++needle_len;

    terminal_print("global search:");
    terminal_print(needle);

    while (1)
    {
        int found = find_string_bytes(aml, aml_len, needle, needle_len, off);
        if (found < 0)
            break;

        ++hits;
        terminal_print("hit:");
        terminal_print_hex32((uint32_t)found);
        dump_named_hit_window(aml, aml_len, "near:", (uint32_t)found, 32, 96);

        off = (uint32_t)found + 1;
        if (hits >= 8)
        {
            terminal_warn("hit limit reached");
            break;
        }
    }

    if (hits == 0)
        terminal_warn("no hits");
}

static void search_and_dump_global_nameseg(const uint8_t *aml, uint32_t aml_len, const char *name)
{
    uint32_t i;
    uint32_t hits = 0;

    terminal_print("global nameseg search:");
    terminal_print(name);

    for (i = 0; i + 4 <= aml_len; ++i)
    {
        if (aml[i + 0] == (uint8_t)name[0] &&
            aml[i + 1] == (uint8_t)name[1] &&
            aml[i + 2] == (uint8_t)name[2] &&
            aml[i + 3] == (uint8_t)name[3])
        {
            ++hits;
            terminal_print("nameseg hit:");
            terminal_print_hex32(i);
            dump_named_hit_window(aml, aml_len, "near:", i, 32, 96);

            if (hits >= 12)
            {
                terminal_warn("nameseg hit limit reached");
                break;
            }
        }
    }

    if (hits == 0)
        terminal_warn("no nameseg hits");
}

void acpi_parse_crs_for_i2c(const uint8_t *buf, uint32_t len)
{
    uint32_t i = 0;

    terminal_print("CRS decode start");

    while (i < len)
    {
        uint8_t tag = buf[i];

        if (tag == 0x79)
        {
            terminal_print("CRS end tag");
            break;
        }

        if (tag & 0x80)
        {
            uint8_t item = (uint8_t)(tag & 0x7F);
            uint16_t size;
            const uint8_t *payload;

            if (i + 3 > len)
                break;

            size = (uint16_t)(buf[i + 1] | ((uint16_t)buf[i + 2] << 8));
            if (i + 3 + size > len)
                break;

            payload = buf + i + 3;

            if (item == 0x06 && size == 9)
            {
                uint8_t write_status = payload[0];
                uint32_t base = read_dword(payload + 1);
                uint32_t span = read_dword(payload + 5);

                terminal_success("Memory32Fixed resource");
                terminal_print("write:");
                terminal_print_hex32((uint32_t)write_status);
                terminal_print("base:");
                terminal_print_hex32(base);
                terminal_print("size:");
                terminal_print_hex32(span);
            }
            else if (item == 0x09 && size >= 2)
            {
                uint8_t flags = payload[0];
                uint8_t irq_count = payload[1];
                uint32_t j;

                terminal_success("Extended IRQ resource");
                terminal_print("irq flags:");
                terminal_print_hex32((uint32_t)flags);
                terminal_print("irq count:");
                terminal_print_hex32((uint32_t)irq_count);

                for (j = 0; j < irq_count; ++j)
                {
                    uint32_t off = 2 + j * 4;
                    if (off + 4 <= size)
                    {
                        uint32_t irq = read_dword(payload + off);
                        terminal_print("irq:");
                        terminal_print_hex32(irq);
                    }
                }
            }
            else
            {
                terminal_warn("unknown large resource");
                terminal_print("item:");
                terminal_print_hex32((uint32_t)item);
                terminal_print("size:");
                terminal_print_hex32((uint32_t)size);
            }

            i += (uint32_t)(3 + size);
        }
        else
        {
            uint8_t item = (uint8_t)((tag >> 3) & 0x0F);
            uint8_t size = (uint8_t)(tag & 0x07);

            if (i + 1 + size > len)
                break;

            terminal_warn("small resource");
            terminal_print("item:");
            terminal_print_hex32((uint32_t)item);
            terminal_print("size:");
            terminal_print_hex32((uint32_t)size);

            i += (uint32_t)(1 + size);
        }
    }

    terminal_print("CRS decode end");
}

static int try_extract_buffer_after_name(const uint8_t *aml, uint32_t aml_len, uint32_t name_off,
                                         const uint8_t **buf_out, uint32_t *len_out)
{
    uint32_t p;
    uint32_t pkglen, pkglen_bytes;
    uint32_t bytecount;

    if (!aml || !buf_out || !len_out)
        return -1;

    p = name_off + 5;

    if (p >= aml_len)
        return -1;

    if (aml[p] != 0x11)
        return -1;
    p++;

    if (aml_read_pkglen(aml, aml_len, p, &pkglen, &pkglen_bytes) != 0)
        return -1;
    p += pkglen_bytes;

    if (p >= aml_len)
        return -1;

    if (aml[p] == 0x0A)
    {
        if (p + 2 > aml_len)
            return -1;
        bytecount = aml[p + 1];
        p += 2;
    }
    else if (aml[p] <= 0x3F)
    {
        bytecount = aml[p];
        p += 1;
    }
    else
    {
        return -1;
    }

    if (p + bytecount > aml_len)
        return -1;

    *buf_out = aml + p;
    *len_out = bytecount;
    return 0;
}

static void dump_crs_preview(const uint8_t *aml, uint32_t aml_len, uint32_t body_start, uint32_t body_end)
{
    int crs_name_off = find_name_object(aml, body_start, body_end, "_CRS");
    int crs_method_off = find_method_object(aml, body_start, body_end, "_CRS");

    if (crs_name_off < 0 && crs_method_off < 0)
    {
        terminal_warn("_CRS not found in device body");
        return;
    }

    if (crs_name_off >= 0)
    {
        const uint8_t *crs_buf = 0;
        uint32_t crs_len = 0;

        terminal_success("_CRS name found");
        terminal_print("_CRS name off:");
        terminal_print_hex32((uint32_t)crs_name_off);

        dump_window_hex_ascii(aml, aml_len, (uint32_t)crs_name_off, 96);

        if (try_extract_buffer_after_name(aml, aml_len, (uint32_t)crs_name_off, &crs_buf, &crs_len) == 0)
        {
            terminal_success("_CRS buffer extracted");
            terminal_print("_CRS buf len:");
            terminal_print_hex32(crs_len);

            dump_window_hex_ascii(crs_buf, crs_len, 0, (crs_len > 96) ? 96 : crs_len);
            acpi_parse_crs_for_i2c(crs_buf, crs_len);
        }
        else
        {
            terminal_warn("_CRS name exists but buffer extract failed");
        }
    }

    if (crs_method_off >= 0)
    {
        uint32_t method_name_off = 0;
        uint32_t method_body_start = 0;
        uint32_t method_body_end = 0;
        uint32_t rbuf_name_off = 0;
        const uint8_t *crs_buf = 0;
        uint32_t crs_len = 0;

        terminal_success("_CRS method found");
        terminal_print("_CRS method off:");
        terminal_print_hex32((uint32_t)crs_method_off);

        if (get_method_bounds(aml, aml_len, (uint32_t)crs_method_off,
                              &method_name_off, &method_body_start, &method_body_end) != 0)
        {
            terminal_error("_CRS method bounds decode failed");
            return;
        }

        terminal_print("_CRS method body start:");
        terminal_print_hex32(method_body_start);
        terminal_print("_CRS method body end:");
        terminal_print_hex32(method_body_end);

        dump_window_hex_ascii(aml, aml_len, (uint32_t)crs_method_off, 128);

        if (find_named_buffer_in_range(aml, method_body_start, method_body_end, "RBUF", &rbuf_name_off) == 0)
        {
            terminal_success("RBUF found in _CRS method");
            terminal_print("RBUF name off:");
            terminal_print_hex32(rbuf_name_off);

            if (try_extract_named_buffer(aml, aml_len, rbuf_name_off, &crs_buf, &crs_len) == 0)
            {
                terminal_success("_CRS method buffer extracted");
                terminal_print("_CRS buf len:");
                terminal_print_hex32(crs_len);

                dump_window_hex_ascii(crs_buf, crs_len, 0, (crs_len > 96) ? 96 : crs_len);
                acpi_parse_crs_for_i2c(crs_buf, crs_len);
            }
            else
            {
                terminal_warn("RBUF found but buffer extract failed");
            }
        }
        else
        {
            terminal_warn("RBUF not found in _CRS method body");
        }
    }
}

static void dump_full_device_body(const uint8_t *aml, uint32_t aml_len, uint32_t body_start, uint32_t body_end)
{
    terminal_print("device body dump start");
    terminal_print("body start:");
    terminal_print_hex32(body_start);
    terminal_print("body end:");
    terminal_print_hex32(body_end);

    if (body_end > body_start)
        dump_window_hex_ascii(aml, aml_len, body_start, body_end - body_start);

    terminal_print("device body dump end");
}

static void summarise_i2c_device(const uint8_t *aml, uint32_t aml_len, uint32_t devop_off)
{
    uint32_t name_off, body_start, body_end;
    int off;

    if (get_device_bounds(aml, aml_len, devop_off, &name_off, &body_start, &body_end) != 0)
    {
        terminal_error("device bounds decode failed");
        return;
    }

    terminal_print("deviceop off:");
    terminal_print_hex32(devop_off);
    print_nameseg_line("device: ", aml + name_off);
    terminal_print("body start:");
    terminal_print_hex32(body_start);
    terminal_print("body end:");
    terminal_print_hex32(body_end);

    off = find_name_object(aml, body_start, body_end, "_HID");
    if (off >= 0)
    {
        terminal_print("_HID name found");
        if ((uint32_t)off + 6 < body_end && aml[(uint32_t)off + 5] == 0x0D)
            print_string_line("_HID str: ", aml + (uint32_t)off + 6);
    }

    off = find_name_object(aml, body_start, body_end, "_CID");
    if (off >= 0)
    {
        terminal_print("_CID name found");
        if ((uint32_t)off + 6 < body_end && aml[(uint32_t)off + 5] == 0x0D)
            print_string_line("_CID str: ", aml + (uint32_t)off + 6);
    }

    off = find_name_object(aml, body_start, body_end, "_UID");
    if (off >= 0)
        terminal_print("_UID name found");

    off = find_name_object(aml, body_start, body_end, "_DEP");
    if (off >= 0)
        terminal_print("_DEP name found");

    dump_crs_preview(aml, aml_len, body_start, body_end);

    terminal_print("----------------");
}

static void run_global_aml_searches(const uint8_t *aml, uint32_t aml_len)
{
    terminal_print("global AML searches start");

    search_and_dump_global_nameseg(aml, aml_len, "_CRS");
    search_and_dump_global_nameseg(aml, aml_len, "_DSD");
    search_and_dump_global_nameseg(aml, aml_len, "_DEP");
    search_and_dump_global_nameseg(aml, aml_len, "_STA");
    search_and_dump_global_nameseg(aml, aml_len, "_UID");

    search_and_dump_global_string(aml, aml_len, "I2C1");
    search_and_dump_global_string(aml, aml_len, "I2C3");
    search_and_dump_global_string(aml, aml_len, "I2C9");

    search_and_dump_global_string(aml, aml_len, "QCOM0C10");
    search_and_dump_global_string(aml, aml_len, "QCOMFFEA");
    search_and_dump_global_string(aml, aml_len, "QCOMFFEE");

    search_and_dump_global_string(aml, aml_len, "I2CSerialBus");
    search_and_dump_global_string(aml, aml_len, "SerialBus");
    search_and_dump_global_string(aml, aml_len, "GpioInt");
    search_and_dump_global_string(aml, aml_len, "GpioIo");
    search_and_dump_global_string(aml, aml_len, "ResourceTemplate");

    terminal_print("global AML searches end");
}

static void probe_dsdt_for_i2c_targets(const acpi_sdt_header_t *dsdt)
{
    const uint8_t *table;
    const uint8_t *aml;
    uint32_t table_len;
    uint32_t aml_len;
    uint32_t i;
    uint32_t hits = 0;

    if (!dsdt)
    {
        terminal_error("i2c probe: no DSDT");
        return;
    }

    if (!memeq_n(dsdt->signature, "DSDT", 4))
    {
        terminal_error("i2c probe: target is not DSDT");
        return;
    }

    if (!checksum_ok(dsdt, dsdt->length))
        terminal_warn("i2c probe: DSDT checksum bad");
    else
        terminal_success("i2c probe: DSDT checksum ok");

    table = (const uint8_t *)dsdt;
    table_len = dsdt->length;

    if (table_len <= sizeof(acpi_sdt_header_t))
    {
        terminal_error("i2c probe: DSDT too short");
        return;
    }

    aml = table + sizeof(acpi_sdt_header_t);
    aml_len = table_len - (uint32_t)sizeof(acpi_sdt_header_t);

    terminal_print("i2c probe: DSDT phys:");
    terminal_print_hex64((uint64_t)(uintptr_t)dsdt);
    terminal_print("i2c probe: AML len:");
    terminal_print_hex32(aml_len);

    for (i = 0; i + 6 < aml_len; ++i)
    {
        if (aml[i] == 0x5B && aml[i + 1] == 0x82)
        {
            if (match_deviceop_nameseg(aml, aml_len, i, "I2C1"))
            {
                ++hits;
                terminal_success("found target I2C1 device");
                terminal_print("hit #:");
                terminal_print_hex32(hits);
                summarise_i2c_device(aml, aml_len, i);
                break;
            }
        }
    }

    if (hits == 0)
        terminal_warn("i2c probe: no I2C1 DeviceOp found");
    else
        terminal_print("i2c probe: I2C1 done");
}

void acpi_probe_i2c_from_rsdp(uint64_t rsdp_phys)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;
    const acpi_sdt_header_t *fadt;
    const acpi_sdt_header_t *dsdt;

    terminal_print("acpi i2c probe start");

    if (!rsdp_phys)
    {
        terminal_error("i2c probe: rsdp is null");
        return;
    }

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
    {
        terminal_error("i2c probe: bad RSDP sig");
        return;
    }

    root = find_xsdt_or_rsdt(rsdp);
    if (!root)
    {
        terminal_error("i2c probe: no XSDT/RSDT");
        return;
    }

    terminal_print("i2c probe: root sig:");
    if (memeq_n(root->signature, "XSDT", 4))
        terminal_print("XSDT");
    else if (memeq_n(root->signature, "RSDT", 4))
        terminal_print("RSDT");
    else
    {
        char sig[5];
        copy_sig4(sig, root->signature);
        terminal_print(sig);
    }

    fadt = find_fadt_from_root(root);
    if (!fadt)
    {
        terminal_error("i2c probe: no FADT");
        return;
    }

    dsdt = find_dsdt_from_fadt(fadt);
    if (!dsdt)
    {
        terminal_error("i2c probe: no DSDT/XDSDT");
        return;
    }

    probe_dsdt_for_i2c_targets(dsdt);

    terminal_print("acpi i2c probe done");
}