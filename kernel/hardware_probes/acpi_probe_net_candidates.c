#include "hardware_probes/acpi_probe_net_candidates.h"
#include "acpi/aml_tiny.h"
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

static acpi_net_resource_window g_net_res[ACPI_NET_RESOURCE_MAX];
static uint32_t g_net_res_count = 0;
static acpi_net_gpio_hint g_net_gpio[ACPI_NET_GPIO_MAX];
static uint32_t g_net_gpio_count = 0;
static uint8_t g_crs_eval_buf[AML_TINY_MAX_BUFFER_BYTES];
static uint32_t g_crs_eval_len = 0;
typedef struct
{
    char path[64];
    uint64_t value;
} aml_named_slot;
static aml_named_slot g_aml_named[64];
static uint32_t g_aml_named_count = 0;
static uint32_t g_aml_named_rw_log_count = 0;

static uint32_t cstr_len_bounded(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s)
        return 0;
    while (n < cap && s[n])
        n++;
    return n;
}

static int cstr_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

static void aml_named_reset(void)
{
    g_aml_named_count = 0;
    g_aml_named_rw_log_count = 0;
}

static int aml_named_get(const char *path, uint64_t *out)
{
    if (!path || !out)
        return 0;
    for (uint32_t i = 0; i < g_aml_named_count; ++i)
    {
        if (cstr_eq(g_aml_named[i].path, path))
        {
            *out = g_aml_named[i].value;
            return 1;
        }
    }
    return 0;
}

static int aml_named_has(const char *path)
{
    uint64_t ignored = 0;
    return aml_named_get(path, &ignored);
}

static void aml_named_set(const char *path, uint64_t value)
{
    uint32_t n;
    if (!path || !path[0])
        return;
    for (uint32_t i = 0; i < g_aml_named_count; ++i)
    {
        if (cstr_eq(g_aml_named[i].path, path))
        {
            g_aml_named[i].value = value;
            return;
        }
    }
    if (g_aml_named_count >= (uint32_t)(sizeof(g_aml_named) / sizeof(g_aml_named[0])))
        return;
    n = cstr_len_bounded(path, sizeof(g_aml_named[0].path) - 1u);
    for (uint32_t i = 0; i < n; ++i)
        g_aml_named[g_aml_named_count].path[i] = path[i];
    g_aml_named[g_aml_named_count].path[n] = 0;
    g_aml_named[g_aml_named_count].value = value;
    g_aml_named_count++;
}

static int aml_named_set_if_absent(const char *path, uint64_t value)
{
    if (!path || !path[0] || aml_named_has(path))
        return 0;
    aml_named_set(path, value);
    return aml_named_has(path);
}

static void net_res_reset(void)
{
    g_net_res_count = 0;
    g_net_gpio_count = 0;
}

static void copy_namez(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0u)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (i + 1u < cap && src[i])
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void net_res_add(uint32_t kind,
                        uint32_t rtype,
                        uint64_t min_addr,
                        uint64_t max_addr,
                        uint64_t span_len,
                        const char *dev_name,
                        const char *hid_name)
{
    acpi_net_resource_window *e;
    if (g_net_res_count >= ACPI_NET_RESOURCE_MAX)
        return;
    e = &g_net_res[g_net_res_count++];
    e->kind = kind;
    e->rtype = rtype;
    e->min_addr = min_addr;
    e->max_addr = max_addr;
    e->span_len = span_len;
    copy_namez(e->dev_name, sizeof(e->dev_name), dev_name);
    copy_namez(e->hid_name, sizeof(e->hid_name), hid_name);
}

static void net_gpio_add(uint32_t conn_type,
                         uint32_t flags,
                         uint32_t pin_config,
                         uint32_t pin,
                         const char *dev_name,
                         const char *hid_name)
{
    acpi_net_gpio_hint *e;
    if (g_net_gpio_count >= ACPI_NET_GPIO_MAX)
        return;
    e = &g_net_gpio[g_net_gpio_count++];
    e->conn_type = conn_type;
    e->flags = flags;
    e->pin_config = pin_config;
    e->pin = pin;
    copy_namez(e->dev_name, sizeof(e->dev_name), dev_name);
    copy_namez(e->hid_name, sizeof(e->hid_name), hid_name);
}

static int memeq_n(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int find_bytes(const uint8_t *buf, uint32_t len, const char *pat, uint32_t pat_len)
{
    if (!buf || !pat || pat_len == 0u || len < pat_len)
        return 0;

    for (uint32_t i = 0; i <= len - pat_len; ++i)
    {
        uint32_t j = 0;
        for (; j < pat_len; ++j)
        {
            if (buf[i + j] != (uint8_t)pat[j])
                break;
        }
        if (j == pat_len)
            return 1;
    }
    return 0;
}

static uint32_t read_dword(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_aml_qword(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8u; ++i)
        v |= ((uint64_t)p[i]) << (8u * i);
    return v;
}

static uint64_t read_qword(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; ++i)
        v |= ((uint64_t)p[i]) << (8u * i);
    return v;
}

static uint16_t read_word(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void copy_sig4(char out[5], const char in[4])
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = 0;
}

static int is_nameseg_char(uint8_t c)
{
    if (c == '_')
        return 1;
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    return 0;
}

static int nameseg_is_valid(const uint8_t *p)
{
    return p &&
           is_nameseg_char(p[0]) &&
           is_nameseg_char(p[1]) &&
           is_nameseg_char(p[2]) &&
           is_nameseg_char(p[3]);
}

static void decode_eisaid(uint32_t eisa, char out[8])
{
    uint8_t c1 = (uint8_t)((eisa >> 26) & 0x1Fu);
    uint8_t c2 = (uint8_t)((eisa >> 21) & 0x1Fu);
    uint8_t c3 = (uint8_t)((eisa >> 16) & 0x1Fu);
    uint16_t prod = (uint16_t)(eisa & 0xFFFFu);
    static const char hx[] = "0123456789ABCDEF";

    out[0] = (char)('@' + c1);
    out[1] = (char)('@' + c2);
    out[2] = (char)('@' + c3);
    out[3] = hx[(prod >> 12) & 0xFu];
    out[4] = hx[(prod >> 8) & 0xFu];
    out[5] = hx[(prod >> 4) & 0xFu];
    out[6] = hx[prod & 0xFu];
    out[7] = 0;
}

static int aml_read_pkglen(const uint8_t *aml, uint32_t aml_len, uint32_t off,
                           uint32_t *pkglen_out, uint32_t *pkglen_bytes_out)
{
    uint8_t lead;
    uint32_t follow_count;
    uint32_t len;

    if (!aml || off >= aml_len || !pkglen_out || !pkglen_bytes_out)
        return -1;

    lead = aml[off];
    follow_count = (uint32_t)((lead >> 6) & 0x3u);
    if (off + 1u + follow_count > aml_len)
        return -1;

    if (follow_count == 0u)
    {
        len = (uint32_t)(lead & 0x3Fu);
    }
    else
    {
        len = (uint32_t)(lead & 0x0Fu);
        for (uint32_t i = 0; i < follow_count; ++i)
            len |= ((uint32_t)aml[off + 1u + i]) << (4u + (8u * i));
    }

    *pkglen_out = len;
    *pkglen_bytes_out = 1u + follow_count;
    return 0;
}

static int aml_parse_namestring_lastseg(const uint8_t *aml,
                                        uint32_t aml_len,
                                        uint32_t off,
                                        uint32_t *consumed_out,
                                        uint8_t out_seg[4])
{
    uint32_t p;
    uint32_t start;
    uint32_t seg_count = 0;
    uint32_t seg_base = 0;

    if (!aml || !consumed_out || !out_seg || off >= aml_len)
        return -1;

    p = off;
    if (aml[p] == '\\')
        p++;
    while (p < aml_len && aml[p] == '^')
        p++;
    if (p >= aml_len)
        return -1;

    start = p;
    if (aml[p] == 0x00u) /* NullName */
    {
        *consumed_out = (p - off) + 1u;
        return -1;
    }
    else if (aml[p] == 0x2Eu) /* DualNamePrefix */
    {
        if (p + 1u + 8u > aml_len)
            return -1;
        seg_count = 2u;
        seg_base = p + 1u;
        p += 1u + 8u;
    }
    else if (aml[p] == 0x2Fu) /* MultiNamePrefix */
    {
        uint32_t n;
        if (p + 2u > aml_len)
            return -1;
        n = (uint32_t)aml[p + 1u];
        if (n == 0u)
            return -1;
        if (p + 2u + (n * 4u) > aml_len)
            return -1;
        seg_count = n;
        seg_base = p + 2u;
        p += 2u + (n * 4u);
    }
    else
    {
        if (p + 4u > aml_len)
            return -1;
        seg_count = 1u;
        seg_base = p;
        p += 4u;
    }

    {
        uint32_t last = seg_base + ((seg_count - 1u) * 4u);
        if (!nameseg_is_valid(aml + last))
            return -1;
        out_seg[0] = aml[last + 0u];
        out_seg[1] = aml[last + 1u];
        out_seg[2] = aml[last + 2u];
        out_seg[3] = aml[last + 3u];
    }

    *consumed_out = (p - off);
    if (*consumed_out == 0u)
        *consumed_out = (p - start);
    return 0;
}

static int aml_parse_integer_const_at(const uint8_t *aml,
                                      uint32_t aml_len,
                                      uint32_t off,
                                      uint64_t *value_out,
                                      uint32_t *consumed_out)
{
    if (!aml || !value_out || !consumed_out || off >= aml_len)
        return -1;

    switch (aml[off])
    {
    case 0x00u: /* ZeroOp */
        *value_out = 0u;
        *consumed_out = 1u;
        return 0;
    case 0x01u: /* OneOp */
        *value_out = 1u;
        *consumed_out = 1u;
        return 0;
    case 0xFFu: /* OnesOp */
        *value_out = ~0ull;
        *consumed_out = 1u;
        return 0;
    case 0x0Au: /* ByteConst */
        if (off + 1u >= aml_len)
            return -1;
        *value_out = (uint64_t)aml[off + 1u];
        *consumed_out = 2u;
        return 0;
    case 0x0Bu: /* WordConst */
        if (off + 2u >= aml_len)
            return -1;
        *value_out = (uint64_t)read_word(aml + off + 1u);
        *consumed_out = 3u;
        return 0;
    case 0x0Cu: /* DWordConst */
        if (off + 4u >= aml_len)
            return -1;
        *value_out = (uint64_t)read_dword(aml + off + 1u);
        *consumed_out = 5u;
        return 0;
    case 0x0Eu: /* QWordConst */
        if (off + 8u >= aml_len)
            return -1;
        *value_out = read_aml_qword(aml + off + 1u);
        *consumed_out = 9u;
        return 0;
    default:
        return -1;
    }
}

static int interesting_named_seed(const char name[5])
{
    return cstr_eq(name, "DID0") ||
           cstr_eq(name, "DID1") ||
           cstr_eq(name, "PVDI") ||
           cstr_eq(name, "PRP5");
}

static int hardcoded_qcom_wifi_nameseg(const char name[5])
{
    return cstr_eq(name, "PCI4") ||
           cstr_eq(name, "RP1_") ||
           cstr_eq(name, "WLN_");
}

static void seed_simple_named_ints_from_table(const acpi_sdt_header_t *tbl)
{
    const uint8_t *aml;
    uint32_t aml_len;
    uint32_t logged = 0;

    if (!tbl || tbl->length < sizeof(acpi_sdt_header_t))
        return;

    aml = (const uint8_t *)(uintptr_t)tbl;
    aml_len = tbl->length;

    for (uint32_t p = sizeof(acpi_sdt_header_t); p + 6u < aml_len; ++p)
    {
        uint32_t consumed_name = 0;
        uint32_t consumed_value = 0;
        uint64_t value = 0;
        uint8_t seg[4];
        char name[5];

        if (aml[p] != 0x08u) /* NameOp */
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, p + 1u, &consumed_name, seg) != 0 || consumed_name == 0u)
            continue;
        if (aml_parse_integer_const_at(aml, aml_len, p + 1u + consumed_name, &value, &consumed_value) != 0)
            continue;

        (void)consumed_value;
        name[0] = (char)seg[0];
        name[1] = (char)seg[1];
        name[2] = (char)seg[2];
        name[3] = (char)seg[3];
        name[4] = 0;

        if (!interesting_named_seed(name))
            continue;

        if (aml_named_set_if_absent(name, value) && logged < 8u)
        {
            terminal_print("[K:ACPI-NET] seed ");
            terminal_print(name);
            terminal_print("=");
            terminal_print_inline_hex64(value);
            logged++;
        }
    }

    /*
      Some PCI children expose DIDx as Field() objects over PCI_Config regions.
      We cannot safely read that config space on this platform yet, but the DSDT
      often compares the field to the expected vendor/device dword. Use that as
      a conservative ACPI-derived seed so simple power methods can make progress.
    */
    for (uint32_t p = sizeof(acpi_sdt_header_t); p + 7u < aml_len; ++p)
    {
        uint32_t consumed_name = 0;
        uint32_t consumed_value = 0;
        uint64_t value = 0;
        uint8_t seg[4];
        char name[5];
        uint32_t name_off;
        uint32_t value_off;

        if (aml[p] != 0x93u) /* LEqualOp */
            continue;

        name_off = p + 1u;
        if (aml_parse_namestring_lastseg(aml, aml_len, name_off, &consumed_name, seg) != 0 || consumed_name == 0u)
            continue;
        value_off = name_off + consumed_name;
        if (aml_parse_integer_const_at(aml, aml_len, value_off, &value, &consumed_value) != 0)
            continue;

        (void)consumed_value;
        name[0] = (char)seg[0];
        name[1] = (char)seg[1];
        name[2] = (char)seg[2];
        name[3] = (char)seg[3];
        name[4] = 0;

        if (!interesting_named_seed(name))
            continue;

        if (aml_named_set_if_absent(name, value) && logged < 8u)
        {
            terminal_print("[K:ACPI-NET] seed cmp ");
            terminal_print(name);
            terminal_print("=");
            terminal_print_inline_hex64(value);
            logged++;
        }
    }
}

static int find_named_buffer_range(const uint8_t *aml,
                                   uint32_t aml_len,
                                   uint32_t scan_start,
                                   uint32_t scan_end,
                                   uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3,
                                   const uint8_t **buf_out,
                                   uint32_t *len_out)
{
    if (!aml || !buf_out || !len_out || scan_end > aml_len || scan_start >= scan_end)
        return -1;

    for (uint32_t t = scan_start; t + 6u < scan_end; ++t)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, bcount = 0;
        uint32_t nsz = 0;
        uint8_t seg[4];
        uint32_t q;

        if (aml[t] != 0x08) /* NameOp */
            continue;

        if (aml_parse_namestring_lastseg(aml, aml_len, t + 1u, &nsz, seg) != 0)
            continue;
        if (!(seg[0] == n0 && seg[1] == n1 && seg[2] == n2 && seg[3] == n3))
            continue;

        q = t + 1u + nsz;
        if (q >= scan_end || aml[q] != 0x11u) /* BufferOp */
            continue;
        q += 1u;
        if (aml_read_pkglen(aml, aml_len, q, &pkglen, &pkglen_bytes) != 0)
            continue;
        q += pkglen_bytes;
        if (q >= scan_end)
            continue;

        if (aml[q] == 0x0A)
        {
            if (q + 1u >= scan_end)
                continue;
            bcount = aml[q + 1u];
            q += 2u;
        }
        else if (aml[q] <= 0x3Fu)
        {
            bcount = aml[q];
            q += 1u;
        }
        else
        {
            continue;
        }

        if (q + bcount > scan_end)
            continue;

        *buf_out = aml + q;
        *len_out = bcount;
        return 0;
    }
    return -1;
}

static int eval_crs_method_buffer(const uint8_t *aml,
                                  uint32_t aml_len,
                                  uint32_t m_body_start,
                                  uint32_t m_body_end,
                                  const uint8_t **buf_out,
                                  uint32_t *len_out);

static int find_crs_buffer_in_body(const uint8_t *aml,
                                   uint32_t aml_len,
                                   uint32_t body_start,
                                   uint32_t body_end,
                                   const uint8_t **buf_out,
                                   uint32_t *len_out)
{
    uint32_t p;

    if (!aml || !buf_out || !len_out)
        return -1;

    for (p = body_start; p + 6u < body_end; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, bcount = 0;
        uint32_t nsz = 0;
        uint8_t seg[4];
        uint32_t q;

        if (!(aml[p] == 0x08 &&
              aml[p + 1u] == '_' &&
              aml[p + 2u] == 'C' &&
              aml[p + 3u] == 'R' &&
              aml[p + 4u] == 'S'))
            continue;

        q = p + 5u;
        if (q >= body_end)
            continue;

        if (aml[q] != 0x11u) /* not direct BufferOp: try NameString reference alias */
        {
            if (aml_parse_namestring_lastseg(aml, aml_len, q, &nsz, seg) != 0)
                continue;
            if (find_named_buffer_range(aml, aml_len, body_start, body_end, seg[0], seg[1], seg[2], seg[3], buf_out, len_out) == 0)
                return 0;
            if (find_named_buffer_range(aml, aml_len, 0u, aml_len, seg[0], seg[1], seg[2], seg[3], buf_out, len_out) == 0)
                return 0;
            continue;
        }

        q += 1u; /* skip BufferOp */
        if (aml_read_pkglen(aml, aml_len, q, &pkglen, &pkglen_bytes) != 0)
            continue;
        q += pkglen_bytes;
        if (q >= body_end)
            continue;

        if (aml[q] == 0x0A)
        {
            if (q + 1u >= body_end)
                continue;
            bcount = aml[q + 1u];
            q += 2u;
        }
        else if (aml[q] <= 0x3Fu)
        {
            bcount = aml[q];
            q += 1u;
        }
        else
        {
            continue;
        }

        if (q + bcount > body_end)
            continue;

        *buf_out = aml + q;
        *len_out = bcount;
        return 0;
    }

    /* Fallback: _CRS defined as MethodOp with an internal named buffer (e.g. RBUF). */
    for (p = body_start; p + 5u < body_end; ++p)
    {
        uint32_t m_pkglen = 0, m_pkglen_bytes = 0;
        uint32_t m_name_off, m_name_len = 0, m_body_start, m_body_end;
        uint8_t m_seg[4];
        uint32_t k;

        if (aml[p] != 0x14) /* MethodOp */
            continue;
        if (aml_read_pkglen(aml, aml_len, p + 1u, &m_pkglen, &m_pkglen_bytes) != 0)
            continue;

        m_name_off = p + 1u + m_pkglen_bytes;
        if (m_name_off + 1u > body_end)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, m_name_off, &m_name_len, m_seg) != 0)
            continue;
        if (!(m_seg[0] == '_' && m_seg[1] == 'C' && m_seg[2] == 'R' && m_seg[3] == 'S'))
            continue;
        if (m_name_off + m_name_len >= body_end)
            continue;

        m_body_start = m_name_off + m_name_len + 1u; /* +name +flags */
        m_body_end = p + 1u + m_pkglen;
        if (m_body_end > body_end)
            m_body_end = body_end;
        if (m_body_end <= m_body_start)
            continue;

        for (k = m_body_start; k + 6u < m_body_end; ++k)
        {
            uint32_t pkglen = 0, pkglen_bytes = 0, bcount = 0;
            uint32_t q;

            if (!(aml[k] == 0x08 &&
                  aml[k + 1u] == 'R' &&
                  aml[k + 2u] == 'B' &&
                  aml[k + 3u] == 'U' &&
                  aml[k + 4u] == 'F' &&
                  aml[k + 5u] == 0x11))
                continue;

            q = k + 6u;
            if (aml_read_pkglen(aml, aml_len, q, &pkglen, &pkglen_bytes) != 0)
                continue;
            q += pkglen_bytes;
            if (q >= m_body_end)
                continue;

            if (aml[q] == 0x0A)
            {
                if (q + 1u >= m_body_end)
                    continue;
                bcount = aml[q + 1u];
                q += 2u;
            }
            else if (aml[q] <= 0x3Fu)
            {
                bcount = aml[q];
                q += 1u;
            }
            else
            {
                continue;
            }

            if (q + bcount > m_body_end)
                continue;

            *buf_out = aml + q;
            *len_out = bcount;
            return 0;
        }

        /* Also handle: Method(_CRS){ Return(NAME) } with NAME buffer defined elsewhere. */
        for (k = m_body_start; k + 5u < m_body_end; ++k)
        {
            uint8_t n0, n1, n2, n3;
            uint32_t s;
            uint32_t seg_count = 0;

            if (aml[k] != 0xA4) /* ReturnOp */
                continue;

            s = k + 1u;
            if (s < m_body_end && aml[s] == '\\')
                s++;
            while (s < m_body_end && aml[s] == '^')
                s++;
            if (s >= m_body_end)
                continue;

            if (aml[s] == 0x2Eu) /* DualNamePrefix */
            {
                if (s + 1u + 8u > m_body_end)
                    continue;
                s += 1u;
                seg_count = 2u;
            }
            else if (aml[s] == 0x2Fu) /* MultiNamePrefix */
            {
                if (s + 2u > m_body_end)
                    continue;
                seg_count = (uint32_t)aml[s + 1u];
                s += 2u;
                if (seg_count == 0u || s + (seg_count * 4u) > m_body_end)
                    continue;
            }
            else
            {
                seg_count = 1u;
                if (s + 4u > m_body_end)
                    continue;
            }

            /* Use the last path segment as the object name to resolve. */
            {
                uint32_t last = s + ((seg_count - 1u) * 4u);
                if (!nameseg_is_valid(aml + last))
                    continue;
                n0 = aml[last + 0u];
                n1 = aml[last + 1u];
                n2 = aml[last + 2u];
                n3 = aml[last + 3u];
            }

            if (find_named_buffer_range(aml, aml_len, body_start, body_end, n0, n1, n2, n3, buf_out, len_out) == 0)
                return 0;
            /* Fallback for method return to a buffer defined outside the local device body. */
            if (find_named_buffer_range(aml, aml_len, 0u, aml_len, n0, n1, n2, n3, buf_out, len_out) == 0)
                return 0;
        }

        /* Runtime fallback: try executing _CRS method with tiny AML engine. */
        if (eval_crs_method_buffer(aml, aml_len, m_body_start, m_body_end, buf_out, len_out) == 0)
            return 0;

        /*
          Heuristic fallback for complex _CRS methods:
          look for a BufferOp in method body that appears to carry a resource
          template (EndTag 0x79 typically present).
        */
        for (k = m_body_start; k + 3u < m_body_end; ++k)
        {
            uint32_t pkglen = 0, pkglen_bytes = 0, bcount = 0;
            uint32_t q;
            uint8_t has_endtag = 0;

            if (aml[k] != 0x11u) /* BufferOp */
                continue;

            q = k + 1u;
            if (aml_read_pkglen(aml, aml_len, q, &pkglen, &pkglen_bytes) != 0)
                continue;
            q += pkglen_bytes;
            if (q >= m_body_end)
                continue;

            if (aml[q] == 0x0A)
            {
                if (q + 1u >= m_body_end)
                    continue;
                bcount = aml[q + 1u];
                q += 2u;
            }
            else if (aml[q] <= 0x3Fu)
            {
                bcount = aml[q];
                q += 1u;
            }
            else
            {
                continue;
            }

            if (bcount == 0u || q + bcount > m_body_end)
                continue;

            for (uint32_t z = 0; z < bcount; ++z)
            {
                if (aml[q + z] == 0x79u)
                {
                    has_endtag = 1u;
                    break;
                }
            }

            if (!has_endtag)
                continue;

            *buf_out = aml + q;
            *len_out = bcount;
            return 0;
        }
    }

    return -1;
}

static int aml_net_read_named_int(void *user, const char *path, uint64_t *out_value)
{
    (void)user;
    if (!out_value)
        return -1;
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PRP5", 4u))
    {
        *out_value = 1u;
        if (g_aml_named_rw_log_count < 32u)
        {
            terminal_print("[K:ACPI-NET] read PRP5 override path=");
            terminal_print(path);
            terminal_print(" value=");
            terminal_print_inline_hex64(*out_value);
            g_aml_named_rw_log_count++;
        }
        return 0;
    }
    if (aml_named_get(path, out_value))
    {
        if (path &&
            (find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID0", 4u) ||
             find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PVDI", 4u)) &&
            g_aml_named_rw_log_count < 32u)
        {
            terminal_print("[K:ACPI-NET] read exact path=");
            terminal_print(path);
            terminal_print(" value=");
            terminal_print_inline_hex64(*out_value);
            g_aml_named_rw_log_count++;
        }
        return 0;
    }
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID0", 4u) &&
        aml_named_get("DID0", out_value))
    {
        if (g_aml_named_rw_log_count < 32u)
        {
            terminal_print("[K:ACPI-NET] read suffix DID0 path=");
            terminal_print(path);
            terminal_print(" value=");
            terminal_print_inline_hex64(*out_value);
            g_aml_named_rw_log_count++;
        }
        return 0;
    }
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID1", 4u) &&
        aml_named_get("DID1", out_value))
    {
        if (g_aml_named_rw_log_count < 32u)
        {
            terminal_print("[K:ACPI-NET] read suffix DID1 path=");
            terminal_print(path);
            terminal_print(" value=");
            terminal_print_inline_hex64(*out_value);
            g_aml_named_rw_log_count++;
        }
        return 0;
    }
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PVDI", 4u) &&
        aml_named_get("PVDI", out_value))
    {
        if (g_aml_named_rw_log_count < 32u)
        {
            terminal_print("[K:ACPI-NET] read suffix PVDI path=");
            terminal_print(path);
            terminal_print(" value=");
            terminal_print_inline_hex64(*out_value);
            g_aml_named_rw_log_count++;
        }
        return 0;
    }
    if (path &&
        (find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID0", 4u) ||
         find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PVDI", 4u)) &&
        g_aml_named_rw_log_count < 32u)
    {
        terminal_print("[K:ACPI-NET] read miss path=");
        terminal_print(path);
        g_aml_named_rw_log_count++;
    }
    return -1;
}

static int aml_net_write_named_int(void *user, const char *path, uint64_t value)
{
    (void)user;
    aml_named_set(path, value);
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID0", 4u))
        aml_named_set("DID0", value);
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID1", 4u))
        aml_named_set("DID1", value);
    if (path && find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PVDI", 4u))
        aml_named_set("PVDI", value);
    if (path &&
        (find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "DID0", 4u) ||
         find_bytes((const uint8_t *)path, cstr_len_bounded(path, 128u), "PVDI", 4u)) &&
        g_aml_named_rw_log_count < 32u)
    {
        terminal_print("[K:ACPI-NET] write path=");
        terminal_print(path);
        terminal_print(" value=");
        terminal_print_inline_hex64(value);
        g_aml_named_rw_log_count++;
    }
    return 0;
}

static void aml_net_log(void *user, const char *msg)
{
    (void)user;
    (void)msg;
}

static int eval_crs_method_buffer(const uint8_t *aml,
                                  uint32_t aml_len,
                                  uint32_t m_body_start,
                                  uint32_t m_body_end,
                                  const uint8_t **buf_out,
                                  uint32_t *len_out)
{
    aml_tiny_method m = {0};
    aml_tiny_host h = {0};
    aml_tiny_value out = {0};
    uint32_t body_len;
    int rc;

    if (!aml || !buf_out || !len_out || m_body_end <= m_body_start || m_body_end > aml_len)
        return -1;

    body_len = m_body_end - m_body_start;
    if (body_len == 0u)
        return -1;

    m.aml = aml + m_body_start;
    m.aml_len = body_len;
    m.scope_prefix = "\\_SB";
    m.arg_count = 0u;
    m.use_typed_args = 0u;

    h.read_named_int = aml_net_read_named_int;
    h.write_named_int = aml_net_write_named_int;
    h.log = aml_net_log;
    h.user = 0;

    rc = aml_tiny_exec_value(&m, &h, &out);
    if (rc != AML_TINY_OK)
        return -1;
    if (out.type != 4u || out.buf_len == 0u)
        return -1;

    g_crs_eval_len = out.buf_len;
    if (g_crs_eval_len > AML_TINY_MAX_BUFFER_BYTES)
        g_crs_eval_len = AML_TINY_MAX_BUFFER_BYTES;
    for (uint32_t i = 0; i < g_crs_eval_len; ++i)
        g_crs_eval_buf[i] = out.buf[i];

    terminal_print("[K:ACPI-NET] _CRS resolved via aml_tiny_exec_value");

    *buf_out = g_crs_eval_buf;
    *len_out = g_crs_eval_len;
    return 0;
}

static int find_crs_buffer_in_scopes_for_device(const uint8_t *aml,
                                                uint32_t aml_len,
                                                const char dev_name[5],
                                                const uint8_t **buf_out,
                                                uint32_t *len_out)
{
    uint32_t p;

    if (!aml || !dev_name || !buf_out || !len_out)
        return -1;

    for (p = 0; p + 2u < aml_len; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, nsz = 0;
        uint32_t ns_off, body_start, body_end;
        uint8_t seg[4];

        if (aml[p] != 0x10u) /* ScopeOp */
            continue;
        if (aml_read_pkglen(aml, aml_len, p + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        ns_off = p + 1u + pkglen_bytes;
        if (ns_off >= aml_len)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, ns_off, &nsz, seg) != 0)
            continue;
        if (!(seg[0] == (uint8_t)dev_name[0] &&
              seg[1] == (uint8_t)dev_name[1] &&
              seg[2] == (uint8_t)dev_name[2] &&
              seg[3] == (uint8_t)dev_name[3]))
            continue;

        body_start = ns_off + nsz;
        body_end = p + 1u + pkglen_bytes + pkglen;
        if (body_end > aml_len)
            body_end = aml_len;
        if (body_start >= body_end)
            continue;

        if (find_crs_buffer_in_body(aml, aml_len, body_start, body_end, buf_out, len_out) == 0)
            return 0;
    }

    return -1;
}

static int should_collect_net_resource(uint32_t hints)
{
    return (hints & (DIHOS_NET_HINT_WWAN |
                     DIHOS_NET_HINT_MHI |
                     DIHOS_NET_HINT_WLAN |
                     DIHOS_NET_HINT_WIFI |
                     DIHOS_NET_HINT_WCN |
                     DIHOS_NET_HINT_MBIM |
                     DIHOS_NET_HINT_QMI |
                     DIHOS_NET_HINT_USB |
                     DIHOS_NET_HINT_SDIO)) != 0u;
}

static uint64_t inclusive_end(uint64_t base, uint64_t len)
{
    if (len == 0u)
        return base;
    if (base + len - 1u < base)
        return ~0ull;
    return base + len - 1u;
}

static void print_resource_bytes(const uint8_t *p, uint32_t len, uint32_t max_len)
{
    uint32_t n = len;
    if (n > max_len)
        n = max_len;
    terminal_print("[K:ACPI-NET] _CRS raw:");
    for (uint32_t i = 0; i < n; ++i)
    {
        terminal_print(" ");
        terminal_print_inline_hex32(p[i]);
    }
}

static int find_method_body_bounds_in_body(const uint8_t *aml,
                                           uint32_t aml_len,
                                           uint32_t body_start,
                                           uint32_t body_end,
                                           const char name4[4],
                                           uint32_t *meth_body_out,
                                           uint32_t *meth_end_out,
                                           uint8_t *flags_out)
{
    if (!aml || !name4 || !meth_body_out || !meth_end_out || !flags_out)
        return -1;

    for (uint32_t p = body_start; p + 5u < body_end; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, name_off, name_len = 0;
        uint32_t flags_off, meth_body, meth_end;
        uint8_t seg[4];

        if (aml[p] != 0x14u) /* MethodOp */
            continue;
        if (aml_read_pkglen(aml, aml_len, p + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = p + 1u + pkglen_bytes;
        if (name_off >= body_end)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, name_off, &name_len, seg) != 0)
            continue;
        if (!(seg[0] == (uint8_t)name4[0] &&
              seg[1] == (uint8_t)name4[1] &&
              seg[2] == (uint8_t)name4[2] &&
              seg[3] == (uint8_t)name4[3]))
            continue;

        flags_off = name_off + name_len;
        meth_body = flags_off + 1u;
        meth_end = p + 1u + pkglen;
        if (meth_end > body_end)
            meth_end = body_end;
        if (flags_off >= body_end || meth_body > meth_end)
            continue;

        *meth_body_out = meth_body;
        *meth_end_out = meth_end;
        *flags_out = aml[flags_off];
        return 0;
    }

    return -1;
}

static int find_method_body_bounds_prefer_nonempty(const uint8_t *aml,
                                                   uint32_t aml_len,
                                                   uint32_t body_start,
                                                   uint32_t body_end,
                                                   const char name4[4],
                                                   uint32_t *meth_body_out,
                                                   uint32_t *meth_end_out,
                                                   uint8_t *flags_out)
{
    uint32_t fallback_body = 0, fallback_end = 0;
    uint8_t fallback_flags = 0;
    int have_fallback = 0;

    if (!aml || !name4 || !meth_body_out || !meth_end_out || !flags_out)
        return -1;

    for (uint32_t p = body_start; p + 5u < body_end; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, name_off, name_len = 0;
        uint32_t flags_off, meth_body, meth_end;
        uint8_t seg[4];

        if (aml[p] != 0x14u)
            continue;
        if (aml_read_pkglen(aml, aml_len, p + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = p + 1u + pkglen_bytes;
        if (name_off >= body_end)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, name_off, &name_len, seg) != 0)
            continue;
        if (!(seg[0] == (uint8_t)name4[0] &&
              seg[1] == (uint8_t)name4[1] &&
              seg[2] == (uint8_t)name4[2] &&
              seg[3] == (uint8_t)name4[3]))
            continue;

        flags_off = name_off + name_len;
        meth_body = flags_off + 1u;
        meth_end = p + 1u + pkglen;
        if (meth_end > body_end)
            meth_end = body_end;
        if (flags_off >= body_end || meth_body > meth_end)
            continue;

        if (!have_fallback)
        {
            fallback_body = meth_body;
            fallback_end = meth_end;
            fallback_flags = aml[flags_off];
            have_fallback = 1;
        }

        if (meth_end > meth_body)
        {
            *meth_body_out = meth_body;
            *meth_end_out = meth_end;
            *flags_out = aml[flags_off];
            return 0;
        }
    }

    if (have_fallback)
    {
        *meth_body_out = fallback_body;
        *meth_end_out = fallback_end;
        *flags_out = fallback_flags;
        return 0;
    }

    return -1;
}

static int body_has_name_literal(const uint8_t *aml,
                                 uint32_t body_start,
                                 uint32_t body_end,
                                 const char name4[4])
{
    if (!aml || !name4)
        return 0;

    for (uint32_t p = body_start; p + 4u <= body_end; ++p)
    {
        if (aml[p + 0u] == (uint8_t)name4[0] &&
            aml[p + 1u] == (uint8_t)name4[1] &&
            aml[p + 2u] == (uint8_t)name4[2] &&
            aml[p + 3u] == (uint8_t)name4[3])
            return 1;
    }

    return 0;
}

static int find_device_body_by_nameseg(const uint8_t *aml,
                                       uint32_t aml_len,
                                       const char name4[4],
                                       uint32_t *body_start_out,
                                       uint32_t *body_end_out,
                                       char *hid_out,
                                       uint32_t hid_cap)
{
    if (!aml || !name4 || !body_start_out || !body_end_out)
        return -1;

    if (hid_out && hid_cap)
        hid_out[0] = 0;

    for (uint32_t i = 0; i + 8u < aml_len; ++i)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0;
        uint32_t name_off, body_start, body_end;

        if (!(aml[i] == 0x5Bu && aml[i + 1u] == 0x82u)) /* DeviceOp */
            continue;
        if (aml_read_pkglen(aml, aml_len, i + 2u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = i + 2u + pkglen_bytes;
        body_start = name_off + 4u;
        if (name_off + 4u > aml_len || pkglen < 4u)
            continue;

        body_end = body_start + (pkglen - 4u);
        if (body_end > aml_len)
            body_end = aml_len;
        if (body_end <= body_start)
            continue;

        if (!(aml[name_off + 0u] == (uint8_t)name4[0] &&
              aml[name_off + 1u] == (uint8_t)name4[1] &&
              aml[name_off + 2u] == (uint8_t)name4[2] &&
              aml[name_off + 3u] == (uint8_t)name4[3]))
            continue;

        *body_start_out = body_start;
        *body_end_out = body_end;

        if (hid_out && hid_cap)
        {
            for (uint32_t p = body_start; p + 7u < body_end; ++p)
            {
                if (!(aml[p] == 0x08 &&
                      aml[p + 1u] == '_' &&
                      aml[p + 2u] == 'H' &&
                      aml[p + 3u] == 'I' &&
                      aml[p + 4u] == 'D'))
                    continue;

                if (aml[p + 5u] == 0x0D)
                {
                    uint32_t k = 0, s = p + 6u;
                    while (s < body_end && aml[s] && k + 1u < hid_cap)
                        hid_out[k++] = (char)aml[s++];
                    hid_out[k] = 0;
                }
                else if (aml[p + 5u] == 0x0C && p + 9u < body_end)
                {
                    uint32_t eisa = read_dword(aml + p + 6u);
                    if (hid_cap >= 8u)
                        decode_eisaid(eisa, hid_out);
                }
                break;
            }
        }

        return 0;
    }

    return -1;
}

static int find_scope_body_by_nameseg(const uint8_t *aml,
                                      uint32_t aml_len,
                                      const char name4[4],
                                      uint32_t *body_start_out,
                                      uint32_t *body_end_out)
{
    if (!aml || !name4 || !body_start_out || !body_end_out)
        return -1;

    for (uint32_t i = 0; i + 2u < aml_len; ++i)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, nsz = 0;
        uint32_t ns_off, body_start, body_end;
        uint8_t seg[4];

        if (aml[i] != 0x10u) /* ScopeOp */
            continue;
        if (aml_read_pkglen(aml, aml_len, i + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        ns_off = i + 1u + pkglen_bytes;
        if (ns_off >= aml_len)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, ns_off, &nsz, seg) != 0)
            continue;
        if (!(seg[0] == (uint8_t)name4[0] &&
              seg[1] == (uint8_t)name4[1] &&
              seg[2] == (uint8_t)name4[2] &&
              seg[3] == (uint8_t)name4[3]))
            continue;

        body_start = ns_off + nsz;
        body_end = i + 1u + pkglen_bytes + pkglen;
        if (body_end > aml_len)
            body_end = aml_len;
        if (body_start >= body_end)
            continue;

        *body_start_out = body_start;
        *body_end_out = body_end;
        return 0;
    }

    return -1;
}

static uint32_t method_presence_mask(const uint8_t *aml, uint32_t aml_len, uint32_t body_start, uint32_t body_end)
{
    uint32_t mask = 0;
    uint32_t mb = 0, me = 0;
    uint8_t flags = 0;
    if (find_method_body_bounds_in_body(aml, aml_len, body_start, body_end, "_STA", &mb, &me, &flags) == 0)
        mask |= 1u << 0;
    if (find_method_body_bounds_in_body(aml, aml_len, body_start, body_end, "_PS0", &mb, &me, &flags) == 0)
        mask |= 1u << 1;
    if (find_method_body_bounds_in_body(aml, aml_len, body_start, body_end, "_INI", &mb, &me, &flags) == 0)
        mask |= 1u << 2;
    if (find_method_body_bounds_in_body(aml, aml_len, body_start, body_end, "_RST", &mb, &me, &flags) == 0)
        mask |= 1u << 3;
    return mask;
}

static void dump_dep_names(const uint8_t *aml,
                           uint32_t aml_len,
                           const char *dev_name,
                           uint32_t body_start,
                           uint32_t body_end)
{
    if (!aml || !dev_name)
        return;

    for (uint32_t p = body_start; p + 7u < body_end; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0;
        uint32_t q;
        uint32_t count;

        if (!(aml[p] == 0x08u &&
              aml[p + 1u] == '_' &&
              aml[p + 2u] == 'D' &&
              aml[p + 3u] == 'E' &&
              aml[p + 4u] == 'P' &&
              aml[p + 5u] == 0x12u)) /* Name(_DEP, Package(...)) */
            continue;

        q = p + 6u;
        if (aml_read_pkglen(aml, aml_len, q, &pkglen, &pkglen_bytes) != 0)
            continue;
        q += pkglen_bytes;
        if (q >= body_end)
            continue;

        count = (uint32_t)aml[q++];
        terminal_print("[K:ACPI-NET] _DEP dev=");
        terminal_print(dev_name);
        terminal_print(" count=");
        terminal_print_inline_hex32(count);

        for (uint32_t i = 0; i < count && q < body_end; ++i)
        {
            uint32_t consumed = 0;
            uint8_t seg[4];
            char dep_name[5];

            if (aml_parse_namestring_lastseg(aml, aml_len, q, &consumed, seg) != 0 || consumed == 0u)
                break;

            dep_name[0] = (char)seg[0];
            dep_name[1] = (char)seg[1];
            dep_name[2] = (char)seg[2];
            dep_name[3] = (char)seg[3];
            dep_name[4] = 0;

            terminal_print("[K:ACPI-NET] _DEP ");
            terminal_print(dev_name);
            terminal_print(" -> ");
            terminal_print(dep_name);
            {
                uint32_t dep_body_start = 0, dep_body_end = 0;
                char dep_hid[17];
                if (find_device_body_by_nameseg(aml, aml_len, dep_name, &dep_body_start, &dep_body_end, dep_hid, sizeof(dep_hid)) == 0)
                {
                    uint32_t mp = method_presence_mask(aml, aml_len, dep_body_start, dep_body_end);
                    terminal_print(" hid=");
                    terminal_print(dep_hid[0] ? dep_hid : "(none)");
                    terminal_print(" meth=");
                    terminal_print_inline_hex32(mp);
                }
            }

            q += consumed;
        }

        return;
    }
}

static void print_aml_bytes(const char *tag, const uint8_t *aml, uint32_t start, uint32_t end, uint32_t max_len)
{
    uint32_t n;
    if (!tag || !aml || end <= start)
        return;

    n = end - start;
    if (n > max_len)
        n = max_len;

    terminal_print(tag);
    for (uint32_t i = 0; i < n; ++i)
    {
        terminal_print(" ");
        terminal_print_inline_hex32(aml[start + i]);
    }
}

static void dump_symbol_contexts_range(const uint8_t *aml,
                                       uint32_t aml_len,
                                       uint32_t start,
                                       uint32_t end,
                                       const char sym4[4],
                                       uint32_t max_hits)
{
    uint32_t hits = 0;

    if (!aml || !sym4 || end > aml_len || start >= end)
        return;

    for (uint32_t p = start; p + 4u <= end && hits < max_hits; ++p)
    {
        char symz[5];
        if (!(aml[p + 0u] == (uint8_t)sym4[0] &&
              aml[p + 1u] == (uint8_t)sym4[1] &&
              aml[p + 2u] == (uint8_t)sym4[2] &&
              aml[p + 3u] == (uint8_t)sym4[3]))
            continue;

        symz[0] = sym4[0];
        symz[1] = sym4[1];
        symz[2] = sym4[2];
        symz[3] = sym4[3];
        symz[4] = 0;

        terminal_print("[K:ACPI-NET] symbol ");
        terminal_print(symz);
        terminal_print(" off=");
        terminal_print_inline_hex32(p);
        print_aml_bytes(" ctx:", aml, (p >= 16u) ? (p - 16u) : start,
                        ((p + 32u) < end) ? (p + 32u) : end, 48u);
        hits++;
    }
}

static void dump_all_methods_in_body(const uint8_t *aml,
                                     uint32_t aml_len,
                                     const char *dev_name,
                                     uint32_t body_start,
                                     uint32_t body_end)
{
    uint32_t logged = 0;

    if (!aml || !dev_name)
        return;

    for (uint32_t p = body_start; p + 5u < body_end && logged < 32u; ++p)
    {
        uint32_t pkglen = 0, pkglen_bytes = 0, name_off, name_len = 0;
        uint32_t flags_off, meth_body, meth_end;
        uint8_t seg[4];
        char name[5];

        if (aml[p] != 0x14u)
            continue;
        if (aml_read_pkglen(aml, aml_len, p + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = p + 1u + pkglen_bytes;
        if (name_off >= body_end)
            continue;
        if (aml_parse_namestring_lastseg(aml, aml_len, name_off, &name_len, seg) != 0)
            continue;

        flags_off = name_off + name_len;
        meth_body = flags_off + 1u;
        meth_end = p + 1u + pkglen;
        if (flags_off >= body_end || meth_body > body_end)
            continue;
        if (meth_end > body_end)
            meth_end = body_end;

        name[0] = (char)seg[0];
        name[1] = (char)seg[1];
        name[2] = (char)seg[2];
        name[3] = (char)seg[3];
        name[4] = 0;

        terminal_print("[K:ACPI-NET] method-any ");
        terminal_print(dev_name);
        terminal_print(".");
        terminal_print(name);
        terminal_print(" args=");
        terminal_print_inline_hex32((uint32_t)(aml[flags_off] & 0x07u));
        terminal_print(" len=");
        terminal_print_inline_hex32(meth_end - meth_body);
        logged++;
    }
}

static void dump_focus_device_methods(const uint8_t *aml,
                                      uint32_t aml_len,
                                      const char *dev_name,
                                      const char *hid_name,
                                      uint32_t body_start,
                                      uint32_t body_end)
{
    static const char *methods[] = {"_STA", "_PS0", "PVD5", "_PSC", "_INI", "_RST", "_PS3", "_PS1", "_PS2"};
    int focus = 0;

    if (!aml || !dev_name || !hid_name)
        return;

    if (memeq_n(dev_name, "SDC2", 4) || memeq_n(hid_name, "QCOM2466", 8))
        focus = 1;
    if (memeq_n(dev_name, "PCI5", 4) || memeq_n(dev_name, "WWAN", 4))
        focus = 1;
    if (hardcoded_qcom_wifi_nameseg(dev_name))
        focus = 1;
    if (!focus)
        return;

    terminal_print("[K:ACPI-NET] method scan dev=");
    terminal_print(dev_name);
    terminal_print(" hid=");
    terminal_print(hid_name);
    terminal_print(" body_len=");
    terminal_print_inline_hex32(body_end - body_start);
    terminal_print(" has _PR0=");
    terminal_print_inline_hex32((uint32_t)body_has_name_literal(aml, body_start, body_end, "_PR0"));
    terminal_print(" _PR3=");
    terminal_print_inline_hex32((uint32_t)body_has_name_literal(aml, body_start, body_end, "_PR3"));
    terminal_print(" _DEP=");
    terminal_print_inline_hex32((uint32_t)body_has_name_literal(aml, body_start, body_end, "_DEP"));
    dump_dep_names(aml, aml_len, dev_name, body_start, body_end);
    dump_all_methods_in_body(aml, aml_len, dev_name, body_start, body_end);

    for (uint32_t i = 0; i < (uint32_t)(sizeof(methods) / sizeof(methods[0])); ++i)
    {
        uint32_t meth_body = 0, meth_end = 0;
        uint8_t flags = 0;
        if (find_method_body_bounds_prefer_nonempty(aml, aml_len, body_start, body_end,
                                                    methods[i], &meth_body, &meth_end, &flags) != 0)
            continue;

        terminal_print("[K:ACPI-NET] method ");
        terminal_print(dev_name);
        terminal_print(".");
        terminal_print(methods[i]);
        terminal_print(" args=");
        terminal_print_inline_hex32((uint32_t)(flags & 0x07u));
        terminal_print(" len=");
        terminal_print_inline_hex32(meth_end - meth_body);
        print_aml_bytes(" body:", aml, meth_body, meth_end, 64u);
    }

    print_aml_bytes("[K:ACPI-NET] focus body head:", aml, body_start, body_end, 96u);
}

static void dump_crs_summary(const char *dev_name,
                             const char *hid_name,
                             const uint8_t *buf,
                             uint32_t len,
                             uint32_t hints)
{
    uint32_t rem = len;
    const uint8_t *p = buf;
    uint32_t items = 0;
    int collect_resource = should_collect_net_resource(hints);

    terminal_print("[K:ACPI-NET] _CRS dev=");
    terminal_print(dev_name);
    terminal_print(" hid=");
    terminal_print(hid_name);
    terminal_print(" len=");
    terminal_print_inline_hex32(len);

    while (rem > 0u && items < 48u)
    {
        uint8_t tag = p[0];
        if (tag == 0x79u)
        {
            terminal_print("[K:ACPI-NET] _CRS end-tag");
            break;
        }

        if (tag & 0x80u)
        {
            uint8_t item = (uint8_t)(tag & 0x7Fu);
            uint16_t size;
            if (rem < 3u)
                break;
            size = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
            if (rem < (uint32_t)(3u + size))
                break;

            terminal_print("[K:ACPI-NET] _CRS large item=");
            terminal_print_inline_hex32(item);
            terminal_print(" size=");
            terminal_print_inline_hex32(size);

            if (item == 0x06u && size >= 9u)
            {
                const uint8_t *d = p + 3u;
                uint8_t rw = d[0];
                uint32_t base = read_dword(d + 1u);
                uint32_t rlen = read_dword(d + 5u);
                terminal_print("[K:ACPI-NET] _CRS fixed32-rw=");
                terminal_print_inline_hex32(rw);
                terminal_print(" base=");
                terminal_print_inline_hex32(base);
                terminal_print(" len=");
                terminal_print_inline_hex32(rlen);
                if (collect_resource && rlen)
                    net_res_add(3u, rw, (uint64_t)base, inclusive_end((uint64_t)base, (uint64_t)rlen),
                                (uint64_t)rlen, dev_name, hid_name);
            }
            else if (item == 0x05u && size >= 17u)
            {
                const uint8_t *d = p + 3u;
                uint8_t rw = d[0];
                uint32_t min = read_dword(d + 1u);
                uint32_t max = read_dword(d + 5u);
                uint32_t align = read_dword(d + 9u);
                uint32_t rlen = read_dword(d + 13u);
                terminal_print("[K:ACPI-NET] _CRS mem32-rw=");
                terminal_print_inline_hex32(rw);
                terminal_print(" min=");
                terminal_print_inline_hex32(min);
                terminal_print(" max=");
                terminal_print_inline_hex32(max);
                terminal_print(" align=");
                terminal_print_inline_hex32(align);
                terminal_print(" len=");
                terminal_print_inline_hex32(rlen);
                if (collect_resource && rlen)
                    net_res_add(4u, rw, (uint64_t)min, (uint64_t)max, (uint64_t)rlen, dev_name, hid_name);
            }
            else if (item == 0x07u && size >= 23u)
            {
                const uint8_t *d = p + 3u;
                uint8_t rtype = d[0];
                uint32_t min = read_dword(d + 7u);
                uint32_t max = read_dword(d + 11u);
                uint32_t rlen = read_dword(d + 19u);
                terminal_print("[K:ACPI-NET] _CRS dword-rtype=");
                terminal_print_inline_hex32(rtype);
                terminal_print(" min=");
                terminal_print_inline_hex32(min);
                terminal_print(" max=");
                terminal_print_inline_hex32(max);
                terminal_print(" len=");
                terminal_print_inline_hex32(rlen);
                if (collect_resource && rlen)
                    net_res_add(1u, rtype, (uint64_t)min, (uint64_t)max, (uint64_t)rlen, dev_name, hid_name);
            }
            else if (item == 0x08u && size >= 13u)
            {
                const uint8_t *d = p + 3u;
                uint8_t rtype = d[0];
                uint16_t min = read_word(d + 5u);
                uint16_t max = read_word(d + 7u);
                uint16_t rlen = read_word(d + 11u);
                terminal_print("[K:ACPI-NET] _CRS word-rtype=");
                terminal_print_inline_hex32(rtype);
                terminal_print(" min=");
                terminal_print_inline_hex32(min);
                terminal_print(" max=");
                terminal_print_inline_hex32(max);
                terminal_print(" len=");
                terminal_print_inline_hex32(rlen);
                if (collect_resource && rlen)
                    net_res_add(2u, rtype, (uint64_t)min, (uint64_t)max, (uint64_t)rlen, dev_name, hid_name);
            }
            else if (item == 0x09u && size >= 6u)
            {
                const uint8_t *d = p + 3u;
                uint8_t flags = d[0];
                uint8_t count = d[1];
                uint32_t irq0 = (count && size >= 6u) ? read_dword(d + 2u) : 0u;
                terminal_print("[K:ACPI-NET] _CRS irq flags=");
                terminal_print_inline_hex32(flags);
                terminal_print(" count=");
                terminal_print_inline_hex32(count);
                terminal_print(" irq0=");
                terminal_print_inline_hex32(irq0);
            }
            else if (item == 0x0Au && size >= 43u)
            {
                const uint8_t *d = p + 3u;
                uint8_t rtype = d[0];
                uint64_t min = read_qword(d + 11u);
                uint64_t max = read_qword(d + 19u);
                uint64_t rlen = read_qword(d + 35u);
                terminal_print("[K:ACPI-NET] _CRS qword-rtype=");
                terminal_print_inline_hex32(rtype);
                terminal_print(" min=");
                terminal_print_inline_hex64(min);
                terminal_print(" max=");
                terminal_print_inline_hex64(max);
                terminal_print(" len=");
                terminal_print_inline_hex64(rlen);
                if (collect_resource && rlen)
                    net_res_add(5u, rtype, min, max, rlen, dev_name, hid_name);
            }
            else if (item == 0x0Cu && size >= 12u)
            {
                const uint8_t *d = p + 3u;
                uint32_t total = (uint32_t)(3u + size);
                uint8_t rev = d[0];
                uint8_t conn = d[1];
                uint16_t flags = (size >= 6u) ? read_word(d + 4u) : 0u;
                uint8_t pin_cfg = (size >= 7u) ? d[6] : 0u;
                uint16_t pin_table_off = (size >= 13u) ? read_word(d + 11u) : 0u;
                uint16_t source_off = (size >= 16u) ? read_word(d + 14u) : 0u;
                uint16_t first_pin = 0u;
                if (pin_table_off != 0u && (uint32_t)pin_table_off + 1u < total)
                    first_pin = read_word(p + pin_table_off);
                terminal_print("[K:ACPI-NET] _CRS gpio rev=");
                terminal_print_inline_hex32(rev);
                terminal_print(" conn=");
                terminal_print_inline_hex32(conn);
                terminal_print(" flags=");
                terminal_print_inline_hex32(flags);
                terminal_print(" cfg=");
                terminal_print_inline_hex32(pin_cfg);
                terminal_print(" pin_table=");
                terminal_print_inline_hex32(pin_table_off);
                terminal_print(" src=");
                terminal_print_inline_hex32(source_off);
                terminal_print(" pin0=");
                terminal_print_inline_hex32(first_pin);
                if (collect_resource && first_pin)
                    net_gpio_add(conn, flags, pin_cfg, first_pin, dev_name, hid_name);
                print_resource_bytes(p, (uint32_t)(3u + size), 32u);
            }

            p += (uint32_t)(3u + size);
            rem -= (uint32_t)(3u + size);
            items++;
            continue;
        }
        else
        {
            uint8_t stype = (uint8_t)((tag >> 3) & 0x0Fu);
            uint8_t ssize = (uint8_t)(tag & 0x07u);
            if (ssize == 0x07u)
                ssize = 2u;
            if (rem < (uint32_t)(1u + ssize))
                break;

            terminal_print("[K:ACPI-NET] _CRS small type=");
            terminal_print_inline_hex32(stype);
            terminal_print(" size=");
            terminal_print_inline_hex32(ssize);

            p += (uint32_t)(1u + ssize);
            rem -= (uint32_t)(1u + ssize);
            items++;
        }
    }
}

static uint32_t scan_dsdt_device_candidates(const acpi_sdt_header_t *dsdt_hdr)
{
    const uint8_t *aml = (const uint8_t *)(uintptr_t)dsdt_hdr;
    uint32_t aml_len = dsdt_hdr->length;
    uint32_t logged_qcom = 0;
    uint32_t logged_wmhi = 0;
    uint32_t found_hints = 0;

    terminal_print("[K:ACPI-NET] DSDT device candidate scan");

    for (uint32_t i = 0; i + 8u < aml_len; ++i)
    {
        uint32_t pkglen = 0;
        uint32_t pkglen_bytes = 0;
        uint32_t name_off;
        uint32_t body_start;
        uint32_t body_end;
        char name[5];
        char hid_str[17];
        int has_hid = 0;
        uint32_t local_hints = 0;
        uint64_t adr_val = 0;
        int has_adr = 0;

        if (aml[i] != 0x5Bu || aml[i + 1u] != 0x82u)
            continue;

        if (aml_read_pkglen(aml, aml_len, i + 2u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = i + 2u + pkglen_bytes;
        body_start = name_off + 4u;
        if (name_off + 4u > aml_len || pkglen < 4u)
            continue;

        body_end = body_start + (pkglen - 4u);
        if (body_end > aml_len)
            body_end = aml_len;
        if (body_end <= body_start)
            continue;

        name[0] = (char)aml[name_off + 0u];
        name[1] = (char)aml[name_off + 1u];
        name[2] = (char)aml[name_off + 2u];
        name[3] = (char)aml[name_off + 3u];
        name[4] = 0;

        if (!nameseg_is_valid(aml + name_off))
            continue;

        if (find_bytes(aml + body_start, body_end - body_start, "WWAN", 4))
            local_hints |= DIHOS_NET_HINT_WWAN;
        if (find_bytes(aml + body_start, body_end - body_start, "WLAN", 4))
            local_hints |= DIHOS_NET_HINT_WLAN;
        if (find_bytes(aml + body_start, body_end - body_start, "WIFI", 4))
            local_hints |= DIHOS_NET_HINT_WIFI;
        if (find_bytes(aml + body_start, body_end - body_start, "WCN", 3))
            local_hints |= DIHOS_NET_HINT_WCN;
        if (find_bytes(aml + body_start, body_end - body_start, "SDIO", 4))
            local_hints |= DIHOS_NET_HINT_SDIO;
        if (find_bytes(aml + body_start, body_end - body_start, "QCOM", 4))
            local_hints |= DIHOS_NET_HINT_QCOM;
        if (find_bytes(aml + body_start, body_end - body_start, "MHI", 3))
            local_hints |= DIHOS_NET_HINT_MHI;
        if (find_bytes(aml + body_start, body_end - body_start, "MBIM", 4))
            local_hints |= DIHOS_NET_HINT_MBIM;
        if (find_bytes(aml + body_start, body_end - body_start, "QMI", 3))
            local_hints |= DIHOS_NET_HINT_QMI;

        hid_str[0] = 0;
        for (uint32_t p = body_start; p + 7u < body_end; ++p)
        {
            if (aml[p] == 0x08 &&
                aml[p + 1u] == '_' &&
                aml[p + 2u] == 'H' &&
                aml[p + 3u] == 'I' &&
                aml[p + 4u] == 'D')
            {
                if (aml[p + 5u] == 0x0D)
                {
                    uint32_t k = 0;
                    uint32_t s = p + 6u;
                    while (s < body_end && aml[s] && k < 16u)
                        hid_str[k++] = (char)aml[s++];
                    hid_str[k] = 0;
                    has_hid = (k != 0u);
                }
                else if (aml[p + 5u] == 0x0C && p + 9u < body_end)
                {
                    uint32_t eisa = read_dword(aml + p + 6u);
                    decode_eisaid(eisa, hid_str);
                    has_hid = 1;
                }
                break;
            }
        }

        if (memeq_n(name, "SDC2", 4) || memeq_n(hid_str, "QCOM2466", 8))
            local_hints |= DIHOS_NET_HINT_SDIO;
        if (hardcoded_qcom_wifi_nameseg(name))
        {
            local_hints |= DIHOS_NET_HINT_QCOM | DIHOS_NET_HINT_WLAN;
            if (!has_hid)
            {
                copy_namez(hid_str, sizeof(hid_str), "PCI-QCOM-WIFI");
                has_hid = 1;
            }
        }

        for (uint32_t p = body_start; p + 6u < body_end; ++p)
        {
            if (!(aml[p] == 0x08 &&
                  aml[p + 1u] == '_' &&
                  aml[p + 2u] == 'A' &&
                  aml[p + 3u] == 'D' &&
                  aml[p + 4u] == 'R'))
                continue;

            if (aml[p + 5u] == 0x0A && p + 6u < body_end)
            {
                adr_val = (uint64_t)aml[p + 6u];
                has_adr = 1;
            }
            else if (aml[p + 5u] == 0x0B && p + 7u < body_end)
            {
                adr_val = (uint64_t)read_word(aml + p + 6u);
                has_adr = 1;
            }
            else if (aml[p + 5u] == 0x0C && p + 9u < body_end)
            {
                adr_val = (uint64_t)read_dword(aml + p + 6u);
                has_adr = 1;
            }
            if (has_adr)
                break;
        }

        if (local_hints == 0u || !has_hid)
            continue;
        found_hints |= local_hints;
        if ((local_hints & (DIHOS_NET_HINT_WWAN |
                            DIHOS_NET_HINT_MHI |
                            DIHOS_NET_HINT_WLAN |
                            DIHOS_NET_HINT_WIFI |
                            DIHOS_NET_HINT_WCN |
                            DIHOS_NET_HINT_SDIO)) != 0u)
        {
            if (logged_wmhi >= 32u)
                continue;
            logged_wmhi++;
        }
        else
        {
            if (logged_qcom >= 12u)
                continue;
            logged_qcom++;
        }

        terminal_print("[K:ACPI-NET] DSDT dev=");
        terminal_print(name);
        terminal_print(" hid=");
        terminal_print(hid_str);
        terminal_print(" off=");
        terminal_print_inline_hex32(i);
        terminal_print(" body=");
        terminal_print_inline_hex32(body_start);
        terminal_print("-");
        terminal_print_inline_hex32(body_end);
        terminal_print(" hints=");
        terminal_print_inline_hex32(local_hints);
        if (has_adr)
        {
            terminal_print(" _ADR=");
            terminal_print_inline_hex64(adr_val);
        }
        dump_focus_device_methods(aml, aml_len, name, hid_str, body_start, body_end);

        if ((local_hints & (DIHOS_NET_HINT_WWAN |
                            DIHOS_NET_HINT_MHI |
                            DIHOS_NET_HINT_WLAN |
                            DIHOS_NET_HINT_WIFI |
                            DIHOS_NET_HINT_WCN |
                            DIHOS_NET_HINT_SDIO |
                            DIHOS_NET_HINT_QCOM)) != 0u)
        {
            const uint8_t *crs_buf = 0;
            uint32_t crs_len = 0;
            if (find_crs_buffer_in_body(aml, aml_len, body_start, body_end, &crs_buf, &crs_len) == 0)
            {
                dump_crs_summary(name, hid_str, crs_buf, crs_len, local_hints);
            }
            else if (find_crs_buffer_in_scopes_for_device(aml, aml_len, name, &crs_buf, &crs_len) == 0)
            {
                terminal_print("[K:ACPI-NET] _CRS resolved via Scope() fallback");
                dump_crs_summary(name, hid_str, crs_buf, crs_len, local_hints);
            }
            else
                terminal_print("[K:ACPI-NET] _CRS not found in candidate body");
        }
    }

    return found_hints;
}

static uint32_t scan_table_for_net_markers(const acpi_sdt_header_t *hdr)
{
    static const char *markers[] = {
        "PNP0A08", "PNP0A03",
        "WLAN", "WWAN", "WIFI",
        "WLN_",
        "QCOM", "WCN", "ATH", "BCM", "RTL", "MTK", "MRVL", "INTC",
        "MHI", "RMNET", "QMI", "MBIM", "RNDIS", "NCM", "CNVW",
        "SDIO", "PCIE", "USB", "NET", "ETH"
    };
    const uint8_t *p = (const uint8_t *)(uintptr_t)hdr;
    char sig[5];
    uint32_t hints = 0;

    copy_sig4(sig, hdr->signature);

    terminal_print("[K:ACPI-NET] scan table ");
    terminal_print(sig);
    terminal_print(" len=");
    terminal_print_inline_hex32(hdr->length);

    for (uint32_t i = 0; i < (uint32_t)(sizeof(markers) / sizeof(markers[0])); ++i)
    {
        const char *m = markers[i];
        uint32_t mlen = 0;
        while (m[mlen])
            ++mlen;

        if (find_bytes(p, hdr->length, m, mlen))
        {
            terminal_print("[K:ACPI-NET] ");
            terminal_print(sig);
            terminal_print(" marker ");
            terminal_print(m);

            if (memeq_n(m, "QCOM", 4))
                hints |= DIHOS_NET_HINT_QCOM;
            else if (memeq_n(m, "WLAN", 4))
                hints |= DIHOS_NET_HINT_WLAN;
            else if (memeq_n(m, "WLN_", 4))
                hints |= DIHOS_NET_HINT_WLAN;
            else if (memeq_n(m, "WIFI", 4))
                hints |= DIHOS_NET_HINT_WIFI;
            else if (memeq_n(m, "WCN", 3))
                hints |= DIHOS_NET_HINT_WCN;
            else if (memeq_n(m, "SDIO", 4))
                hints |= DIHOS_NET_HINT_SDIO;
            else if (memeq_n(m, "WWAN", 4))
                hints |= DIHOS_NET_HINT_WWAN;
            else if (memeq_n(m, "MHI", 3))
                hints |= DIHOS_NET_HINT_MHI;
            else if (memeq_n(m, "MBIM", 4))
                hints |= DIHOS_NET_HINT_MBIM;
            else if (memeq_n(m, "QMI", 3))
                hints |= DIHOS_NET_HINT_QMI;
            else if (memeq_n(m, "USB", 3))
                hints |= DIHOS_NET_HINT_USB;
        }
    }

    return hints;
}

static const acpi_sdt_header_t *find_root(const acpi_rsdp_t *rsdp)
{
    if (!rsdp)
        return 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address)
        return (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
    if (rsdp->rsdt_address)
        return (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)rsdp->rsdt_address;
    return 0;
}

static int exec_device_method_in_table(const acpi_sdt_header_t *tbl,
                                       const char dev_name4[4],
                                       const char method_name4[4],
                                       uint8_t arg_count,
                                       const uint64_t *args,
                                       uint64_t *ret_out,
                                       int *ran_out)
{
    const uint8_t *aml;
    uint32_t aml_len;
    uint32_t body_start = 0, body_end = 0;
    uint32_t meth_body = 0, meth_end = 0;
    uint8_t flags = 0;
    aml_tiny_method m = {0};
    aml_tiny_host h = {0};
    uint64_t ret = 0;
    int rc;

    if (!tbl || !dev_name4 || !method_name4 || !ran_out)
        return -1;
    *ran_out = 0;

    aml = (const uint8_t *)(uintptr_t)tbl;
    aml_len = tbl->length;
    if (aml_len < sizeof(acpi_sdt_header_t))
        return -1;

    if (find_device_body_by_nameseg(aml, aml_len, dev_name4, &body_start, &body_end, 0, 0) != 0)
    {
        if (find_scope_body_by_nameseg(aml, aml_len, dev_name4, &body_start, &body_end) != 0)
            return -1;
    }
    if (find_method_body_bounds_prefer_nonempty(aml, aml_len, body_start, body_end, method_name4, &meth_body, &meth_end, &flags) != 0)
        return -1;
    if (meth_end < meth_body)
        return -1;

    if (meth_end == meth_body)
    {
        if (ret_out)
            *ret_out = 0u;
        *ran_out = 1;
        {
            char devz[5];
            char methz[5];
            devz[0] = dev_name4[0];
            devz[1] = dev_name4[1];
            devz[2] = dev_name4[2];
            devz[3] = dev_name4[3];
            devz[4] = 0;
            methz[0] = method_name4[0];
            methz[1] = method_name4[1];
            methz[2] = method_name4[2];
            methz[3] = method_name4[3];
            methz[4] = 0;
            terminal_print("[K:ACPI-NET] exec ");
            terminal_print(devz);
            terminal_print(".");
            terminal_print(methz);
            terminal_print(" len=0x00000000 rc=0 (empty body)");
        }
        return 0;
    }

    m.aml = aml + meth_body;
    m.aml_len = meth_end - meth_body;
    m.scope_prefix = "\\_SB";
    m.arg_count = (uint8_t)(flags & 0x07u);
    if (m.arg_count > 7u)
        m.arg_count = 7u;
    for (uint32_t i = 0; i < (uint32_t)m.arg_count; ++i)
        m.args[i] = (i < (uint32_t)arg_count && args) ? args[i] : 0u;
    m.use_typed_args = 0u;

    h.read_named_int = aml_net_read_named_int;
    h.write_named_int = aml_net_write_named_int;
    h.log = aml_net_log;
    h.user = 0;

    seed_simple_named_ints_from_table(tbl);
    rc = aml_tiny_exec(&m, &h, &ret);
    *ran_out = 1;
    if (ret_out)
        *ret_out = ret;

    {
        char devz[5];
        char methz[5];
        devz[0] = dev_name4[0];
        devz[1] = dev_name4[1];
        devz[2] = dev_name4[2];
        devz[3] = dev_name4[3];
        devz[4] = 0;
        methz[0] = method_name4[0];
        methz[1] = method_name4[1];
        methz[2] = method_name4[2];
        methz[3] = method_name4[3];
        methz[4] = 0;
        terminal_print("[K:ACPI-NET] exec ");
        terminal_print(devz);
        terminal_print(".");
        terminal_print(methz);
    }
    terminal_print(" len=");
    terminal_print_inline_hex64((uint64_t)(meth_end - meth_body));
    terminal_print(" rc=");
    terminal_print_inline_hex64((uint64_t)(int64_t)rc);
    terminal_print(" argc=");
    terminal_print_inline_hex64((uint64_t)(flags & 0x07u));
    terminal_print(" ret=");
    terminal_print_inline_hex64(ret);
    return rc;
}

uint32_t acpi_probe_net_candidates_from_rsdp(uint64_t rsdp_phys)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;
    int scanned_dsdt = 0;
    uint32_t scanned_ssdt = 0;
    uint32_t hints = 0;
    net_res_reset();

    terminal_print("[K:ACPI-NET] probe start");

    if (!rsdp_phys)
    {
        terminal_print("[K:ACPI-NET] no RSDP");
        return 0;
    }

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;
    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
    {
        terminal_print("[K:ACPI-NET] bad RSDP sig");
        return 0;
    }

    root = find_root(rsdp);
    if (!root || root->length < sizeof(acpi_sdt_header_t))
    {
        terminal_print("[K:ACPI-NET] no valid root table");
        return 0;
    }

    if (memeq_n(root->signature, "XSDT", 4))
    {
        const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
        uint32_t n = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8u;

        terminal_print("[K:ACPI-NET] root=XSDT entries=");
        terminal_print_inline_hex32(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)ptrs[i];
            if (!hdr || hdr->length < sizeof(acpi_sdt_header_t))
                continue;

            if (!(memeq_n(hdr->signature, "FACP", 4) ||
                  memeq_n(hdr->signature, "DSDT", 4) ||
                  memeq_n(hdr->signature, "SSDT", 4) ||
                  memeq_n(hdr->signature, "MCFG", 4)))
                continue;

            hints |= scan_table_for_net_markers(hdr);

            if (memeq_n(hdr->signature, "FACP", 4))
            {
                const uint8_t *fadt = (const uint8_t *)(uintptr_t)hdr;
                uint64_t xdsdt = (hdr->length >= 148u) ? read_qword(fadt + 140u) : 0ull;
                uint64_t dsdt = (hdr->length >= 44u) ? (uint64_t)read_dword(fadt + 40u) : 0ull;
                const acpi_sdt_header_t *dsdt_hdr =
                    (const acpi_sdt_header_t *)(uintptr_t)(xdsdt ? xdsdt : dsdt);

                if (!scanned_dsdt && dsdt_hdr && dsdt_hdr->length >= sizeof(acpi_sdt_header_t))
                {
                    hints |= scan_table_for_net_markers(dsdt_hdr);
                    hints |= scan_dsdt_device_candidates(dsdt_hdr);
                    scanned_dsdt = 1;
                }
            }
            else if (memeq_n(hdr->signature, "SSDT", 4))
            {
                if (scanned_ssdt < 8u)
                {
                    hints |= scan_dsdt_device_candidates(hdr);
                    scanned_ssdt++;
                }
            }
            else if (memeq_n(hdr->signature, "DSDT", 4))
            {
                if (!scanned_dsdt)
                {
                    hints |= scan_dsdt_device_candidates(hdr);
                    scanned_dsdt = 1;
                }
            }
        }
    }
    else if (memeq_n(root->signature, "RSDT", 4))
    {
        const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
        uint32_t n = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4u;

        terminal_print("[K:ACPI-NET] root=RSDT entries=");
        terminal_print_inline_hex32(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)ptrs[i];
            if (!hdr || hdr->length < sizeof(acpi_sdt_header_t))
                continue;

            if (!(memeq_n(hdr->signature, "FACP", 4) ||
                  memeq_n(hdr->signature, "DSDT", 4) ||
                  memeq_n(hdr->signature, "SSDT", 4) ||
                  memeq_n(hdr->signature, "MCFG", 4)))
                continue;

            hints |= scan_table_for_net_markers(hdr);
            if (memeq_n(hdr->signature, "SSDT", 4))
            {
                if (scanned_ssdt < 8u)
                {
                    hints |= scan_dsdt_device_candidates(hdr);
                    scanned_ssdt++;
                }
            }
            else if (memeq_n(hdr->signature, "DSDT", 4))
            {
                if (!scanned_dsdt)
                {
                    hints |= scan_dsdt_device_candidates(hdr);
                    scanned_dsdt = 1;
                }
            }
        }
    }
    else
    {
        terminal_print("[K:ACPI-NET] unknown root sig");
    }

    terminal_print("[K:ACPI-NET] hints mask=");
    terminal_print_inline_hex32(hints);
    terminal_print("[K:ACPI-NET] probe done");
    return hints;
}

uint32_t acpi_probe_net_resource_count(void)
{
    return g_net_res_count;
}

const acpi_net_resource_window *acpi_probe_net_resources(void)
{
    return g_net_res;
}

uint32_t acpi_probe_net_gpio_count(void)
{
    return g_net_gpio_count;
}

const acpi_net_gpio_hint *acpi_probe_net_gpios(void)
{
    return g_net_gpio;
}

int acpi_probe_net_exec_device_method_args(uint64_t rsdp_phys,
                                           const char dev_name4[4],
                                           const char method_name4[4],
                                           uint8_t arg_count,
                                           const uint64_t *args,
                                           uint64_t *ret_out)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;

    if (!rsdp_phys || !dev_name4 || !method_name4)
        return -1;

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;
    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
        return -1;

    root = find_root(rsdp);
    if (!root || root->length < sizeof(acpi_sdt_header_t))
        return -1;

    if (memeq_n(root->signature, "XSDT", 4))
    {
        const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
        uint32_t n = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8u;
        uint32_t ssdt_seen = 0u;

        for (uint32_t i = 0; i < n; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)ptrs[i];
            int ran = 0;
            int rc;
            if (!hdr || hdr->length < sizeof(acpi_sdt_header_t))
                continue;

            if (memeq_n(hdr->signature, "FACP", 4))
            {
                const uint8_t *fadt = (const uint8_t *)(uintptr_t)hdr;
                uint64_t xdsdt = (hdr->length >= 148u) ? read_qword(fadt + 140u) : 0ull;
                uint64_t dsdt = (hdr->length >= 44u) ? (uint64_t)read_dword(fadt + 40u) : 0ull;
                const acpi_sdt_header_t *dsdt_hdr =
                    (const acpi_sdt_header_t *)(uintptr_t)(xdsdt ? xdsdt : dsdt);
                if (!dsdt_hdr || dsdt_hdr->length < sizeof(acpi_sdt_header_t))
                    continue;
                rc = exec_device_method_in_table(dsdt_hdr, dev_name4, method_name4, arg_count, args, ret_out, &ran);
                if (ran)
                    return rc;
            }
            else if (memeq_n(hdr->signature, "DSDT", 4))
            {
                rc = exec_device_method_in_table(hdr, dev_name4, method_name4, arg_count, args, ret_out, &ran);
                if (ran)
                    return rc;
            }
            else if (memeq_n(hdr->signature, "SSDT", 4) && ssdt_seen < 16u)
            {
                ssdt_seen++;
                rc = exec_device_method_in_table(hdr, dev_name4, method_name4, arg_count, args, ret_out, &ran);
                if (ran)
                    return rc;
            }
        }
    }
    else if (memeq_n(root->signature, "RSDT", 4))
    {
        const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
        uint32_t n = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4u;
        uint32_t ssdt_seen = 0u;

        for (uint32_t i = 0; i < n; ++i)
        {
            const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)(uint64_t)ptrs[i];
            int ran = 0;
            int rc;
            if (!hdr || hdr->length < sizeof(acpi_sdt_header_t))
                continue;

            if (memeq_n(hdr->signature, "DSDT", 4))
            {
                rc = exec_device_method_in_table(hdr, dev_name4, method_name4, arg_count, args, ret_out, &ran);
                if (ran)
                    return rc;
            }
            else if (memeq_n(hdr->signature, "SSDT", 4) && ssdt_seen < 16u)
            {
                ssdt_seen++;
                rc = exec_device_method_in_table(hdr, dev_name4, method_name4, arg_count, args, ret_out, &ran);
                if (ran)
                    return rc;
            }
        }
    }

    terminal_print("[K:ACPI-NET] exec method not found");
    return -2;
}

int acpi_probe_net_exec_device_method(uint64_t rsdp_phys, const char dev_name4[4], const char method_name4[4], uint64_t *ret_out)
{
    return acpi_probe_net_exec_device_method_args(rsdp_phys, dev_name4, method_name4, 0u, 0, ret_out);
}

void acpi_probe_net_exec_context_reset(void)
{
    aml_named_reset();
}
