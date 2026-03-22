#include "hardware_probes/acpi_probe_hid.h"
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
    const uint8_t *aml;
    uint32_t aml_len;
} aml_blob_t;

typedef struct
{
    char name[5];
    uint32_t deviceop_off;
    uint32_t body_start;
    uint32_t body_end;
    uint32_t crs_name_off;
    uint32_t rbuf_off;
    const uint8_t *rbuf;
    uint32_t rbuf_len;
} hid_dev_t;

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

static void print_hex_bytes(const uint8_t *p, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; ++i)
    {
        terminal_print_hex8(p[i]);
        terminal_print(" ");
        if (((i + 1u) & 15u) == 0u)
            terminal_print("\n");
    }
    if ((len & 15u) != 0u)
        terminal_print("\n");
}

static int nameseg_eq(const uint8_t *p, const char *s4)
{
    return p[0] == (uint8_t)s4[0] &&
           p[1] == (uint8_t)s4[1] &&
           p[2] == (uint8_t)s4[2] &&
           p[3] == (uint8_t)s4[3];
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

static int find_device_by_name(const aml_blob_t *dsdt, const char *name4, hid_dev_t *out)
{
    uint32_t i;

    for (i = 0; i + 8u < dsdt->aml_len; ++i)
    {
        if (dsdt->aml[i] != 0x5Bu || dsdt->aml[i + 1u] != 0x82u)
            continue;

        {
            uint32_t pkglen = 0;
            uint32_t pkglen_bytes = 0;
            uint32_t name_off;
            uint32_t body_start;
            uint32_t body_end;

            if (aml_read_pkglen(dsdt->aml, dsdt->aml_len, i + 2u, &pkglen, &pkglen_bytes) != 0)
                continue;

            name_off = i + 2u + pkglen_bytes;
            body_start = name_off + 4u;

            if (name_off + 4u > dsdt->aml_len)
                continue;

            if (pkglen < 4u)
                continue;

            body_end = body_start + (pkglen - 4u);
            if (body_end > dsdt->aml_len)
                body_end = dsdt->aml_len;

            if (!nameseg_eq(dsdt->aml + name_off, name4))
                continue;

            out->name[0] = name4[0];
            out->name[1] = name4[1];
            out->name[2] = name4[2];
            out->name[3] = name4[3];
            out->name[4] = 0;
            out->deviceop_off = i;
            out->body_start = body_start;
            out->body_end = body_end;
            out->crs_name_off = 0;
            out->rbuf_off = 0;
            out->rbuf = 0;
            out->rbuf_len = 0;
            return 1;
        }
    }

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

    if (!buf || !name)
        return -1;

    for (i = start; i < end; ++i)
    {
        uint32_t pkglen = 0;
        uint32_t pkglen_bytes = 0;
        uint32_t name_off;

        if (buf[i] != 0x14)
            continue;

        if (aml_read_pkglen(buf, end, i + 1u, &pkglen, &pkglen_bytes) != 0)
            continue;

        name_off = i + 1u + pkglen_bytes;
        if (name_off + 4u > end)
            continue;

        if (buf[name_off + 0] == (uint8_t)name[0] &&
            buf[name_off + 1] == (uint8_t)name[1] &&
            buf[name_off + 2] == (uint8_t)name[2] &&
            buf[name_off + 3] == (uint8_t)name[3])
            return (int)i;
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

    pkglen_off = method_off + 1u;

    if (aml_read_pkglen(aml, aml_len, pkglen_off, &pkglen, &pkglen_bytes) != 0)
        return -1;

    pkg_start = pkglen_off + pkglen_bytes;
    pkg_end = pkg_start + pkglen;

    if (pkg_end > aml_len)
        pkg_end = aml_len;

    name_off = pkg_start;
    flags_off = name_off + 4u;
    body_start = flags_off + 1u;

    if (name_off + 4u > aml_len || body_start > pkg_end)
        return -1;

    if (name_off_out)
        *name_off_out = name_off;
    if (body_start_out)
        *body_start_out = body_start;
    if (body_end_out)
        *body_end_out = pkg_end;

    return 0;
}

static int find_named_buffer_in_range(const uint8_t *aml, uint32_t start, uint32_t end,
                                      const char *name, uint32_t *name_off_out)
{
    uint32_t i;

    if (!aml || !name || !name_off_out)
        return -1;

    for (i = start; i + 6u <= end; ++i)
    {
        if (aml[i] == 0x08 &&
            aml[i + 1] == (uint8_t)name[0] &&
            aml[i + 2] == (uint8_t)name[1] &&
            aml[i + 3] == (uint8_t)name[2] &&
            aml[i + 4] == (uint8_t)name[3] &&
            aml[i + 5] == 0x11)
        {
            *name_off_out = i;
            return 0;
        }
    }

    return -1;
}

static int try_extract_named_buffer(const uint8_t *aml, uint32_t aml_len, uint32_t name_off,
                                    const uint8_t **buf_out, uint32_t *len_out)
{
    uint32_t p;
    uint32_t pkglen, pkglen_bytes;
    uint32_t bytecount;

    if (!aml || !buf_out || !len_out)
        return -1;

    p = name_off + 5u;
    if (p >= aml_len || aml[p] != 0x11)
        return -1;
    p++;

    if (aml_read_pkglen(aml, aml_len, p, &pkglen, &pkglen_bytes) != 0)
        return -1;
    p += pkglen_bytes;

    if (p >= aml_len)
        return -1;

    if (aml[p] == 0x0A)
    {
        if (p + 2u > aml_len)
            return -1;
        bytecount = aml[p + 1u];
        p += 2u;
    }
    else if (aml[p] <= 0x3F)
    {
        bytecount = aml[p];
        p += 1u;
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

static int find_crs_rbuf(const aml_blob_t *dsdt, hid_dev_t *dev)
{
    int crs_name_off;
    int crs_method_off;

    crs_name_off = find_name_object(dsdt->aml, dev->body_start, dev->body_end, "_CRS");
    if (crs_name_off >= 0)
    {
        const uint8_t *buf = 0;
        uint32_t len = 0;

        dev->crs_name_off = (uint32_t)crs_name_off;

        if (try_extract_named_buffer(dsdt->aml, dsdt->aml_len, (uint32_t)crs_name_off, &buf, &len) == 0)
        {
            dev->rbuf_off = (uint32_t)(buf - dsdt->aml);
            dev->rbuf = buf;
            dev->rbuf_len = len;
            return 1;
        }
    }

    crs_method_off = find_method_object(dsdt->aml, dev->body_start, dev->body_end, "_CRS");
    if (crs_method_off >= 0)
    {
        uint32_t method_name_off = 0;
        uint32_t method_body_start = 0;
        uint32_t method_body_end = 0;
        uint32_t rbuf_name_off = 0;
        const uint8_t *buf = 0;
        uint32_t len = 0;

        dev->crs_name_off = (uint32_t)crs_method_off;

        if (get_method_bounds(dsdt->aml, dsdt->aml_len, (uint32_t)crs_method_off,
                              &method_name_off, &method_body_start, &method_body_end) != 0)
            return 0;

        if (find_named_buffer_in_range(dsdt->aml, method_body_start, method_body_end,
                                       "RBUF", &rbuf_name_off) != 0)
            return 0;

        if (try_extract_named_buffer(dsdt->aml, dsdt->aml_len, rbuf_name_off, &buf, &len) != 0)
            return 0;

        dev->rbuf_off = (uint32_t)(buf - dsdt->aml);
        dev->rbuf = buf;
        dev->rbuf_len = len;
        return 1;
    }

    return 0;
}

static void decode_resource_buffer(const hid_dev_t *dev)
{
    const uint8_t *p = dev->rbuf;
    uint32_t rem = dev->rbuf_len;

    terminal_print("device: ");
    terminal_print(dev->name);
    terminal_print("deviceop off:");
    terminal_print_hex32(dev->deviceop_off);
    terminal_print("_CRS off:");
    terminal_print_hex32(dev->crs_name_off);
    terminal_print("RBUF off:");
    terminal_print_hex32(dev->rbuf_off);
    terminal_print("_CRS raw len:");
    terminal_print_hex32(dev->rbuf_len);

    print_hex_bytes(p, dev->rbuf_len);

    while (rem > 0u)
    {
        uint8_t tag = p[0];

        if (tag == 0x79u)
        {
            terminal_success("end tag");
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

            if (item == 0x0Cu)
            {
                terminal_success("I2C serial bus resource");
                terminal_print("i2c raw len:");
                terminal_print_hex32((uint32_t)size);
                print_hex_bytes(p, 3u + size);
            }
            else if (item == 0x0Eu)
            {
                terminal_success("GPIO resource");
                terminal_print("gpio raw len:");
                terminal_print_hex32((uint32_t)size);
                print_hex_bytes(p, 3u + size);
            }

            p += (uint32_t)(3u + size);
            rem -= (uint32_t)(3u + size);
            continue;
        }

        terminal_warn("small or unknown resource");
        terminal_print("tag:");
        terminal_print_hex8(tag);
        break;
    }
}

static void probe_dsdt_for_hid_targets(const acpi_sdt_header_t *dsdt_hdr)
{
    aml_blob_t dsdt;
    hid_dev_t eckb;
    hid_dev_t tcpd;
    const uint8_t *table;
    uint32_t table_len;

    if (!dsdt_hdr)
    {
        terminal_error("hid probe: no DSDT");
        return;
    }

    if (!memeq_n(dsdt_hdr->signature, "DSDT", 4))
    {
        terminal_error("hid probe: target is not DSDT");
        return;
    }

    if (!checksum_ok(dsdt_hdr, dsdt_hdr->length))
        terminal_warn("hid probe: DSDT checksum bad");
    else
        terminal_success("hid probe: DSDT checksum ok");

    table = (const uint8_t *)dsdt_hdr;
    table_len = dsdt_hdr->length;

    terminal_print("hid probe: DSDT phys:");
    terminal_print_hex64((uint64_t)(uintptr_t)dsdt_hdr);
    terminal_print("hid probe: DSDT len:");
    terminal_print_hex32(table_len);

    dsdt.aml = table;
    dsdt.aml_len = table_len;

    if (find_device_by_name(&dsdt, "ECKB", &eckb))
    {
        terminal_success("found ECKB");
        if (find_crs_rbuf(&dsdt, &eckb))
            decode_resource_buffer(&eckb);
        else
            terminal_warn("ECKB _CRS/RBUF not found");
    }
    else
    {
        terminal_warn("ECKB not found");
    }

    if (find_device_by_name(&dsdt, "TCPD", &tcpd))
    {
        terminal_success("found TCPD");
        if (find_crs_rbuf(&dsdt, &tcpd))
            decode_resource_buffer(&tcpd);
        else
            terminal_warn("TCPD _CRS/RBUF not found");
    }
    else
    {
        terminal_warn("TCPD not found");
    }
}

void acpi_probe_hid_from_rsdp(uint64_t rsdp_phys)
{
    const acpi_rsdp_t *rsdp;
    const acpi_sdt_header_t *root;
    const acpi_sdt_header_t *fadt;
    const acpi_sdt_header_t *dsdt;

    terminal_print("acpi hid probe start");

    if (!rsdp_phys)
    {
        terminal_error("hid probe: rsdp is null");
        return;
    }

    rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    if (!memeq_n(rsdp->sig, "RSD PTR ", 8))
    {
        terminal_error("hid probe: bad RSDP sig");
        return;
    }

    root = find_xsdt_or_rsdt(rsdp);
    if (!root)
    {
        terminal_error("hid probe: no XSDT/RSDT");
        return;
    }

    terminal_print("hid probe: root sig:");
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
        terminal_error("hid probe: no FADT");
        return;
    }

    dsdt = find_dsdt_from_fadt(fadt);
    if (!dsdt)
    {
        terminal_error("hid probe: no DSDT/XDSDT");
        return;
    }

    probe_dsdt_for_hid_targets(dsdt);

    terminal_print("acpi hid probe done");
    terminal_flush_log();
}