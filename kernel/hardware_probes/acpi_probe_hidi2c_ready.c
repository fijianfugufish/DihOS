#include "hardware_probes/acpi_probe_hidi2c_ready.h"
#include "terminal/terminal_api.h"
#include <stdint.h>
#include <stddef.h>

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

typedef struct
{
    char name[5];

    char hid[16];
    char cid[16];

    uint8_t has_sta;
    uint8_t has_ini;
    uint8_t has_crs;
    uint8_t has_srs;
    uint8_t has_ps0;
    uint8_t has_ps2;
    uint8_t has_ps3;
    uint8_t has_dis;
    uint8_t has_rst;
    uint8_t has_dsm;
    uint8_t has_dsd;
    uint8_t has_dep;
    uint8_t has_pr0;
    uint8_t has_pr3;

    uint8_t ref_i2c1;
    uint8_t body_has_i2c_serialbus;
    uint8_t body_has_gpioint;
    uint8_t dep_refs_i2c1;
    uint8_t dsm_argcount;

    uint8_t crs_is_name_buffer;
    uint8_t crs_is_method_buffer;
    uint8_t crs_has_serialbus;
    uint8_t crs_has_gpio;
    uint8_t crs_small_count;
    uint8_t crs_large_count;
    uint16_t crs_buf_len;
    const uint8_t *crs_buf;

    uint8_t sb_found;
    uint8_t sb_bus_type;
    uint8_t sb_gen_rev;
    uint8_t sb_type_rev;
    uint8_t sb_flags_lo;
    uint8_t sb_flags_hi;
    uint16_t sb_type_data_len;
    uint32_t sb_speed_hz;
    uint16_t sb_slave_addr;
    uint16_t sb_source_off;
    char sb_source[32];

    uint8_t gpio_found;
    uint8_t gpio_conn_type;
    uint16_t gpio_flags;
    uint8_t gpio_pin_cfg;
    uint16_t gpio_pin_table_off;
    uint16_t gpio_source_off;
    uint16_t gpio_first_pin;
    char gpio_source[32];

    uint8_t dsm_hid_desc_ok;
    uint32_t dsm_hid_desc_addr;
    uint8_t dsm_guid_ascii_seen;
    uint8_t dsm_ret_count;
    uint8_t dsm_has_if_arg1_eq_1;
    uint8_t dsm_has_if_arg2_eq_1;
    uint8_t dsm_has_arg0_ref;
    uint8_t dsm_has_arg1_ref;
    uint8_t dsm_has_arg2_ref;
    uint8_t dsm_has_uuid_buffer;
    uint8_t dsm_has_func1_compare;
    uint8_t dsm_has_rev1_compare;
    uint32_t dsm_ret[12];

    char parent_name[5];
    uint8_t parent_has_ps0;
    uint8_t parent_has_ps2;
    uint8_t parent_has_ps3;
    uint8_t parent_has_on;
    uint8_t parent_has_off;
    uint8_t parent_has_rst;
    uint8_t parent_has_pr0;
    uint8_t parent_has_pr3;
    uint8_t parent_has_power_resource;
    uint8_t parent_mentions_child;
    uint8_t parent_mentions_gpio_src;
    uint8_t parent_mentions_sb_src;
    uint8_t parent_has_sta;
    uint8_t parent_has_ini;

    char grandparent_name[5];
    uint8_t grandparent_has_ps0;
    uint8_t grandparent_has_ps2;
    uint8_t grandparent_has_ps3;
    uint8_t grandparent_has_on;
    uint8_t grandparent_has_off;
    uint8_t grandparent_has_rst;
    uint8_t grandparent_has_pr0;
    uint8_t grandparent_has_pr3;
    uint8_t grandparent_has_power_resource;
    uint8_t grandparent_mentions_child;
    uint8_t grandparent_mentions_gpio_src;
    uint8_t grandparent_mentions_sb_src;
    uint8_t grandparent_has_sta;
    uint8_t grandparent_has_ini;

    char ggparent_name[5];
    uint8_t ggparent_has_ps0;
    uint8_t ggparent_has_ps2;
    uint8_t ggparent_has_ps3;
    uint8_t ggparent_has_on;
    uint8_t ggparent_has_off;
    uint8_t ggparent_has_rst;
    uint8_t ggparent_has_pr0;
    uint8_t ggparent_has_pr3;
    uint8_t ggparent_has_power_resource;
    uint8_t ggparent_mentions_child;
    uint8_t ggparent_mentions_gpio_src;
    uint8_t ggparent_mentions_sb_src;
    uint8_t ggparent_has_sta;
    uint8_t ggparent_has_ini;

    uint32_t parent_body_start;
    uint32_t parent_body_end;
    uint32_t grandparent_body_start;
    uint32_t grandparent_body_end;
    uint32_t ggparent_body_start;
    uint32_t ggparent_body_end;
    uint32_t parent_obj_off;
    uint32_t grandparent_obj_off;
    uint32_t ggparent_obj_off;
} hidi2c_acpi_summary_t;

typedef struct
{
    uint32_t d1;
    uint16_t d2;
    uint16_t d3;
    uint8_t d4[8];
} guid128_t;

static const guid128_t HID_I2C_GUID = {
    0x3CDFF6F7u, 0x4267u, 0x4555u, {0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE}};

static hidi2c_acpi_regs g_hidi2c_regs;

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

static uint32_t read_dword(const uint8_t *p)
{
    uint32_t v = 0;
    uint32_t i;
    for (i = 0; i < 4; ++i)
        v |= ((uint32_t)p[i]) << (i * 8);
    return v;
}

static uint64_t read_qword(const uint8_t *p)
{
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8; ++i)
        v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

static void copy_sig4(char out[5], const uint8_t *in)
{
    out[0] = (char)in[0];
    out[1] = (char)in[1];
    out[2] = (char)in[2];
    out[3] = (char)in[3];
    out[4] = 0;
}

static void copy_sig4_printable(char out[5], const uint8_t *in)
{
    uint32_t i;

    if (!out)
        return;

    for (i = 0; i < 4; ++i)
    {
        uint8_t c = in ? in[i] : 0u;
        out[i] = (c >= 32u && c <= 126u) ? (char)c : '?';
    }

    out[4] = 0;
}

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int aml_read_pkglen(const uint8_t *aml, uint32_t aml_len, uint32_t off,
                           uint32_t *pkglen_out, uint32_t *pkglen_bytes_out)
{
    uint8_t lead;
    uint32_t follow_count;
    uint32_t len;
    uint32_t i;

    if (!aml || !pkglen_out || !pkglen_bytes_out || off >= aml_len)
        return -1;

    lead = aml[off];
    follow_count = (uint32_t)((lead >> 6) & 0x3);

    if (off + 1 + follow_count > aml_len)
        return -1;

    if (follow_count == 0)
    {
        len = (uint32_t)(lead & 0x3F);
    }
    else
    {
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
        dsdt32 = read_dword(p + 40);

    if (fadt->length >= 148)
        xdsdt = read_qword(p + 140);

    if (xdsdt)
        return (const acpi_sdt_header_t *)(uintptr_t)xdsdt;

    if (dsdt32)
        return (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)dsdt32;

    return 0;
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

    if (pkglen < 4)
        return -1;

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

static int get_scope_bounds(const uint8_t *aml, uint32_t aml_len, uint32_t scopeop_off,
                            uint32_t *name_off_out, uint32_t *body_start_out, uint32_t *end_out)
{
    uint32_t pkglen, pkglen_bytes;
    uint32_t pkglen_off;
    uint32_t name_off;
    uint32_t body_start;
    uint32_t end;

    if (!aml || scopeop_off + 1 >= aml_len)
        return -1;

    pkglen_off = scopeop_off + 1;
    if (aml_read_pkglen(aml, aml_len, pkglen_off, &pkglen, &pkglen_bytes) != 0)
        return -1;

    name_off = pkglen_off + pkglen_bytes;
    body_start = name_off + 4;

    if (pkglen < 4)
        return -1;

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

static int find_enclosing_parent_object(const uint8_t *aml, uint32_t aml_len, uint32_t child_devop_off,
                                        uint32_t *parent_name_off_out,
                                        uint32_t *parent_body_start_out,
                                        uint32_t *parent_end_out,
                                        uint32_t *parent_obj_off_out)
{
    uint32_t i;
    uint32_t best_obj_off = 0;
    uint32_t best_name_off = 0;
    uint32_t best_body_start = 0;
    uint32_t best_end = 0;
    uint8_t found = 0;

    if (!aml || child_devop_off >= aml_len)
        return -1;

    for (i = 0; i + 6 < child_devop_off; ++i)
    {
        uint32_t name_off = 0;
        uint32_t body_start = 0;
        uint32_t end = 0;
        int ok = 0;

        if (aml[i] == 0x5B && aml[i + 1] == 0x82) /* DeviceOp */
            ok = (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &end) == 0);
        else if (aml[i] == 0x10) /* ScopeOp */
            ok = (get_scope_bounds(aml, aml_len, i, &name_off, &body_start, &end) == 0);

        if (!ok)
            continue;

        if (body_start <= child_devop_off && child_devop_off < end)
        {
            if (!found || body_start >= best_body_start)
            {
                best_obj_off = i;
                best_name_off = name_off;
                best_body_start = body_start;
                best_end = end;
                found = 1;
            }
        }
    }

    if (!found)
        return -1;

    if (parent_name_off_out)
        *parent_name_off_out = best_name_off;
    if (parent_body_start_out)
        *parent_body_start_out = best_body_start;
    if (parent_end_out)
        *parent_end_out = best_end;
    if (parent_obj_off_out)
        *parent_obj_off_out = best_obj_off;

    return 0;
}

static int find_name_object(const uint8_t *buf, uint32_t start, uint32_t end, const char *name)
{
    uint32_t i;

    if (!buf || !name || end <= start + 5)
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
        if (buf[i] != 0x14)
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
            return (int)i;
    }

    return -1;
}

static void eisaid_to_string(uint32_t v, char out[8])
{
    out[0] = (char)(((v >> 26) & 0x1Fu) + '@');
    out[1] = (char)(((v >> 21) & 0x1Fu) + '@');
    out[2] = (char)(((v >> 16) & 0x1Fu) + '@');

    {
        static const char hex[] = "0123456789ABCDEF";
        uint16_t prod = (uint16_t)(v & 0xFFFFu);
        out[3] = hex[(prod >> 12) & 0xFu];
        out[4] = hex[(prod >> 8) & 0xFu];
        out[5] = hex[(prod >> 4) & 0xFu];
        out[6] = hex[(prod >> 0) & 0xFu];
        out[7] = 0;
    }
}

static int extract_name_id(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                           const char *name, char *out, uint32_t out_cap)
{
    int off;
    uint32_t p;
    uint32_t i = 0;

    if (!aml || !out || out_cap == 0)
        return -1;

    out[0] = 0;

    off = find_name_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    p = (uint32_t)off + 5;
    if (p >= body_end)
        return -1;

    if (aml[p] == 0x0D)
    {
        p++;
        while (p < body_end && aml[p] != 0 && i + 1 < out_cap)
        {
            uint8_t c = aml[p++];
            if (c < 32 || c > 126)
                break;
            out[i++] = (char)c;
        }
        out[i] = 0;
        return (i != 0) ? 0 : -1;
    }

    if (aml[p] == 0x0C && p + 4 < body_end)
    {
        char tmp[8];
        uint32_t v = read_dword(aml + p + 1);
        eisaid_to_string(v, tmp);

        i = 0;
        while (tmp[i] && i + 1 < out_cap)
        {
            out[i] = tmp[i];
            i++;
        }
        out[i] = 0;
        return (i != 0) ? 0 : -1;
    }

    return -1;
}

static uint8_t has_name_or_method(const uint8_t *aml, uint32_t body_start, uint32_t body_end, const char *name)
{
    if (find_name_object(aml, body_start, body_end, name) >= 0)
        return 1;
    if (find_method_object(aml, body_start, body_end, name) >= 0)
        return 1;
    return 0;
}

static uint8_t body_contains_nameseg(const uint8_t *aml, uint32_t body_start, uint32_t body_end, const char *name)
{
    uint32_t i;

    if (!aml || !name || body_end < body_start + 4)
        return 0;

    for (i = body_start; i + 4 <= body_end; ++i)
    {
        if (aml[i + 0] == (uint8_t)name[0] &&
            aml[i + 1] == (uint8_t)name[1] &&
            aml[i + 2] == (uint8_t)name[2] &&
            aml[i + 3] == (uint8_t)name[3])
            return 1;
    }

    return 0;
}

static uint8_t body_contains_bytes(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                   const char *pat, uint32_t pat_len)
{
    uint32_t i, j;

    if (!aml || !pat || pat_len == 0 || body_end < body_start + pat_len)
        return 0;

    for (i = body_start; i + pat_len <= body_end; ++i)
    {
        for (j = 0; j < pat_len; ++j)
        {
            if (aml[i + j] != (uint8_t)pat[j])
                break;
        }
        if (j == pat_len)
            return 1;
    }

    return 0;
}

static uint8_t body_contains_bytes_exact(const uint8_t *aml, uint32_t start, uint32_t end,
                                         const uint8_t *pat, uint32_t pat_len)
{
    uint32_t i, j;

    if (!aml || !pat || pat_len == 0 || end < start + pat_len)
        return 0;

    for (i = start; i + pat_len <= end; ++i)
    {
        for (j = 0; j < pat_len; ++j)
        {
            if (aml[i + j] != pat[j])
                break;
        }
        if (j == pat_len)
            return 1;
    }

    return 0;
}

static uint8_t dep_refs_name(const uint8_t *aml, uint32_t body_start, uint32_t body_end, const char *name)
{
    int off;

    if (!aml || !name)
        return 0;

    off = find_name_object(aml, body_start, body_end, "_DEP");
    if (off < 0)
        return 0;

    return body_contains_bytes(aml, (uint32_t)off, body_end, name, 4);
}

static uint8_t method_argcount(const uint8_t *aml, uint32_t body_start, uint32_t body_end, const char *name)
{
    int off;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;
    uint32_t flags_off;

    if (!aml || !name)
        return 0xFFu;

    off = find_method_object(aml, body_start, body_end, name);
    if (off < 0)
        return 0xFFu;

    if (aml_read_pkglen(aml, body_end, (uint32_t)off + 1, &pkglen, &pkglen_bytes) != 0)
        return 0xFFu;

    name_off = (uint32_t)off + 1 + pkglen_bytes;
    flags_off = name_off + 4;

    if (flags_off >= body_end)
        return 0xFFu;

    return (uint8_t)(aml[flags_off] & 0x07u);
}

static int is_hidi2cish_id(const char *s)
{
    if (!s || !s[0])
        return 0;

    if (memeq_n(s, "PNP0C50", 7))
        return 1;
    if (memeq_n(s, "QTEC0001", 8))
        return 1;
    if (memeq_n(s, "QTEC0002", 8))
        return 1;
    if (memeq_n(s, "QTEC", 4))
        return 1;

    return 0;
}

static int dsm_is_trusted(const hidi2c_acpi_summary_t *s)
{
    if (!s)
        return 0;

    /*
      Require actual UUID evidence, not just func/rev shape and a guessed
      small integer. TCPD currently ACKs on I2C but returns all-zero payloads,
      so we need to be stricter about blessing a descriptor register.
    */
    return s->dsm_hid_desc_ok &&
           s->dsm_has_func1_compare &&
           s->dsm_has_rev1_compare &&
           s->dsm_has_arg0_ref &&
           s->dsm_has_arg1_ref &&
           s->dsm_has_arg2_ref &&
           s->dsm_has_uuid_buffer;
}

static void print_flag(const char *name, uint8_t v)
{
    terminal_print(name);
    terminal_print(v ? ":Y " : ":N ");
}

static int aml_parse_const_byte(const uint8_t *aml, uint32_t end, uint32_t off,
                                uint8_t *value_out, uint32_t *next_off_out)
{
    if (!aml || !value_out || !next_off_out || off >= end)
        return -1;

    if (aml[off] == 0x0A)
    {
        if (off + 1 >= end)
            return -1;
        *value_out = aml[off + 1];
        *next_off_out = off + 2;
        return 0;
    }

    if (aml[off] <= 0x7F)
    {
        *value_out = aml[off];
        *next_off_out = off + 1;
        return 0;
    }

    if (aml[off] == 0x00)
    {
        *value_out = 0;
        *next_off_out = off + 1;
        return 0;
    }

    if (aml[off] == 0x01)
    {
        *value_out = 1;
        *next_off_out = off + 1;
        return 0;
    }

    return -1;
}

static int extract_name_buffer(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                               const char *name, const uint8_t **buf_out, uint16_t *len_out)
{
    int off;
    uint32_t p;
    uint32_t next;
    uint8_t declared_len;
    uint32_t buf_pkglen;
    uint32_t buf_pkglen_bytes;
    uint32_t data_off;

    if (!aml || !buf_out || !len_out)
        return -1;

    *buf_out = 0;
    *len_out = 0;

    off = find_name_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    p = (uint32_t)off + 5;
    if (p >= body_end)
        return -1;

    if (aml[p] != 0x11)
        return -1;

    p++;

    if (aml_read_pkglen(aml, body_end, p, &buf_pkglen, &buf_pkglen_bytes) != 0)
        return -1;

    p += buf_pkglen_bytes;

    if (aml_parse_const_byte(aml, body_end, p, &declared_len, &next) != 0)
        return -1;

    p = next;
    data_off = p;

    if (data_off + declared_len > body_end)
        return -1;

    *buf_out = aml + data_off;
    *len_out = declared_len;
    return 0;
}

static int extract_named_buffer_by_nameseg(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                           const char *objname, const uint8_t **buf_out, uint16_t *len_out)
{
    int off;
    uint32_t p;
    uint32_t next;
    uint8_t declared_len;
    uint32_t buf_pkglen, buf_pkglen_bytes;
    uint32_t data_off;

    if (!aml || !objname || !buf_out || !len_out)
        return -1;

    *buf_out = 0;
    *len_out = 0;

    off = find_name_object(aml, body_start, body_end, objname);
    if (off < 0)
        return -1;

    p = (uint32_t)off + 5;
    if (p >= body_end)
        return -1;

    if (aml[p] != 0x11) /* BufferOp */
        return -1;

    p++;

    if (aml_read_pkglen(aml, body_end, p, &buf_pkglen, &buf_pkglen_bytes) != 0)
        return -1;

    p += buf_pkglen_bytes;

    if (aml_parse_const_byte(aml, body_end, p, &declared_len, &next) != 0)
        return -1;

    data_off = next;
    if (data_off + declared_len > body_end)
        return -1;

    *buf_out = aml + data_off;
    *len_out = declared_len;
    return 0;
}

static int extract_method_return_buffer(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                        const char *name, const uint8_t **buf_out, uint16_t *len_out)
{
    int off;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;
    uint32_t flags_off;
    uint32_t meth_body;
    uint32_t meth_end;
    uint32_t i;

    if (!aml || !buf_out || !len_out)
        return -1;

    *buf_out = 0;
    *len_out = 0;

    off = find_method_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    if (aml_read_pkglen(aml, body_end, (uint32_t)off + 1, &pkglen, &pkglen_bytes) != 0)
        return -1;

    name_off = (uint32_t)off + 1 + pkglen_bytes;
    flags_off = name_off + 4;
    meth_body = flags_off + 1;
    meth_end = (uint32_t)off + 1 + pkglen_bytes + pkglen;

    if (meth_end > body_end)
        meth_end = body_end;
    if (meth_body >= meth_end)
        return -1;

    /*
      Look for:
        A4            ReturnOp
        11            BufferOp
        <PkgLen>
        <ByteConst>
        <buffer bytes...>
    */
    for (i = meth_body; i + 4 < meth_end; ++i)
    {
        uint32_t p = i + 1;
        uint32_t buf_pkglen, buf_pkglen_bytes;
        uint8_t declared_len;
        uint32_t next;
        uint32_t data_off;

        if (aml[i] != 0xA4) /* ReturnOp */
            continue;
        if (aml[p] != 0x11) /* BufferOp */
            continue;

        p++;

        if (aml_read_pkglen(aml, meth_end, p, &buf_pkglen, &buf_pkglen_bytes) != 0)
            continue;

        p += buf_pkglen_bytes;

        if (aml_parse_const_byte(aml, meth_end, p, &declared_len, &next) != 0)
            continue;

        data_off = next;

        if (data_off + declared_len > meth_end)
            continue;

        *buf_out = aml + data_off;
        *len_out = declared_len;
        return 0;
    }

    return -1;
}

static int extract_method_body_bounds(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                      const char *name, uint32_t *meth_body_out, uint32_t *meth_end_out)
{
    int off;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;
    uint32_t flags_off;
    uint32_t meth_body;
    uint32_t meth_end;

    if (!aml || !name || !meth_body_out || !meth_end_out)
        return -1;

    off = find_method_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    if (aml_read_pkglen(aml, body_end, (uint32_t)off + 1, &pkglen, &pkglen_bytes) != 0)
        return -1;

    name_off = (uint32_t)off + 1 + pkglen_bytes;
    flags_off = name_off + 4;
    meth_body = flags_off + 1;
    meth_end = (uint32_t)off + 1 + pkglen_bytes + pkglen;

    if (meth_end > body_end)
        meth_end = body_end;
    if (meth_body >= meth_end)
        return -1;

    *meth_body_out = meth_body;
    *meth_end_out = meth_end;
    return 0;
}

static void print_method_body_hex(const char *tag,
                                  const uint8_t *aml,
                                  uint32_t meth_body,
                                  uint32_t meth_end)
{
    uint32_t i;
    uint32_t n;

    if (!tag || !aml || meth_end <= meth_body)
        return;

    terminal_print(tag);
    terminal_print(" len:");
    terminal_print_hex32(meth_end - meth_body);
    terminal_print(" body:");

    n = meth_end - meth_body;

    for (i = 0; i < n; ++i)
    {
        terminal_print(" ");
        terminal_print_hex8(aml[meth_body + i]);
    }

    if ((meth_end - meth_body) > n)
        terminal_print(" ...");

    terminal_print("\n");
}

static uint8_t aml_u32_eq_le(const uint8_t *p, uint32_t v)
{
    return rd32le(p) == v;
}

static uint8_t body_mentions_tcpd_clues(const uint8_t *aml, uint32_t body_start, uint32_t body_end)
{
    if (!aml || body_end <= body_start)
        return 0;

    if (body_contains_bytes(aml, body_start, body_end, "TCPD", 4))
        return 1;
    if (body_contains_bytes(aml, body_start, body_end, "GIO0", 4))
        return 1;
    if (body_contains_bytes(aml, body_start, body_end, "LID0", 4))
        return 1;
    if (body_contains_bytes(aml, body_start, body_end, "LIDS", 4))
        return 1;
    if (body_contains_bytes(aml, body_start, body_end, "LIDR", 4))
        return 1;
    if (body_contains_bytes(aml, body_start, body_end, "I2C1", 4))
        return 1;

    /* little-endian DWORD constants that might appear in AML */
    for (uint32_t i = body_start; i + 4 <= body_end; ++i)
    {
        if (aml_u32_eq_le(aml + i, 0x0000002Cu)) /* TCPD address */
            return 1;
        if (aml_u32_eq_le(aml + i, 0x00000020u)) /* trusted desc reg */
            return 1;
    }

    return 0;
}

static uint8_t body_looks_like_tcpd_power_method(const uint8_t *aml,
                                                 uint32_t body_start,
                                                 uint32_t body_end)
{
    uint8_t has_i2c = 0;
    uint8_t has_hid = 0;
    uint8_t has_gpio = 0;
    uint8_t has_tcpdish = 0;
    uint8_t has_lid = 0;
    uint32_t i;

    if (!aml || body_end <= body_start)
        return 0;

    for (i = body_start; i + 3u < body_end; ++i)
    {
        /* Positive signals */
        if (!has_i2c &&
            aml[i] == 'I' && aml[i + 1] == '2' && aml[i + 2] == 'C')
            has_i2c = 1;

        if (!has_hid &&
            aml[i] == 'P' && aml[i + 1] == 'N' && aml[i + 2] == 'P' &&
            aml[i + 3] == '0')
            has_hid = 1;

        if (!has_gpio &&
            aml[i] == 'G' && aml[i + 1] == 'P' && aml[i + 2] == 'I' &&
            aml[i + 3] == 'O')
            has_gpio = 1;

        if (!has_tcpdish)
        {
            if ((aml[i] == 'T' && aml[i + 1] == 'C' && aml[i + 2] == 'P' && aml[i + 3] == 'D') ||
                (aml[i] == 'Q' && aml[i + 1] == 'T' && aml[i + 2] == 'E' && aml[i + 3] == 'C') ||
                (aml[i] == 'P' && aml[i + 1] == 'N' && aml[i + 2] == 'P' && aml[i + 3] == '0') ||
                (aml[i] == 'I' && aml[i + 1] == '2' && aml[i + 2] == 'C' && aml[i + 3] == '6'))
                has_tcpdish = 1;
        }

        /* Strong negative signals: lid plumbing */
        if (!has_lid)
        {
            if ((aml[i] == 'L' && aml[i + 1] == 'I' && aml[i + 2] == 'D' && aml[i + 3] == '0') ||
                (aml[i] == 'L' && aml[i + 1] == 'I' && aml[i + 2] == 'D' && aml[i + 3] == 'B') ||
                (aml[i] == 'L' && aml[i + 1] == 'I' && aml[i + 2] == 'D' && aml[i + 3] == 'R') ||
                (aml[i] == 'L' && aml[i + 1] == 'I' && aml[i + 2] == 'D' && aml[i + 3] == 'S'))
                has_lid = 1;
        }
    }

    /* Reject obvious lid methods outright */
    if (has_lid)
        return 0;

    /* Require something genuinely device-ish, not just generic GPIO */
    if (has_tcpdish)
        return 1;

    /* Fallback: I2C + GPIO is plausible, but GPIO alone is not */
    if (has_i2c && has_gpio)
        return 1;

    /* HID + GPIO is also plausible */
    if (has_hid && has_gpio)
        return 1;

    return 0;
}

static void export_method_body(const uint8_t *aml,
                               uint32_t scope_body_start,
                               uint32_t scope_body_end,
                               const char *meth_name,
                               uint8_t *valid_out,
                               uint16_t *len_out,
                               uint8_t *body_out,
                               uint32_t body_cap,
                               const char *tag)
{
    uint32_t meth_body = 0;
    uint32_t meth_end = 0;
    uint32_t len = 0;
    uint32_t i;

    if (!aml || !meth_name || !valid_out || !len_out || !body_out || body_cap == 0)
        return;

    if (scope_body_end <= scope_body_start)
        return;

    if (extract_method_body_bounds(aml,
                                   scope_body_start,
                                   scope_body_end,
                                   meth_name,
                                   &meth_body,
                                   &meth_end) != 0)
        return;

    if (meth_end <= meth_body)
        return;

    len = meth_end - meth_body;
    if (len > body_cap)
        len = body_cap;

    for (i = 0; i < len; ++i)
        body_out[i] = aml[meth_body + i];

    *len_out = (uint16_t)len;
    *valid_out = 1u;

    terminal_print(tag);
    terminal_print(" len:");
    terminal_print_hex32(len);
    terminal_print("\n");
}

static void maybe_export_tcpd_methods(const uint8_t *aml,
                                      const hidi2c_acpi_summary_t *s)
{
    uint32_t chosen_start = 0;
    uint32_t chosen_end = 0;
    uint8_t chosen_has_ps0 = 0;
    uint8_t chosen_has_ps3 = 0;
    uint8_t chosen_has_sta = 0;
    uint8_t chosen_has_ini = 0;
    const char *chosen_tag = "ACPI TCPD scope";

    if (!aml || !s)
        return;

    /*
      Pick the scope that actually looks like a TCPD power scope.
      Prefer ggparent, then grandparent, then parent.
    */
    if (s->ggparent_body_end > s->ggparent_body_start &&
        body_looks_like_tcpd_power_method(aml, s->ggparent_body_start, s->ggparent_body_end))
    {
        chosen_start = s->ggparent_body_start;
        chosen_end = s->ggparent_body_end;
        chosen_has_ps0 = s->ggparent_has_ps0;
        chosen_has_ps3 = s->ggparent_has_ps3;
        chosen_has_sta = s->ggparent_has_sta;
        chosen_has_ini = s->ggparent_has_ini;
        chosen_tag = "ACPI TCPD scope: ggparent";
    }
    else if (s->grandparent_body_end > s->grandparent_body_start &&
             body_looks_like_tcpd_power_method(aml, s->grandparent_body_start, s->grandparent_body_end))
    {
        chosen_start = s->grandparent_body_start;
        chosen_end = s->grandparent_body_end;
        chosen_has_ps0 = s->grandparent_has_ps0;
        chosen_has_ps3 = s->grandparent_has_ps3;
        chosen_has_sta = s->grandparent_has_sta;
        chosen_has_ini = s->grandparent_has_ini;
        chosen_tag = "ACPI TCPD scope: grandparent";
    }
    else if (s->parent_body_end > s->parent_body_start &&
             body_looks_like_tcpd_power_method(aml, s->parent_body_start, s->parent_body_end))
    {
        chosen_start = s->parent_body_start;
        chosen_end = s->parent_body_end;
        chosen_has_ps0 = s->parent_has_ps0;
        chosen_has_ps3 = s->parent_has_ps3;
        chosen_has_sta = s->parent_has_sta;
        chosen_has_ini = s->parent_has_ini;
        chosen_tag = "ACPI TCPD scope: parent";
    }
    else
    {
        terminal_print("ACPI TCPD scope: no trusted power scope found\n");
        return;
    }

    terminal_print(chosen_tag);
    terminal_print("\n");

    if (chosen_has_ps0)
    {
        export_method_body(aml,
                           chosen_start,
                           chosen_end,
                           "_PS0",
                           &g_hidi2c_regs.tcpd_ps0_valid,
                           &g_hidi2c_regs.tcpd_ps0_len,
                           g_hidi2c_regs.tcpd_ps0_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI TCPD _PS0 bytes captured");
    }

    if (chosen_has_ps3)
    {
        export_method_body(aml,
                           chosen_start,
                           chosen_end,
                           "_PS3",
                           &g_hidi2c_regs.tcpd_ps3_valid,
                           &g_hidi2c_regs.tcpd_ps3_len,
                           g_hidi2c_regs.tcpd_ps3_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI TCPD _PS3 bytes captured");
    }

    if (chosen_has_sta)
    {
        export_method_body(aml,
                           chosen_start,
                           chosen_end,
                           "_STA",
                           &g_hidi2c_regs.tcpd_sta_valid,
                           &g_hidi2c_regs.tcpd_sta_len,
                           g_hidi2c_regs.tcpd_sta_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI TCPD _STA bytes captured");
    }

    if (chosen_has_ini)
    {
        export_method_body(aml,
                           chosen_start,
                           chosen_end,
                           "_INI",
                           &g_hidi2c_regs.tcpd_ini_valid,
                           &g_hidi2c_regs.tcpd_ini_len,
                           g_hidi2c_regs.tcpd_ini_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI TCPD _INI bytes captured");
    }
}

static void maybe_export_gio0_reg(const uint8_t *aml, uint32_t aml_len)
{
    uint32_t i;

    if (!aml || aml_len < 8u)
        return;

    for (i = 0; i + 6u < aml_len; ++i)
    {
        uint32_t name_off = 0;
        uint32_t body_start = 0;
        uint32_t body_end = 0;
        uint8_t ok = 0;
        char nm[5];

        if (aml[i] == 0x5Bu && aml[i + 1u] == 0x82u) /* DeviceOp */
            ok = (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);
        else if (aml[i] == 0x10u) /* ScopeOp */
            ok = (get_scope_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);

        if (!ok)
            continue;

        copy_sig4(nm, aml + name_off);
        if (!memeq_n(nm, "GIO0", 4))
            continue;

        export_method_body(aml,
                           body_start,
                           body_end,
                           "_REG",
                           &g_hidi2c_regs.tcpd_gio0_reg_valid,
                           &g_hidi2c_regs.tcpd_gio0_reg_len,
                           g_hidi2c_regs.tcpd_gio0_reg_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI GIO0 _REG bytes captured");
        return;
    }
}

static void maybe_export_tcpd_dsm(const uint8_t *aml,
                                  const hidi2c_acpi_summary_t *s)
{
    uint32_t chosen_start = 0;
    uint32_t chosen_end = 0;
    const char *tag = "ACPI TCPD _DSM bytes captured";

    if (!aml || !s)
        return;

    terminal_set_loud();

    /*
      Match the same scope-selection logic used by maybe_export_tcpd_methods().
      We do NOT want the nearest random ancestor with a _DSM; we want the
      ancestor that actually looks like the TCPD power / bring-up scope.
    */
    if (s->ggparent_body_end > s->ggparent_body_start &&
        body_looks_like_tcpd_power_method(aml, s->ggparent_body_start, s->ggparent_body_end) &&
        find_method_object(aml, s->ggparent_body_start, s->ggparent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->ggparent_body_start;
        chosen_end = s->ggparent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (ggparent trusted)";
    }
    else if (s->grandparent_body_end > s->grandparent_body_start &&
             body_looks_like_tcpd_power_method(aml, s->grandparent_body_start, s->grandparent_body_end) &&
             find_method_object(aml, s->grandparent_body_start, s->grandparent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->grandparent_body_start;
        chosen_end = s->grandparent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (grandparent trusted)";
    }
    else if (s->parent_body_end > s->parent_body_start &&
             body_looks_like_tcpd_power_method(aml, s->parent_body_start, s->parent_body_end) &&
             find_method_object(aml, s->parent_body_start, s->parent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->parent_body_start;
        chosen_end = s->parent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (parent trusted)";
    }
    else if (s->ggparent_body_end > s->ggparent_body_start &&
             find_method_object(aml, s->ggparent_body_start, s->ggparent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->ggparent_body_start;
        chosen_end = s->ggparent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (ggparent fallback)";
    }
    else if (s->grandparent_body_end > s->grandparent_body_start &&
             find_method_object(aml, s->grandparent_body_start, s->grandparent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->grandparent_body_start;
        chosen_end = s->grandparent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (grandparent fallback)";
    }
    else if (s->parent_body_end > s->parent_body_start &&
             find_method_object(aml, s->parent_body_start, s->parent_body_end, "_DSM") >= 0)
    {
        chosen_start = s->parent_body_start;
        chosen_end = s->parent_body_end;
        tag = "ACPI TCPD _DSM bytes captured (parent fallback)";
    }
    else
    {
        terminal_print("ACPI TCPD _DSM not found in parent/grandparent/ggparent\n");
        terminal_set_quiet();
        return;
    }

    export_method_body(aml,
                       chosen_start,
                       chosen_end,
                       "_DSM",
                       &g_hidi2c_regs.tcpd_dsm_valid,
                       &g_hidi2c_regs.tcpd_dsm_len,
                       g_hidi2c_regs.tcpd_dsm_body,
                       HIDI2C_ACPI_MAX_METHOD_BODY,
                       tag);

    terminal_set_quiet();
}

static void maybe_export_gio0_dsm(const uint8_t *aml, uint32_t aml_len)
{
    uint32_t i;

    if (!aml || aml_len < 8u)
        return;

    for (i = 0; i + 6u < aml_len; ++i)
    {
        uint32_t name_off = 0;
        uint32_t body_start = 0;
        uint32_t body_end = 0;
        uint8_t ok = 0;
        char nm[5];

        if (aml[i] == 0x5Bu && aml[i + 1u] == 0x82u) /* DeviceOp */
            ok = (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);
        else if (aml[i] == 0x10u) /* ScopeOp */
            ok = (get_scope_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);

        if (!ok)
            continue;

        copy_sig4(nm, aml + name_off);
        if (!memeq_n(nm, "GIO0", 4))
            continue;

        export_method_body(aml,
                           body_start,
                           body_end,
                           "_DSM",
                           &g_hidi2c_regs.tcpd_gio0_dsm_valid,
                           &g_hidi2c_regs.tcpd_gio0_dsm_len,
                           g_hidi2c_regs.tcpd_gio0_dsm_body,
                           HIDI2C_ACPI_MAX_METHOD_BODY,
                           "ACPI GIO0 _DSM bytes captured");
        return;
    }
}

static void print_method_header(const char *scope_label,
                                const char *meth_name,
                                uint32_t obj_off,
                                uint32_t meth_body,
                                uint32_t meth_end)
{
    terminal_print(scope_label);
    terminal_print(".");
    terminal_print(meth_name);
    terminal_print(" obj:");
    terminal_print_hex32(obj_off);
    terminal_print(" len:");
    terminal_print_hex32(meth_end - meth_body);
    terminal_print("\n");
}

static void dump_method_if_present(const uint8_t *aml,
                                   uint32_t body_start,
                                   uint32_t body_end,
                                   uint32_t obj_off,
                                   const char *scope_label,
                                   const char *meth_name)
{
    uint32_t meth_body = 0, meth_end = 0;

    if (extract_method_body_bounds(aml, body_start, body_end, meth_name, &meth_body, &meth_end) != 0)
        return;

    if ((meth_end - meth_body) <= 1u)
        return;

    print_method_header(scope_label, meth_name, obj_off, meth_body, meth_end);
    print_method_body_hex("  body", aml, meth_body, meth_end);
}

static void dump_scope_candidate_methods(const uint8_t *aml,
                                         uint32_t body_start,
                                         uint32_t body_end,
                                         uint32_t obj_off,
                                         const char *scope_label)
{
    if (!aml || body_end <= body_start)
        return;

    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_PS0");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_PS3");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_ON_");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_OFF");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_RST");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_STA");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_INI");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_REG");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_EVT");
    dump_method_if_present(aml, body_start, body_end, obj_off, scope_label, "_DSM");
}

static void dump_all_methods_in_scope(const uint8_t *aml,
                                      uint32_t body_start,
                                      uint32_t body_end,
                                      const char *scope_label)
{
    uint32_t i;

    if (!aml || !scope_label || body_end <= body_start + 6u)
        return;

    terminal_print(" ");
    terminal_print(scope_label);
    terminal_print(" methods:\n");

    for (i = body_start; i < body_end; ++i)
    {
        uint32_t pkglen, pkglen_bytes;
        uint32_t name_off;
        char nm[5];

        if (aml[i] != 0x14) /* MethodOp */
            continue;

        if (aml_read_pkglen(aml, body_end, i + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = i + 1u + pkglen_bytes;
        if (name_off + 4u > body_end)
            continue;

        copy_sig4_printable(nm, aml + name_off);

        terminal_print("  ");
        terminal_print(nm);
        terminal_print(" obj:");
        terminal_print_hex32(i);
        terminal_print("\n");
    }
}

static void dump_name_object_value(const uint8_t *aml,
                                   uint32_t body_start,
                                   uint32_t body_end,
                                   const char *scope_label,
                                   const char *name)
{
    int off;
    uint32_t p;

    if (!aml || !scope_label || !name)
        return;

    off = find_name_object(aml, body_start, body_end, name);
    if (off < 0)
        return;

    terminal_print(scope_label);
    terminal_print(".");
    terminal_print(name);
    terminal_print(" obj:");
    terminal_print_hex32((uint32_t)off);
    terminal_print("\n");

    p = (uint32_t)off + 5u;
    if (p >= body_end)
        return;

    terminal_print("  valop:");
    terminal_print_hex8(aml[p]);
    terminal_print("\n");

    if (aml[p] == 0x00) /* ZeroOp */
    {
        terminal_print("  value:");
        terminal_print_hex32(0u);
        terminal_print("\n");
    }
    else if (aml[p] == 0x01) /* OneOp */
    {
        terminal_print("  value:");
        terminal_print_hex32(1u);
        terminal_print("\n");
    }
    else if (aml[p] == 0x0A && p + 1u < body_end) /* ByteConst */
    {
        terminal_print("  value:");
        terminal_print_hex32((uint32_t)aml[p + 1u]);
        terminal_print("\n");
    }
    else if (aml[p] == 0x0B && p + 2u < body_end) /* WordConst */
    {
        terminal_print("  value:");
        terminal_print_hex32((uint32_t)rd16le(aml + p + 1u));
        terminal_print("\n");
    }
    else if (aml[p] == 0x0C && p + 4u < body_end) /* DWordConst */
    {
        terminal_print("  value:");
        terminal_print_hex32(rd32le(aml + p + 1u));
        terminal_print("\n");
    }
    else if (aml[p] == 0x0D) /* String */
    {
        uint32_t i = p + 1u;
        terminal_print("  str:");
        while (i < body_end && aml[i] != 0u)
        {
            char c = (char)aml[i++];
            if ((uint8_t)c < 32u || (uint8_t)c > 126u)
                break;

            char buf[2];   // 1 char + null terminator
            buf[0] = c;    // Your character
            buf[1] = '\0'; // Null terminator
            terminal_print_inline(buf);
        }
        terminal_print("\n");
    }
    else if (aml[p] == 0x11) /* BufferOp */
    {
        const uint8_t *buf = 0;
        uint16_t len = 0;
        uint32_t i, n;

        if (extract_name_buffer(aml, body_start, body_end, name, &buf, &len) == 0 && buf && len)
        {
            terminal_print("  buf len:");
            terminal_print_hex32(len);
            terminal_print(" data:");
            n = (len < 16u) ? len : 16u;
            for (i = 0; i < n; ++i)
            {
                terminal_print(" ");
                terminal_print_hex8(buf[i]);
            }
            terminal_print("\n");
        }
    }
}

static void dump_all_name_objects_in_scope(const uint8_t *aml,
                                           uint32_t body_start,
                                           uint32_t body_end,
                                           const char *scope_label)
{
    uint32_t i;

    if (!aml || !scope_label || body_end <= body_start + 5u)
        return;

    terminal_print(" ");
    terminal_print(scope_label);
    terminal_print(" names:\n");

    for (i = body_start; i + 5u <= body_end; ++i)
    {
        char nm[5];

        if (aml[i] != 0x08) /* NameOp */
            continue;

        copy_sig4_printable(nm, aml + i + 1u);

        terminal_print("  ");
        terminal_print(nm);
        terminal_print("\n");
    }
}

static void dump_gio0_scope_candidates(const uint8_t *aml, uint32_t aml_len)
{
    uint32_t i;

    if (!aml || aml_len < 8u)
        return;

    terminal_print(" GIO0 scope scan\n");

    for (i = 0; i + 6u < aml_len; ++i)
    {
        uint32_t name_off = 0;
        uint32_t body_start = 0;
        uint32_t body_end = 0;
        uint8_t ok = 0;
        char nm[5];

        if (aml[i] == 0x5Bu && aml[i + 1u] == 0x82u) /* DeviceOp */
            ok = (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);
        else if (aml[i] == 0x10u) /* ScopeOp */
            ok = (get_scope_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);

        if (!ok)
            continue;

        copy_sig4(nm, aml + name_off);

        if (!memeq_n(nm, "GIO0", 4))
            continue;

        terminal_print("GIO0 obj:");
        terminal_print_hex32(i);
        terminal_print(" body:");
        terminal_print_hex32(body_start);
        terminal_print("..");
        terminal_print_hex32(body_end);
        terminal_print("\n");

        dump_scope_candidate_methods(aml, body_start, body_end, i, "GIO0");
        dump_all_name_objects_in_scope(aml, body_start, body_end, "GIO0");
        dump_all_methods_in_scope(aml, body_start, body_end, "GIO0");

        dump_name_object_value(aml, body_start, body_end, "GIO0", "GABL");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "LIDR");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "LIDS");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "LIDB");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "_STA");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "_INI");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "TPAD");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "TCRS");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "WAKE");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "RSET");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "PDCE");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "PDCC");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "PDCM");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "PDCV");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "GPIV");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "GPIC");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "GPIW");
        dump_name_object_value(aml, body_start, body_end, "GIO0", "GPIB");

        /*
          Extra clue scan: show whether this scope mentions TCPD-ish things.
        */
        if (body_contains_bytes(aml, body_start, body_end, "TCPD", 4) ||
            body_contains_bytes(aml, body_start, body_end, "GABL", 4) ||
            body_contains_bytes(aml, body_start, body_end, "LIDR", 4) ||
            body_contains_bytes(aml, body_start, body_end, "LID0", 4))
        {
            terminal_print(" GIO0 refs: ");
            if (body_contains_bytes(aml, body_start, body_end, "TCPD", 4))
                terminal_print("TCPD ");
            if (body_contains_bytes(aml, body_start, body_end, "GABL", 4))
                terminal_print("GABL ");
            if (body_contains_bytes(aml, body_start, body_end, "LIDR", 4))
                terminal_print("LIDR ");
            if (body_contains_bytes(aml, body_start, body_end, "LID0", 4))
                terminal_print("LID0 ");
            terminal_print("\n");
        }
    }
}

static int aml_parse_const_int(const uint8_t *aml, uint32_t end, uint32_t off,
                               uint32_t *value_out, uint32_t *next_off_out)
{
    if (!aml || !value_out || !next_off_out || off >= end)
        return -1;

    /* ZeroOp / OneOp / OnesOp */
    if (aml[off] == 0x00)
    {
        *value_out = 0u;
        *next_off_out = off + 1;
        return 0;
    }

    if (aml[off] == 0x01)
    {
        *value_out = 1u;
        *next_off_out = off + 1;
        return 0;
    }

    if (aml[off] == 0xFF)
    {
        *value_out = 0xFFFFFFFFu;
        *next_off_out = off + 1;
        return 0;
    }

    /* ByteConst */
    if (aml[off] == 0x0A)
    {
        if (off + 1 >= end)
            return -1;
        *value_out = aml[off + 1];
        *next_off_out = off + 2;
        return 0;
    }

    /* WordConst */
    if (aml[off] == 0x0B)
    {
        if (off + 2 >= end)
            return -1;
        *value_out = (uint32_t)rd16le(aml + off + 1);
        *next_off_out = off + 3;
        return 0;
    }

    /* DWordConst */
    if (aml[off] == 0x0C)
    {
        if (off + 4 >= end)
            return -1;
        *value_out = rd32le(aml + off + 1);
        *next_off_out = off + 5;
        return 0;
    }

    return -1;
}

static uint8_t body_contains_ascii_nocase(const uint8_t *aml, uint32_t start, uint32_t end,
                                          const char *pat, uint32_t pat_len)
{
    uint32_t i, j;

    if (!aml || !pat || pat_len == 0 || end < start + pat_len)
        return 0;

    for (i = start; i + pat_len <= end; ++i)
    {
        for (j = 0; j < pat_len; ++j)
        {
            uint8_t a = aml[i + j];
            uint8_t b = (uint8_t)pat[j];

            if (a >= 'a' && a <= 'z')
                a = (uint8_t)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z')
                b = (uint8_t)(b - 'a' + 'A');

            if (a != b)
                break;
        }

        if (j == pat_len)
            return 1;
    }

    return 0;
}

static void dsm_collect_return_ints(hidi2c_acpi_summary_t *s,
                                    const uint8_t *aml, uint32_t meth_body, uint32_t meth_end)
{
    uint32_t i;

    if (!s || !aml)
        return;

    for (i = meth_body; i < meth_end && s->dsm_ret_count < 12u; ++i)
    {
        uint32_t v, next;

        if (aml[i] != 0xA4) /* ReturnOp */
            continue;

        if (aml_parse_const_int(aml, meth_end, i + 1, &v, &next) == 0)
        {
            uint32_t k;
            uint8_t dup = 0;

            for (k = 0; k < s->dsm_ret_count; ++k)
            {
                if (s->dsm_ret[k] == v)
                {
                    dup = 1;
                    break;
                }
            }

            if (!dup)
                s->dsm_ret[s->dsm_ret_count++] = v;
        }
    }
}

static void dsm_mark_simple_hidi2c_patterns(hidi2c_acpi_summary_t *s,
                                            const uint8_t *aml, uint32_t meth_body, uint32_t meth_end)
{
    uint32_t i;

    if (!s || !aml)
        return;

    for (i = meth_body; i + 4 < meth_end; ++i)
    {
        /*
          AML can encode either operand order:

          LEqual(Arg2, One) => 93 6A 01
          LEqual(One, Arg2) => 93 01 6A

          LEqual(Arg1, One) => 93 69 01
          LEqual(One, Arg1) => 93 01 69
        */
        if (aml[i] == 0x93)
        {
            if ((aml[i + 1] == 0x6A && aml[i + 2] == 0x01) ||
                (aml[i + 1] == 0x01 && aml[i + 2] == 0x6A))
                s->dsm_has_func1_compare = 1;

            if ((aml[i + 1] == 0x69 && aml[i + 2] == 0x01) ||
                (aml[i + 1] == 0x01 && aml[i + 2] == 0x69))
                s->dsm_has_rev1_compare = 1;
        }
    }
}

static void dsm_mark_raw_arg_usage(hidi2c_acpi_summary_t *s,
                                   const uint8_t *aml, uint32_t meth_body, uint32_t meth_end)
{
    uint32_t i;

    if (!s || !aml)
        return;

    for (i = meth_body; i < meth_end; ++i)
    {
        if (aml[i] == 0x68)
            s->dsm_has_arg0_ref = 1; /* Arg0Op */
        if (aml[i] == 0x69)
            s->dsm_has_arg1_ref = 1; /* Arg1Op */
        if (aml[i] == 0x6A)
            s->dsm_has_arg2_ref = 1; /* Arg2Op */
    }
}

/*
  UUID ToUUID("3CDFF6F7-4267-4555-AD05-B30A3D8938DE")
  often becomes raw bytes in AML, not ASCII.
  This is a best-effort byte-pattern check for the HID-I2C GUID.
*/
static void dsm_mark_hidi2c_guid_raw(hidi2c_acpi_summary_t *s,
                                     const uint8_t *aml, uint32_t meth_body, uint32_t meth_end)
{
    static const uint8_t hid_i2c_guid_raw[16] = {
        0xF7, 0xF6, 0xDF, 0x3C,
        0x67, 0x42,
        0x55, 0x45,
        0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE
    };

    static const uint8_t hid_i2c_guid_ascii[36] = {
        '3','C','D','F','F','6','F','7','-',
        '4','2','6','7','-',
        '4','5','5','5','-',
        'A','D','0','5','-',
        'B','3','0','A','3','D','8','9','3','8','D','E'
    };

    if (!s || !aml)
        return;

    /*
      AML often stores ToUUID(...) as raw 16-byte buffer data, not ASCII.
      Treat either representation as UUID evidence.
    */
    if (body_contains_bytes_exact(aml, meth_body, meth_end,
                                  hid_i2c_guid_raw, sizeof(hid_i2c_guid_raw)))
    {
        s->dsm_has_uuid_buffer = 1;
        s->dsm_guid_ascii_seen = 1; /* "seen" in a broad sense for logging */
        return;
    }

    if (body_contains_ascii_nocase(aml, meth_body, meth_end,
                                   (const char *)hid_i2c_guid_ascii, 36))
    {
        s->dsm_has_uuid_buffer = 1;
        s->dsm_guid_ascii_seen = 1;
    }
}

static void dsm_decode_hid_desc_best_effort(hidi2c_acpi_summary_t *s,
                                            const uint8_t *aml, uint32_t body_start, uint32_t body_end)
{
    uint32_t meth_body = 0, meth_end = 0;
    uint32_t i;

    if (!s || !aml || !s->has_dsm)
        return;

    if (extract_method_body_bounds(aml, body_start, body_end, "_DSM", &meth_body, &meth_end) != 0)
        return;

    s->dsm_guid_ascii_seen =
        body_contains_ascii_nocase(aml, meth_body, meth_end,
                                   "3CDFF6F7-4267-4555-AD05-B30A3D8938DE", 36);

    dsm_collect_return_ints(s, aml, meth_body, meth_end);

    dsm_mark_raw_arg_usage(s, aml, meth_body, meth_end);
    dsm_mark_hidi2c_guid_raw(s, aml, meth_body, meth_end);

    dsm_mark_simple_hidi2c_patterns(s, aml, meth_body, meth_end);

    /*
      Best-effort choice:
      - prefer the first small non-zero integer that is not 0xFFFFFFFF
      - HID descriptor register is usually a small offset
    */
    for (i = 0; i < s->dsm_ret_count; ++i)
    {
        uint32_t v = s->dsm_ret[i];
        if (v != 0u && v != 0xFFFFFFFFu && v <= 0xFFFFu)
        {
            s->dsm_hid_desc_ok = 1;
            s->dsm_hid_desc_addr = v;
            return;
        }
    }

    /*
      Fallback: if the only thing we saw is 0 or 1, still surface it.
    */
    if (s->dsm_ret_count == 1 && s->dsm_ret[0] <= 0xFFFFu)
    {
        s->dsm_hid_desc_ok = 1;
        s->dsm_hid_desc_addr = s->dsm_ret[0];
    }
}

extern int i2c1_bus_init(void);
extern int i2c1_bus_write_read(uint8_t addr7, const void *tx, uint32_t tx_len, void *rx, uint32_t rx_len);
extern int i2c1_bus_write_read_combined(uint8_t addr7, const void *tx, uint32_t tx_len, void *rx, uint32_t rx_len);

static int classify_and_export_device(const uint8_t *aml,
                                      const hidi2c_acpi_summary_t *s)
{
    if (!aml || !s)
        return 0;

    if (memeq_n(s->name, "ECKB", 4))
    {
        if (s->dsm_hid_desc_ok)
        {
            g_hidi2c_regs.have_eckb = 1u;
            g_hidi2c_regs.eckb_desc_reg = (uint16_t)(s->dsm_hid_desc_addr & 0xFFFFu);
            g_hidi2c_regs.eckb_desc_trusted = dsm_is_trusted(s) ? 1u : 0u;
        }

        if (s->sb_found)
            g_hidi2c_regs.eckb_addr = (uint8_t)(s->sb_slave_addr & 0x7Fu);

        if (s->gpio_found)
        {
            uint32_t i;
            g_hidi2c_regs.eckb_gpio_valid = 1u;
            g_hidi2c_regs.eckb_gpio_pin = s->gpio_first_pin;
            g_hidi2c_regs.eckb_gpio_flags = s->gpio_flags;

            for (i = 0; i < sizeof(g_hidi2c_regs.eckb_gpio_source); ++i)
            {
                g_hidi2c_regs.eckb_gpio_source[i] = s->gpio_source[i];
                if (!s->gpio_source[i])
                    break;
            }

            if (i == sizeof(g_hidi2c_regs.eckb_gpio_source))
                g_hidi2c_regs.eckb_gpio_source[sizeof(g_hidi2c_regs.eckb_gpio_source) - 1] = 0;
        }
    }

    if (memeq_n(s->name, "TCPD", 4))
    {
        terminal_print("TCPD _DSM decode addr:");
        terminal_print_hex32(s->dsm_hid_desc_addr);
        terminal_print(" ok:");
        terminal_print_hex8(s->dsm_hid_desc_ok);
        terminal_print(" trust:");
        terminal_print_hex8(dsm_is_trusted(s) ? 1u : 0u);
        terminal_print(" func1:");
        terminal_print_hex8(s->dsm_has_func1_compare);
        terminal_print(" rev1:");
        terminal_print_hex8(s->dsm_has_rev1_compare);
        terminal_print(" arg0:");
        terminal_print_hex8(s->dsm_has_arg0_ref);
        terminal_print(" arg1:");
        terminal_print_hex8(s->dsm_has_arg1_ref);
        terminal_print(" arg2:");
        terminal_print_hex8(s->dsm_has_arg2_ref);
        terminal_print(" uuid:");
        terminal_print_hex8(s->dsm_has_uuid_buffer);
        terminal_print("\n");

        g_hidi2c_regs.have_tcpd = 1u;

        /*
          For real HID-over-I2C ACPI, the device-local _DSM (function 1 with
          the HID-I2C UUID) is the standard source of the HID descriptor
          register. Trust the child-device decode path when it meets the
          UUID/Arg/rev/fn checks, instead of forcing the ancestor _DSM path.
        */
        if (s->dsm_hid_desc_ok)
            g_hidi2c_regs.tcpd_desc_reg = (uint16_t)(s->dsm_hid_desc_addr & 0xFFFFu);
        else
            g_hidi2c_regs.tcpd_desc_reg = 0u;

        g_hidi2c_regs.tcpd_desc_trusted = dsm_is_trusted(s) ? 1u : 0u;

        if (s->sb_found)
            g_hidi2c_regs.tcpd_addr = (uint8_t)(s->sb_slave_addr & 0x7Fu);

        if (s->gpio_found)
        {
            uint32_t i;
            g_hidi2c_regs.tcpd_gpio_valid = 1u;
            g_hidi2c_regs.tcpd_gpio_pin = s->gpio_first_pin;
            g_hidi2c_regs.tcpd_gpio_flags = s->gpio_flags;

            for (i = 0; i < sizeof(g_hidi2c_regs.tcpd_gpio_source); ++i)
            {
                g_hidi2c_regs.tcpd_gpio_source[i] = s->gpio_source[i];
                if (!s->gpio_source[i])
                    break;
            }

            if (i == sizeof(g_hidi2c_regs.tcpd_gpio_source))
                g_hidi2c_regs.tcpd_gpio_source[sizeof(g_hidi2c_regs.tcpd_gpio_source) - 1] = 0;
        }
    }

    if (s->name[0] == 'T' &&
        s->name[1] == 'C' &&
        s->name[2] == 'P' &&
        s->name[3] == 'D')
    {
        /*
          Keep exporting ancestor power/setup methods like _STA/_INI/_PS0,
          but do NOT export the ancestor _DSM into the runtime path.

          The child TCPD _DSM is already decoded statically above for the
          HID descriptor register. The ancestor _DSM we have been exporting
          behaves like a looping power method and does not match the standard
          HID-I2C ACPI _DSM contract.
        */
        maybe_export_tcpd_methods(aml, s);

        g_hidi2c_regs.tcpd_dsm_valid = 0u;
        g_hidi2c_regs.tcpd_dsm_len = 0u;
    }

    if (memeq_n(s->name, "ECKB", 4))
        return 1;
    if (memeq_n(s->name, "TCPD", 4))
        return 1;
    if (is_hidi2cish_id(s->hid))
        return 1;
    if (is_hidi2cish_id(s->cid))
        return 1;

    return 0;
}

static int extract_name_reference_buffer(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                         const char *name, const uint8_t **buf_out, uint16_t *len_out)
{
    int off;
    uint32_t p;
    char ref[5];

    if (!aml || !name || !buf_out || !len_out)
        return -1;

    *buf_out = 0;
    *len_out = 0;

    off = find_name_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    p = (uint32_t)off + 5;
    if (p + 4 > body_end)
        return -1;

    /* direct nameseg alias: Name (_CRS, BUF0) */
    if ((aml[p] == '\\') || (aml[p] == '^'))
        return -1;

    if (p + 4 <= body_end)
    {
        ref[0] = (char)aml[p + 0];
        ref[1] = (char)aml[p + 1];
        ref[2] = (char)aml[p + 2];
        ref[3] = (char)aml[p + 3];
        ref[4] = 0;

        if (extract_named_buffer_by_nameseg(aml, body_start, body_end, ref, buf_out, len_out) == 0)
            return 0;
    }

    return -1;
}

static int extract_method_return_ref_buffer(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                                            const char *name, const uint8_t **buf_out, uint16_t *len_out)
{
    int off;
    uint32_t pkglen, pkglen_bytes;
    uint32_t name_off;
    uint32_t flags_off;
    uint32_t meth_body;
    uint32_t meth_end;
    uint32_t i;
    char ref[5];

    if (!aml || !name || !buf_out || !len_out)
        return -1;

    *buf_out = 0;
    *len_out = 0;

    off = find_method_object(aml, body_start, body_end, name);
    if (off < 0)
        return -1;

    if (aml_read_pkglen(aml, body_end, (uint32_t)off + 1, &pkglen, &pkglen_bytes) != 0)
        return -1;

    name_off = (uint32_t)off + 1 + pkglen_bytes;
    flags_off = name_off + 4;
    meth_body = flags_off + 1;
    meth_end = (uint32_t)off + 1 + pkglen_bytes + pkglen;

    if (meth_end > body_end)
        meth_end = body_end;
    if (meth_body >= meth_end)
        return -1;

    /* Look for: ReturnOp + NameSeg */
    for (i = meth_body; i + 5 <= meth_end; ++i)
    {
        if (aml[i] != 0xA4) /* ReturnOp */
            continue;

        if (i + 5 > meth_end)
            continue;

        if (aml[i + 1] == '\\' || aml[i + 1] == '^')
            continue;

        ref[0] = (char)aml[i + 1];
        ref[1] = (char)aml[i + 2];
        ref[2] = (char)aml[i + 3];
        ref[3] = (char)aml[i + 4];
        ref[4] = 0;

        if (extract_named_buffer_by_nameseg(aml, body_start, body_end, ref, buf_out, len_out) == 0)
            return 0;
    }

    return -1;
}

static int extract_crs_buffer(const uint8_t *aml, uint32_t body_start, uint32_t body_end,
                              const uint8_t **buf_out, uint16_t *len_out, uint8_t *is_method_out)
{
    if (buf_out)
        *buf_out = 0;
    if (len_out)
        *len_out = 0;
    if (is_method_out)
        *is_method_out = 0;

    /* Name (_CRS, Buffer(...)) */
    if (extract_name_buffer(aml, body_start, body_end, "_CRS", buf_out, len_out) == 0)
        return 0;

    /* Name (_CRS, BUF0) */
    if (extract_name_reference_buffer(aml, body_start, body_end, "_CRS", buf_out, len_out) == 0)
        return 0;

    /* Method (_CRS) { Return (Buffer(...)) } */
    if (extract_method_return_buffer(aml, body_start, body_end, "_CRS", buf_out, len_out) == 0)
    {
        if (is_method_out)
            *is_method_out = 1;
        return 0;
    }

    /* Method (_CRS) { Return (BUF0) } */
    if (extract_method_return_ref_buffer(aml, body_start, body_end, "_CRS", buf_out, len_out) == 0)
    {
        if (is_method_out)
            *is_method_out = 1;
        return 0;
    }

    return -1;
}

static void crs_scan_resource_buffer(hidi2c_acpi_summary_t *s)
{
    const uint8_t *p;
    uint32_t off = 0;
    uint32_t len;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    p = s->crs_buf;
    len = s->crs_buf_len;

    while (off < len)
    {
        uint8_t tag = p[off];

        if ((tag & 0x80u) == 0)
        {
            uint8_t item_len = (uint8_t)(tag & 0x07u);
            uint8_t item = (uint8_t)((tag >> 3) & 0x0Fu);

            s->crs_small_count++;

            /* EndTag */
            if (item == 0x0Fu)
                break;

            if (off + 1u + item_len > len)
                break;

            off += 1u + item_len;
            continue;
        }
        else
        {
            uint8_t raw_tag = tag;
            uint16_t item_len;

            if (off + 3u > len)
                break;

            item_len = (uint16_t)p[off + 1] | (uint16_t)((uint16_t)p[off + 2] << 8);
            s->crs_large_count++;

            /*
              IMPORTANT:
              Use the raw large-item tag values exactly as observed in your
              old successful probe logs:

                0x8E = GPIO resource
                0x8C = I2C SerialBus resource

              Do not remap these here based on earlier guesses.
            */
            if (raw_tag == 0x8Cu)
                s->crs_has_serialbus = 1;

            if (raw_tag == 0x8Eu)
                s->crs_has_gpio = 1;

            if (off + 3u + item_len > len)
                break;

            off += 3u + item_len;
        }
    }
}

static void print_crs_first_bytes(const hidi2c_acpi_summary_t *s)
{
    uint32_t i, n;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    terminal_print(" crs0:");
    n = (s->crs_buf_len < 12u) ? s->crs_buf_len : 12u;

    for (i = 0; i < n; ++i)
    {
        terminal_print_hex8(s->crs_buf[i]);
        terminal_print(" ");
    }

    terminal_print("\n");
}

static void print_crs_descriptor_summary(const hidi2c_acpi_summary_t *s)
{
    if (!s || !s->crs_buf || s->crs_buf_len < 3)
        return;

    terminal_print(" crsTag:");
    terminal_print_hex8(s->crs_buf[0]);
    terminal_print(" crsLen:");
    terminal_print_hex8(s->crs_buf[1]);
    terminal_print(" ");
    terminal_print_hex8(s->crs_buf[2]);
    terminal_print("\n");

    if (s->gpio_found)
    {
        terminal_print(" gpioOffPin:");
        terminal_print_hex32(s->gpio_pin_table_off);
        terminal_print(" gpioOffSrc:");
        terminal_print_hex32(s->gpio_source_off);
        terminal_print("\n");
    }
}

static void copy_crs_string(const uint8_t *base, uint32_t total_len, uint16_t off,
                            char *out, uint32_t out_cap)
{
    uint32_t i = 0;
    uint32_t p = (uint32_t)off;

    if (!out || out_cap == 0)
        return;

    out[0] = 0;

    if (!base || p >= total_len)
        return;

    while (p < total_len && i + 1 < out_cap)
    {
        uint8_t c = base[p++];
        if (c == 0)
            break;
        if (c < 32 || c > 126)
            break;
        out[i++] = (char)c;
    }

    out[i] = 0;
}

static void decode_first_serialbus_descriptor(hidi2c_acpi_summary_t *s)
{
    const uint8_t *p;
    uint32_t len;
    uint32_t off = 0;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    p = s->crs_buf;
    len = s->crs_buf_len;

    while (off + 3u <= len)
    {
        uint8_t raw_tag = p[off];

        if ((raw_tag & 0x80u) == 0)
        {
            uint8_t item_len = (uint8_t)(raw_tag & 0x07u);
            uint8_t item = (uint8_t)((raw_tag >> 3) & 0x0Fu);

            if (item == 0x0Fu) /* EndTag */
                break;

            if (off + 1u + item_len > len)
                break;

            off += 1u + item_len;
            continue;
        }

        {
            uint16_t item_len = rd16le(p + off + 1);
            const uint8_t *g = p + off;
            uint32_t total = 3u + item_len;

            if (off + total > len)
                break;

            /*
              Old working TCPD/ECKB dumps show:
                0x8C = I2C serial bus resource
            */
            if (raw_tag == 0x8Cu)
            {
                s->sb_found = 1;

                /*
                  Preserve your existing field extraction layout, but only for
                  the correctly identified serial-bus descriptor.
                */
                s->sb_gen_rev = g[3];
                s->sb_bus_type = g[5];
                s->sb_flags_lo = g[6];
                s->sb_flags_hi = rd16le(g + 7);
                s->sb_type_rev = g[9];
                s->sb_type_data_len = rd16le(g + 10);

                if (total >= 22u)
                {
                    s->sb_speed_hz = rd32le(g + 12);
                    s->sb_slave_addr = rd16le(g + 16);
                    s->sb_source_off = rd16le(g + 18);

                    if (s->sb_source_off < total)
                        copy_crs_string(g, total, s->sb_source_off,
                                        s->sb_source, sizeof(s->sb_source));
                }

                return;
            }

            off += total;
        }
    }
}

static void decode_first_gpio_descriptor(hidi2c_acpi_summary_t *s)
{
    const uint8_t *p;
    uint32_t len;
    uint32_t off = 0;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    p = s->crs_buf;
    len = s->crs_buf_len;

    while (off + 3u <= len)
    {
        uint8_t raw_tag = p[off];

        if ((raw_tag & 0x80u) == 0)
        {
            uint8_t item_len = (uint8_t)(raw_tag & 0x07u);
            uint8_t item = (uint8_t)((raw_tag >> 3) & 0x0Fu);

            if (item == 0x0Fu) /* EndTag */
                break;

            if (off + 1u + item_len > len)
                break;

            off += 1u + item_len;
            continue;
        }

        {
            uint16_t item_len = rd16le(p + off + 1);
            const uint8_t *g = p + off;
            uint32_t total = 3u + item_len;

            if (off + total > len)
                break;

            /*
              Old working TCPD/ECKB dumps show:
                0x8E = GPIO resource
            */
            if (raw_tag == 0x8Eu)
            {
                uint16_t pin_table_off = 0;
                uint16_t source_name_off = 0;
                uint8_t conn_type = 0;
                uint16_t flags = 0;
                uint8_t pin_cfg = 0;

                s->gpio_found = 1;

                /*
                  Use the same layout your old probe successfully surfaced from
                  the TCPD/ECKB buffers. We keep the same extraction offsets,
                  but only after identifying the correct raw descriptor tag.
                */
                if (total >= 19u)
                {
                    conn_type = g[4];
                    flags = rd16le(g + 7);
                    pin_cfg = g[9];
                    pin_table_off = rd16le(g + 13);
                    source_name_off = rd16le(g + 17);
                }

                s->gpio_conn_type = conn_type;
                s->gpio_flags = flags;
                s->gpio_pin_cfg = pin_cfg;
                s->gpio_pin_table_off = pin_table_off;
                s->gpio_source_off = source_name_off;
                s->gpio_first_pin = 0;
                s->gpio_source[0] = 0;

                if (pin_table_off != 0 &&
                    pin_table_off + 1u < total)
                {
                    s->gpio_first_pin = rd16le(g + pin_table_off);
                }

                /*
                  Fallback scan stays, but now it runs on the correct GPIO
                  descriptor instead of the serial bus one.
                */
                if (s->gpio_first_pin == 0)
                {
                    uint32_t k;
                    for (k = 3u; k + 1u < total; k += 2u)
                    {
                        uint16_t cand = rd16le(g + k);

                        if (cand == 0)
                            continue;
                        if (cand == pin_table_off || cand == source_name_off)
                            continue;
                        if (cand >= total)
                            continue;

                        s->gpio_first_pin = cand;
                        break;
                    }
                }

                if (source_name_off != 0 &&
                    source_name_off < total)
                {
                    copy_crs_string(g, total, source_name_off,
                                    s->gpio_source, sizeof(s->gpio_source));
                }

                return;
            }

            off += total;
        }
    }
}

static void decode_first_serialbus_descriptor_legacy(hidi2c_acpi_summary_t *s)
{
    const uint8_t *p;
    uint32_t len;
    uint32_t off = 0;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    p = s->crs_buf;
    len = s->crs_buf_len;

    while (off + 3u <= len)
    {
        uint8_t raw_tag = p[off];
        uint16_t item_len;
        const uint8_t *g;
        uint32_t total;

        if ((raw_tag & 0x80u) == 0)
        {
            uint8_t item = (uint8_t)((raw_tag >> 3) & 0x0Fu);
            uint8_t small_len = (uint8_t)(raw_tag & 0x07u);

            if (item == 0x0Fu)
                break;

            if (off + 1u + small_len > len)
                break;

            off += 1u + small_len;
            continue;
        }

        item_len = rd16le(p + off + 1);
        g = p + off;
        total = 3u + item_len;

        if (off + total > len)
            break;

        /*
          Old working probes identified raw 0x8C buffers as the I2C serial bus
          resource for both ECKB and TCPD.
        */
        if (raw_tag == 0x8Cu && total >= 0x20u)
        {
            s->sb_found = 1u;

            /*
              Preserve the same layout the old probe successfully surfaced.
              This is still runtime-decoded from the live CRS bytes.
            */
            s->sb_gen_rev = g[3];
            s->sb_bus_type = g[5];
            s->sb_flags_lo = g[6];
            s->sb_flags_hi = rd16le(g + 7);
            s->sb_type_rev = g[9];
            s->sb_type_data_len = rd16le(g + 10);
            s->sb_speed_hz = rd32le(g + 12);
            s->sb_slave_addr = rd16le(g + 16);
            s->sb_source_off = rd16le(g + 18);

            if (s->sb_source_off < total)
                copy_crs_string(g, total, s->sb_source_off,
                                s->sb_source, sizeof(s->sb_source));
            return;
        }

        off += total;
    }
}

static void decode_first_gpio_descriptor_legacy(hidi2c_acpi_summary_t *s)
{
    const uint8_t *p;
    uint32_t len;
    uint32_t off = 0;

    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    p = s->crs_buf;
    len = s->crs_buf_len;

    while (off + 3u <= len)
    {
        uint8_t raw_tag = p[off];
        uint16_t item_len;
        const uint8_t *g;
        uint32_t total;

        if ((raw_tag & 0x80u) == 0)
        {
            uint8_t item = (uint8_t)((raw_tag >> 3) & 0x0Fu);
            uint8_t small_len = (uint8_t)(raw_tag & 0x07u);

            if (item == 0x0Fu)
                break;

            if (off + 1u + small_len > len)
                break;

            off += 1u + small_len;
            continue;
        }

        item_len = rd16le(p + off + 1);
        g = p + off;
        total = 3u + item_len;

        if (off + total > len)
            break;

        /*
          Old working probes identified raw 0x8E buffers as the GPIO resource
          for both ECKB and TCPD.
        */
        if (raw_tag == 0x8Eu && total >= 0x19u)
        {
            uint16_t pin_table_off = 0;
            uint16_t source_name_off = 0;
            uint32_t k;

            s->gpio_found = 1u;

            /*
              Keep this aligned to the legacy probe interpretation.
              This is format compatibility, not hardcoded device values.
            */
            s->gpio_conn_type = g[4];
            s->gpio_flags = rd16le(g + 7);
            s->gpio_pin_cfg = g[9];
            s->gpio_pin_table_off = rd16le(g + 13);
            s->gpio_source_off = rd16le(g + 17);
            s->gpio_first_pin = 0u;
            s->gpio_source[0] = 0;

            pin_table_off = s->gpio_pin_table_off;
            source_name_off = s->gpio_source_off;

            if (pin_table_off != 0u && pin_table_off + 1u < total)
                s->gpio_first_pin = rd16le(g + pin_table_off);

            if (s->gpio_first_pin == 0u)
            {
                for (k = 3u; k + 1u < total; k += 2u)
                {
                    uint16_t cand = rd16le(g + k);

                    if (cand == 0u)
                        continue;
                    if (cand == pin_table_off || cand == source_name_off)
                        continue;
                    if (cand >= total)
                        continue;

                    s->gpio_first_pin = cand;
                    break;
                }
            }

            if (source_name_off != 0u && source_name_off < total)
            {
                copy_crs_string(g, total, source_name_off,
                                s->gpio_source, sizeof(s->gpio_source));
            }

            return;
        }

        off += total;
    }
}

static void decode_crs_runtime_compat(hidi2c_acpi_summary_t *s)
{
    if (!s || !s->crs_buf || !s->crs_buf_len)
        return;

    /*
      First try the current decoder.
    */
    crs_scan_resource_buffer(s);

    if (!s->sb_found)
        decode_first_serialbus_descriptor(s);

    if (!s->gpio_found)
        decode_first_gpio_descriptor(s);

    /*
      Compatibility fallback:
      if the current path still failed to recover one of the resources, retry
      using the old working probe layout against the same live CRS buffer.
    */
    if (!s->sb_found)
        decode_first_serialbus_descriptor_legacy(s);

    if (!s->gpio_found)
        decode_first_gpio_descriptor_legacy(s);
}

static void summarise_parent_scope(hidi2c_acpi_summary_t *s,
                                   const uint8_t *aml, uint32_t aml_len, uint32_t devop_off)
{
    uint32_t parent_name_off = 0;
    uint32_t parent_body_start = 0;
    uint32_t parent_end = 0;

    if (!s || !aml)
        return;

    uint32_t obj_off = 0;
    if (find_enclosing_parent_object(aml, aml_len, devop_off,
                                     &parent_name_off, &parent_body_start, &parent_end, &obj_off) != 0)
        return;

    copy_sig4(s->parent_name, aml + parent_name_off);

    s->parent_has_sta = has_name_or_method(aml, parent_body_start, parent_end, "_STA");
    s->parent_has_ini = has_name_or_method(aml, parent_body_start, parent_end, "_INI");
    s->parent_has_ps0 = has_name_or_method(aml, parent_body_start, parent_end, "_PS0");
    s->parent_has_ps2 = has_name_or_method(aml, parent_body_start, parent_end, "_PS2");
    s->parent_has_ps3 = has_name_or_method(aml, parent_body_start, parent_end, "_PS3");
    s->parent_has_on = has_name_or_method(aml, parent_body_start, parent_end, "_ON_");
    s->parent_has_off = has_name_or_method(aml, parent_body_start, parent_end, "_OFF");
    s->parent_has_rst = has_name_or_method(aml, parent_body_start, parent_end, "_RST");
    s->parent_has_pr0 = has_name_or_method(aml, parent_body_start, parent_end, "_PR0");
    s->parent_has_pr3 = has_name_or_method(aml, parent_body_start, parent_end, "_PR3");

    s->parent_has_power_resource =
        body_contains_ascii_nocase(aml, parent_body_start, parent_end, "PowerResource", 13);

    s->parent_mentions_child =
        body_contains_bytes(aml, parent_body_start, parent_end, s->name, 4);

    if (s->gpio_source[0])
        s->parent_mentions_gpio_src =
            body_contains_bytes(aml, parent_body_start, parent_end, s->gpio_source, 4);

    if (s->sb_source[0])
        s->parent_mentions_sb_src =
            body_contains_bytes(aml, parent_body_start, parent_end, s->sb_source, 4);
}

static void summarise_named_scope_fields(hidi2c_acpi_summary_t *s,
                                         const uint8_t *aml,
                                         uint32_t body_start,
                                         uint32_t body_end,
                                         char out_name[5],
                                         uint8_t *has_sta,
                                         uint8_t *has_ini,
                                         uint8_t *has_ps0,
                                         uint8_t *has_ps2,
                                         uint8_t *has_ps3,
                                         uint8_t *has_on,
                                         uint8_t *has_off,
                                         uint8_t *has_rst,
                                         uint8_t *has_pr0,
                                         uint8_t *has_pr3,
                                         uint8_t *has_power_resource,
                                         uint8_t *mentions_child,
                                         uint8_t *mentions_gpio_src,
                                         uint8_t *mentions_sb_src)
{
    if (!s || !aml)
        return;

    *has_sta = has_name_or_method(aml, body_start, body_end, "_STA");
    *has_ini = has_name_or_method(aml, body_start, body_end, "_INI");
    *has_ps0 = has_name_or_method(aml, body_start, body_end, "_PS0");
    *has_ps2 = has_name_or_method(aml, body_start, body_end, "_PS2");
    *has_ps3 = has_name_or_method(aml, body_start, body_end, "_PS3");
    *has_on = has_name_or_method(aml, body_start, body_end, "_ON_");
    *has_off = has_name_or_method(aml, body_start, body_end, "_OFF");
    *has_rst = has_name_or_method(aml, body_start, body_end, "_RST");
    *has_pr0 = has_name_or_method(aml, body_start, body_end, "_PR0");
    *has_pr3 = has_name_or_method(aml, body_start, body_end, "_PR3");

    *has_power_resource =
        body_contains_ascii_nocase(aml, body_start, body_end, "PowerResource", 13);

    *mentions_child =
        body_contains_bytes(aml, body_start, body_end, s->name, 4);

    *mentions_gpio_src = 0;
    *mentions_sb_src = 0;

    if (s->gpio_source[0])
        *mentions_gpio_src =
            body_contains_bytes(aml, body_start, body_end, s->gpio_source, 4);

    if (s->sb_source[0])
        *mentions_sb_src =
            body_contains_bytes(aml, body_start, body_end, s->sb_source, 4);
}

static void dump_scope_power_methods(const char *label,
                                     const uint8_t *aml,
                                     uint32_t body_start,
                                     uint32_t body_end)
{
    uint32_t meth_body = 0, meth_end = 0;
    char tag[24];
    uint32_t i = 0;

    if (!label || !aml)
        return;

    while (label[i] && i + 1 < sizeof(tag))
    {
        tag[i] = label[i];
        ++i;
    }
    tag[i] = 0;

    if (extract_method_body_bounds(aml, body_start, body_end, "_PS0", &meth_body, &meth_end) == 0)
    {
        terminal_print(tag);
        terminal_print("._PS0\n");
        print_method_body_hex("  body", aml, meth_body, meth_end);
    }

    if (extract_method_body_bounds(aml, body_start, body_end, "_PS3", &meth_body, &meth_end) == 0)
    {
        terminal_print(tag);
        terminal_print("._PS3\n");
        print_method_body_hex("  body", aml, meth_body, meth_end);
    }
}

static void summarise_ancestor_chain(hidi2c_acpi_summary_t *s,
                                     const uint8_t *aml, uint32_t aml_len, uint32_t devop_off)
{
    uint32_t p_name = 0, p_body = 0, p_end = 0, p_obj = 0;
    uint32_t gp_name = 0, gp_body = 0, gp_end = 0, gp_obj = 0;
    uint32_t ggp_name = 0, ggp_body = 0, ggp_end = 0, ggp_obj = 0;

    if (!s || !aml)
        return;

    /* parent = immediate enclosing object of the device */
    if (find_enclosing_parent_object(aml, aml_len, devop_off,
                                     &p_name, &p_body, &p_end, &p_obj) != 0)
        return;

    copy_sig4_printable(s->parent_name, aml + p_name);
    summarise_named_scope_fields(s, aml, p_body, p_end,
                                 s->parent_name,
                                 &s->parent_has_sta, &s->parent_has_ini,
                                 &s->parent_has_ps0, &s->parent_has_ps2, &s->parent_has_ps3,
                                 &s->parent_has_on, &s->parent_has_off, &s->parent_has_rst,
                                 &s->parent_has_pr0, &s->parent_has_pr3,
                                 &s->parent_has_power_resource,
                                 &s->parent_mentions_child,
                                 &s->parent_mentions_gpio_src,
                                 &s->parent_mentions_sb_src);

    s->parent_body_start = p_body;
    s->parent_body_end = p_end;
    s->parent_obj_off = p_obj;

    /* grandparent = enclosing object of the parent object */
    if (p_obj > 0 &&
        find_enclosing_parent_object(aml, aml_len, p_obj,
                                     &gp_name, &gp_body, &gp_end, &gp_obj) == 0)
    {
        copy_sig4_printable(s->grandparent_name, aml + gp_name);
        summarise_named_scope_fields(s, aml, gp_body, gp_end,
                                     s->grandparent_name,
                                     &s->grandparent_has_sta, &s->grandparent_has_ini,
                                     &s->grandparent_has_ps0, &s->grandparent_has_ps2, &s->grandparent_has_ps3,
                                     &s->grandparent_has_on, &s->grandparent_has_off, &s->grandparent_has_rst,
                                     &s->grandparent_has_pr0, &s->grandparent_has_pr3,
                                     &s->grandparent_has_power_resource,
                                     &s->grandparent_mentions_child,
                                     &s->grandparent_mentions_gpio_src,
                                     &s->grandparent_mentions_sb_src);

        s->grandparent_body_start = gp_body;
        s->grandparent_body_end = gp_end;
        s->grandparent_obj_off = gp_obj;

        /* great-grandparent = enclosing object of the grandparent object */
        if (gp_obj > 0 &&
            find_enclosing_parent_object(aml, aml_len, gp_obj,
                                         &ggp_name, &ggp_body, &ggp_end, &ggp_obj) == 0)
        {
            copy_sig4_printable(s->ggparent_name, aml + ggp_name);
            summarise_named_scope_fields(s, aml, ggp_body, ggp_end,
                                         s->ggparent_name,
                                         &s->ggparent_has_sta, &s->ggparent_has_ini,
                                         &s->ggparent_has_ps0, &s->ggparent_has_ps2, &s->ggparent_has_ps3,
                                         &s->ggparent_has_on, &s->ggparent_has_off, &s->ggparent_has_rst,
                                         &s->ggparent_has_pr0, &s->ggparent_has_pr3,
                                         &s->ggparent_has_power_resource,
                                         &s->ggparent_mentions_child,
                                         &s->ggparent_mentions_gpio_src,
                                         &s->ggparent_mentions_sb_src);

            s->ggparent_body_start = ggp_body;
            s->ggparent_body_end = ggp_end;
            s->ggparent_obj_off = ggp_obj;
        }
    }
}

static void scan_global_tcpd_wake_methods(const uint8_t *aml, uint32_t aml_len)
{
    uint32_t i;

    if (!aml || aml_len < 8)
        return;

    terminal_print(" TCPD global wake scan\n");

    for (i = 0; i + 6 < aml_len; ++i)
    {
        uint32_t name_off = 0;
        uint32_t body_start = 0;
        uint32_t body_end = 0;
        char objname[5];
        int ok = 0;

        if (aml[i] == 0x5Bu && aml[i + 1u] == 0x82u) /* DeviceOp */
            ok = (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);
        else if (aml[i] == 0x10u) /* ScopeOp */
            ok = (get_scope_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) == 0);

        if (!ok)
            continue;

        copy_sig4_printable(objname, aml + name_off);

        if (!body_looks_like_tcpd_power_method(aml, body_start, body_end))
            continue;

        terminal_print(" candidate:");
        terminal_print(objname);
        terminal_print(" obj:");
        terminal_print_hex32(i);
        terminal_print(" body:");
        terminal_print_hex32(body_start);
        terminal_print("-");
        terminal_print_hex32(body_end);
        terminal_print("\n");

        dump_scope_candidate_methods(aml, body_start, body_end, i, objname);
    }
}

static void print_decoded_crs(const hidi2c_acpi_summary_t *s)
{
    if (!s)
        return;

    if (s->sb_found)
    {
        terminal_print(" sbType:");
        terminal_print_hex8(s->sb_bus_type);
        terminal_print(" sbRev:");
        terminal_print_hex8(s->sb_gen_rev);
        terminal_print(" sbTRev:");
        terminal_print_hex8(s->sb_type_rev);
        terminal_print(" sbAddr:");
        terminal_print_hex32((uint32_t)s->sb_slave_addr);
        terminal_print(" sbHz:");
        terminal_print_hex32(s->sb_speed_hz);
        terminal_print(" sbSrc:");
        terminal_print(s->sb_source[0] ? s->sb_source : "-");
        terminal_print("\n");
    }

    if (s->gpio_found)
    {
        terminal_print(" gpType:");
        terminal_print_hex8(s->gpio_conn_type);
        terminal_print(" gpFlg:");
        terminal_print_hex32((uint32_t)s->gpio_flags);
        terminal_print(" gpPin:");
        terminal_print_hex32((uint32_t)s->gpio_first_pin);
        terminal_print(" gpSrc:");
        terminal_print(s->gpio_source[0] ? s->gpio_source : "-");
        terminal_print("\n");
    }
}

static void summarise_target_device(const uint8_t *aml, uint32_t aml_len, uint32_t devop_off,
                                    uint32_t *hits_out)
{
    uint32_t name_off = 0;
    uint32_t body_start = 0;
    uint32_t body_end = 0;
    hidi2c_acpi_summary_t s;
    uint32_t i;

    (void)aml_len;

    for (i = 0; i < sizeof(s); ++i)
        ((uint8_t *)&s)[i] = 0;

    if (get_device_bounds(aml, aml_len, devop_off, &name_off, &body_start, &body_end) != 0)
        return;

    copy_sig4(s.name, aml + name_off);

    (void)extract_name_id(aml, body_start, body_end, "_HID", s.hid, sizeof(s.hid));
    (void)extract_name_id(aml, body_start, body_end, "_CID", s.cid, sizeof(s.cid));

    s.has_sta = has_name_or_method(aml, body_start, body_end, "_STA");
    s.has_ini = has_name_or_method(aml, body_start, body_end, "_INI");
    s.has_crs = has_name_or_method(aml, body_start, body_end, "_CRS");
    s.has_srs = has_name_or_method(aml, body_start, body_end, "_SRS");
    s.has_ps0 = has_name_or_method(aml, body_start, body_end, "_PS0");
    s.has_ps2 = has_name_or_method(aml, body_start, body_end, "_PS2");
    s.has_ps3 = has_name_or_method(aml, body_start, body_end, "_PS3");
    s.has_dis = has_name_or_method(aml, body_start, body_end, "_DIS");
    s.has_rst = has_name_or_method(aml, body_start, body_end, "_RST");
    s.has_dsm = has_name_or_method(aml, body_start, body_end, "_DSM");
    s.has_dsd = has_name_or_method(aml, body_start, body_end, "_DSD");
    s.has_dep = has_name_or_method(aml, body_start, body_end, "_DEP");
    s.has_pr0 = has_name_or_method(aml, body_start, body_end, "_PR0");
    s.has_pr3 = has_name_or_method(aml, body_start, body_end, "_PR3");
    s.ref_i2c1 = body_contains_nameseg(aml, body_start, body_end, "I2C1");

    s.body_has_i2c_serialbus = body_contains_bytes(aml, body_start, body_end, "I2cSerialBus", 12);
    s.body_has_gpioint = body_contains_bytes(aml, body_start, body_end, "GpioInt", 7);
    s.dep_refs_i2c1 = dep_refs_name(aml, body_start, body_end, "I2C1");
    s.dsm_argcount = method_argcount(aml, body_start, body_end, "_DSM");

    dsm_decode_hid_desc_best_effort(&s, aml, body_start, body_end);

    {
        uint8_t crs_is_method = 0;

        if (extract_crs_buffer(aml, body_start, body_end,
                               &s.crs_buf, &s.crs_buf_len, &crs_is_method) == 0)
        {
            s.crs_is_name_buffer = crs_is_method ? 0 : 1;
            s.crs_is_method_buffer = crs_is_method ? 1 : 0;
            decode_crs_runtime_compat(&s);
        }
        
        summarise_ancestor_chain(&s, aml, aml_len, devop_off);
    }

    if (!classify_and_export_device(aml, &s))
        return;

    if (hits_out)
        (*hits_out)++;

    terminal_print("dev:");
    terminal_print(s.name);
    terminal_print(" hid:");
    terminal_print(s.hid[0] ? s.hid : "-");
    terminal_print(" cid:");
    terminal_print(s.cid[0] ? s.cid : "-");
    terminal_print(" i2c1:");
    terminal_print(s.ref_i2c1 ? "Y" : "N");
    terminal_print("\n");

    print_flag("_STA", s.has_sta);
    print_flag("_INI", s.has_ini);
    print_flag("_CRS", s.has_crs);
    print_flag("_SRS", s.has_srs);
    print_flag("_PS0", s.has_ps0);
    print_flag("_PS2", s.has_ps2);
    print_flag("_PS3", s.has_ps3);
    terminal_print("\n");

    terminal_print(" bus:");
    terminal_print(s.body_has_i2c_serialbus ? "Y" : "N");
    terminal_print(" gpio:");
    terminal_print(s.body_has_gpioint ? "Y" : "N");
    terminal_print(" depI2C1:");
    terminal_print(s.dep_refs_i2c1 ? "Y" : "N");
    terminal_print(" dsmArgs:");
    if (s.dsm_argcount == 0xFFu)
        terminal_print("-");
    else
        terminal_print_hex8(s.dsm_argcount);
    terminal_print("\n");

    terminal_print(" dsmGuid:");
    terminal_print(s.dsm_guid_ascii_seen ? "Y" : "N");
    terminal_print(" dsmDesc:");
    if (s.dsm_hid_desc_ok)
        terminal_print_hex32(s.dsm_hid_desc_addr);
    else
        terminal_print("-");
    terminal_print(" dsmRet:");
    terminal_print_hex8(s.dsm_ret_count);
    terminal_print("\n");

    if (s.dsm_ret_count)
    {
        terminal_print(" dsmVals:");
        for (i = 0; i < s.dsm_ret_count; ++i)
        {
            terminal_print_hex32(s.dsm_ret[i]);
            terminal_print(" ");
        }
        terminal_print("\n");
    }

    terminal_print(" dsmA0:");
    terminal_print(s.dsm_has_arg0_ref ? "Y" : "N");
    terminal_print(" dsmA1:");
    terminal_print(s.dsm_has_arg1_ref ? "Y" : "N");
    terminal_print(" dsmA2:");
    terminal_print(s.dsm_has_arg2_ref ? "Y" : "N");
    terminal_print(" dsmFn1:");
    terminal_print(s.dsm_has_func1_compare ? "Y" : "N");
    terminal_print(" dsmRev1:");
    terminal_print(s.dsm_has_rev1_compare ? "Y" : "N");
    terminal_print("\n");

    terminal_print(" dsmTrust:");
    if (s.dsm_has_func1_compare && s.dsm_has_rev1_compare && s.dsm_hid_desc_ok)
        terminal_print("Y");
    else
        terminal_print("N");
    terminal_print("\n");

    print_flag("_DIS", s.has_dis);
    print_flag("_RST", s.has_rst);
    print_flag("_DSM", s.has_dsm);
    print_flag("_DSD", s.has_dsd);
    print_flag("_DEP", s.has_dep);
    print_flag("_PR0", s.has_pr0);
    print_flag("_PR3", s.has_pr3);
    terminal_print("\n");

    terminal_print(" crsName:");
    terminal_print(s.crs_is_name_buffer ? "Y" : "N");
    terminal_print(" crsMeth:");
    terminal_print(s.crs_is_method_buffer ? "Y" : "N");
    terminal_print(" len:");
    terminal_print_hex32((uint32_t)s.crs_buf_len);
    terminal_print(" ser:");
    terminal_print(s.crs_has_serialbus ? "Y" : "N");
    terminal_print(" gp:");
    terminal_print(s.crs_has_gpio ? "Y" : "N");
    terminal_print(" sm:");
    terminal_print_hex8(s.crs_small_count);
    terminal_print(" lg:");
    terminal_print_hex8(s.crs_large_count);
    terminal_print("\n");

    if (s.has_crs && !s.crs_buf)
    {
        terminal_print(" crsKind:complex\n");
    }

    if (s.crs_is_name_buffer || s.crs_is_method_buffer)
        print_crs_first_bytes(&s);

    if (s.crs_is_name_buffer || s.crs_is_method_buffer)
        print_decoded_crs(&s);

    if (s.parent_name[0])
    {
        terminal_print(" parent:");
        terminal_print(s.parent_name);
        terminal_print(" obj:");
        terminal_print_hex32(s.parent_obj_off);
        terminal_print(" pPS0:");
        terminal_print(s.parent_has_ps0 ? "Y" : "N");
        terminal_print(" pPS2:");
        terminal_print(s.parent_has_ps2 ? "Y" : "N");
        terminal_print(" pPS3:");
        terminal_print(s.parent_has_ps3 ? "Y" : "N");
        terminal_print(" pON:");
        terminal_print(s.parent_has_on ? "Y" : "N");
        terminal_print(" pOFF:");
        terminal_print(s.parent_has_off ? "Y" : "N");
        terminal_print(" pRST:");
        terminal_print(s.parent_has_rst ? "Y" : "N");
        terminal_print("\n");

        terminal_print(" pPR0:");
        terminal_print(s.parent_has_pr0 ? "Y" : "N");
        terminal_print(" pPR3:");
        terminal_print(s.parent_has_pr3 ? "Y" : "N");
        terminal_print(" pPwrRes:");
        terminal_print(s.parent_has_power_resource ? "Y" : "N");
        terminal_print(" pChild:");
        terminal_print(s.parent_mentions_child ? "Y" : "N");
        terminal_print(" pGPIO:");
        terminal_print(s.parent_mentions_gpio_src ? "Y" : "N");
        terminal_print(" pSB:");
        terminal_print(s.parent_mentions_sb_src ? "Y" : "N");
        terminal_print("\n");
    }

    if (s.grandparent_name[0])
    {
        terminal_print(" gparent:");
        terminal_print(s.grandparent_name);
        terminal_print(" obj:");
        terminal_print_hex32(s.grandparent_obj_off);
        terminal_print(" pPS0:");
        terminal_print(s.grandparent_has_ps0 ? "Y" : "N");
        terminal_print(" pPS2:");
        terminal_print(s.grandparent_has_ps2 ? "Y" : "N");
        terminal_print(" pPS3:");
        terminal_print(s.grandparent_has_ps3 ? "Y" : "N");
        terminal_print(" pON:");
        terminal_print(s.grandparent_has_on ? "Y" : "N");
        terminal_print(" pOFF:");
        terminal_print(s.grandparent_has_off ? "Y" : "N");
        terminal_print(" pRST:");
        terminal_print(s.grandparent_has_rst ? "Y" : "N");
        terminal_print("\n");

        terminal_print(" gPR0:");
        terminal_print(s.grandparent_has_pr0 ? "Y" : "N");
        terminal_print(" gPR3:");
        terminal_print(s.grandparent_has_pr3 ? "Y" : "N");
        terminal_print(" gPwrRes:");
        terminal_print(s.grandparent_has_power_resource ? "Y" : "N");
        terminal_print(" gChild:");
        terminal_print(s.grandparent_mentions_child ? "Y" : "N");
        terminal_print(" gGPIO:");
        terminal_print(s.grandparent_mentions_gpio_src ? "Y" : "N");
        terminal_print(" gSB:");
        terminal_print(s.grandparent_mentions_sb_src ? "Y" : "N");
        terminal_print("\n");
    }

    if (s.ggparent_name[0])
    {
        terminal_print(" ggparent:");
        terminal_print(s.ggparent_name);
        terminal_print(" obj:");
        terminal_print_hex32(s.ggparent_obj_off);
        terminal_print(" pPS0:");
        terminal_print(s.ggparent_has_ps0 ? "Y" : "N");
        terminal_print(" pPS2:");
        terminal_print(s.ggparent_has_ps2 ? "Y" : "N");
        terminal_print(" pPS3:");
        terminal_print(s.ggparent_has_ps3 ? "Y" : "N");
        terminal_print(" pON:");
        terminal_print(s.ggparent_has_on ? "Y" : "N");
        terminal_print(" pOFF:");
        terminal_print(s.ggparent_has_off ? "Y" : "N");
        terminal_print(" pRST:");
        terminal_print(s.ggparent_has_rst ? "Y" : "N");
        terminal_print("\n");

        terminal_print(" ggPR0:");
        terminal_print(s.ggparent_has_pr0 ? "Y" : "N");
        terminal_print(" ggPR3:");
        terminal_print(s.ggparent_has_pr3 ? "Y" : "N");
        terminal_print(" ggPwrRes:");
        terminal_print(s.ggparent_has_power_resource ? "Y" : "N");
        terminal_print(" ggChild:");
        terminal_print(s.ggparent_mentions_child ? "Y" : "N");
        terminal_print(" ggGPIO:");
        terminal_print(s.ggparent_mentions_gpio_src ? "Y" : "N");
        terminal_print(" ggSB:");
        terminal_print(s.ggparent_mentions_sb_src ? "Y" : "N");
        terminal_print("\n");
    }

    if (memeq_n(s.name, "TCPD", 4))
    {
        terminal_print(" TCPD power walk\n");

        if (s.parent_name[0] && s.parent_has_ps0)
            dump_scope_candidate_methods(aml, s.parent_body_start, s.parent_body_end, s.parent_obj_off, s.parent_name);

        if (s.grandparent_name[0] && s.grandparent_has_ps0)
            dump_scope_candidate_methods(aml, s.grandparent_body_start, s.grandparent_body_end, s.grandparent_obj_off, s.grandparent_name);

        if (s.ggparent_name[0] && s.ggparent_has_ps0)
            dump_scope_candidate_methods(aml, s.ggparent_body_start, s.ggparent_body_end, s.ggparent_obj_off, s.ggparent_name);

        scan_global_tcpd_wake_methods(aml, aml_len);
        dump_gio0_scope_candidates(aml, aml_len);
        maybe_export_gio0_reg(aml, aml_len);
        maybe_export_gio0_dsm(aml, aml_len);
    }

    if (s.crs_is_name_buffer || s.crs_is_method_buffer)
        print_crs_descriptor_summary(&s);
}

static void probe_i2c1_children(const acpi_sdt_header_t *dsdt)
{
    const uint8_t *table;
    const uint8_t *aml;
    uint32_t table_len;
    uint32_t aml_len;
    uint32_t i;
    uint32_t found_i2c1 = 0;
    uint32_t hits = 0;

    if (!dsdt)
    {
        terminal_error("hidacpi: no dsdt");
        return;
    }

    if (!memeq_n(dsdt->signature, "DSDT", 4))
    {
        terminal_error("hidacpi: not dsdt");
        return;
    }

    if (!checksum_ok(dsdt, dsdt->length))
        terminal_warn("hidacpi: dsdt checksum bad");

    table = (const uint8_t *)dsdt;
    table_len = dsdt->length;

    if (table_len <= sizeof(acpi_sdt_header_t))
    {
        terminal_error("hidacpi: dsdt too short");
        return;
    }

    aml = table + sizeof(acpi_sdt_header_t);
    aml_len = table_len - (uint32_t)sizeof(acpi_sdt_header_t);

    for (i = 0; i + 6 < aml_len; ++i)
    {
        if (aml[i] == 0x5B && aml[i + 1] == 0x82)
        {
            uint32_t name_off = 0;
            uint32_t body_start = 0;
            uint32_t body_end = 0;

            if (get_device_bounds(aml, aml_len, i, &name_off, &body_start, &body_end) != 0)
                continue;

            if (aml[name_off + 0] == 'I' &&
                aml[name_off + 1] == '2' &&
                aml[name_off + 2] == 'C' &&
                aml[name_off + 3] == '1')
            {
                found_i2c1 = 1;
            }

            summarise_target_device(aml, aml_len, i, &hits);
        }
    }

    terminal_print("hidacpi: I2C1 ");
    terminal_print(found_i2c1 ? "found" : "missing");
    terminal_print("\n");

    if (!hits)
        terminal_warn("hidacpi: no HID/I2C targets");
    else
    {
        terminal_print("hidacpi hits:");
        terminal_print_hex32(hits);
        terminal_print("\n");
    }
}

void acpi_probe_hidi2c_ready_from_rsdp(uint64_t rsdp_phys)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;
    const acpi_sdt_header_t *fadt;
    const acpi_sdt_header_t *dsdt;
    char rootsig[5];

    terminal_print("hidacpi probe start\n");

    if (!rsdp_phys)
    {
        terminal_error("hidacpi: rsdp null");
        return;
    }

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
    {
        terminal_error("hidacpi: bad rsdp");
        return;
    }

    root = find_xsdt_or_rsdt(rsdp);
    if (!root)
    {
        terminal_error("hidacpi: no root");
        return;
    }

    copy_sig4(rootsig, (const uint8_t *)root->signature);
    terminal_print("hidacpi root:");
    terminal_print(rootsig);
    terminal_print("\n");

    fadt = find_fadt_from_root(root);
    if (!fadt)
    {
        terminal_error("hidacpi: no fadt");
        return;
    }

    dsdt = find_dsdt_from_fadt(fadt);
    if (!dsdt)
    {
        terminal_error("hidacpi: no dsdt");
        return;
    }

    probe_i2c1_children(dsdt);

    terminal_print("hidacpi probe end\n");
}

int acpi_hidi2c_get_regs_from_rsdp(uint64_t rsdp_phys, hidi2c_acpi_regs *out)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;
    const acpi_sdt_header_t *fadt;
    const acpi_sdt_header_t *dsdt;

    if (!out || !rsdp_phys)
        return -1;

    g_hidi2c_regs.have_eckb = 0u;
    g_hidi2c_regs.have_tcpd = 0u;
    g_hidi2c_regs.eckb_desc_reg = 0u;
    g_hidi2c_regs.tcpd_desc_reg = 0u;
    g_hidi2c_regs.eckb_addr = 0u;
    g_hidi2c_regs.tcpd_addr = 0u;
    g_hidi2c_regs.eckb_desc_trusted = 0u;
    g_hidi2c_regs.tcpd_desc_trusted = 0u;

    g_hidi2c_regs.eckb_gpio_valid = 0u;
    g_hidi2c_regs.eckb_gpio_pin = 0u;
    g_hidi2c_regs.eckb_gpio_flags = 0u;
    g_hidi2c_regs.eckb_gpio_source[0] = 0;

    g_hidi2c_regs.tcpd_gpio_valid = 0u;
    g_hidi2c_regs.tcpd_gpio_pin = 0u;
    g_hidi2c_regs.tcpd_gpio_flags = 0u;
    g_hidi2c_regs.tcpd_gpio_source[0] = 0;

    g_hidi2c_regs.tcpd_ps0_valid = 0u;
    g_hidi2c_regs.tcpd_ps0_len = 0u;

    g_hidi2c_regs.tcpd_ps0_valid = 0u;
    g_hidi2c_regs.tcpd_ps0_len = 0u;

    g_hidi2c_regs.tcpd_ps3_valid = 0u;
    g_hidi2c_regs.tcpd_ps3_len = 0u;

    g_hidi2c_regs.tcpd_sta_valid = 0u;
    g_hidi2c_regs.tcpd_sta_len = 0u;

    g_hidi2c_regs.tcpd_ini_valid = 0u;
    g_hidi2c_regs.tcpd_ini_len = 0u;

    g_hidi2c_regs.tcpd_gio0_reg_valid = 0u;
    g_hidi2c_regs.tcpd_gio0_reg_len = 0u;

    for (uint32_t i = 0; i < HIDI2C_ACPI_MAX_METHOD_BODY; ++i)
        g_hidi2c_regs.tcpd_ps0_body[i] = 0u;

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;
    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
        return -1;

    root = find_xsdt_or_rsdt(rsdp);
    if (!root)
        return -1;

    fadt = find_fadt_from_root(root);
    if (!fadt)
        return -1;

    dsdt = find_dsdt_from_fadt(fadt);
    if (!dsdt)
        return -1;

    probe_i2c1_children(dsdt);

    *out = g_hidi2c_regs;
    return 0;
}