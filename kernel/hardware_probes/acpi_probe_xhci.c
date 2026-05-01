#include "hardware_probes/acpi_probe_xhci.h"
//#include "terminal/terminal_api.h"

#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#pragma pack(push, 1)
typedef struct
{
    char sig[8];
    uint8_t csum;
    char oemid[6];
    uint8_t rev;
    uint32_t rsdt;
    uint32_t len;
    uint64_t xsdt;
    uint8_t xsum;
    uint8_t rsv[3];
} acpi_rsdp_t;

typedef struct
{
    char sig[4];
    uint32_t len;
    uint8_t rev;
    uint8_t csum;
    char oemid[6];
    char oemtab[8];
    uint32_t oemrev;
    uint32_t creator;
    uint32_t creatorrev;
} acpi_sdt_t;

typedef struct
{
    acpi_sdt_t h;
    uint64_t entry[1];
} acpi_xsdt_t;

typedef struct
{
    acpi_sdt_t h;
    uint32_t entry[1];
} acpi_rsdt_t;

typedef struct
{
    acpi_sdt_t h;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0[40];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
} acpi_fadt_t;

typedef struct
{
    acpi_sdt_t h;
    uint8_t data[1];
} acpi_aml_table_t;
#pragma pack(pop)

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return ((uint64_t)rd32(p)) | ((uint64_t)rd32(p + 4) << 32);
}

static int sig4_eq(const char s[4], const char *lit)
{
    return s[0] == lit[0] && s[1] == lit[1] && s[2] == lit[2] && s[3] == lit[3];
}

static int sane_ptr(uint64_t p)
{
    return p >= 0x1000ull && p < 0x0001000000000000ull;
}

static void xhci_add(uint64_t *out, uint32_t *count, uint32_t max_count, uint64_t mmio)
{
    mmio &= ~0xFull;

    if (!out || !count || !max_count || !mmio)
        return;

    for (uint32_t i = 0; i < *count; ++i)
    {
        if ((out[i] & ~0xFull) == mmio)
            return;
    }

    if (*count < max_count)
        out[(*count)++] = mmio;
}

static int xhci_caps_ok(uint64_t mmio)
{
    /*
      DO NOT touch MMIO here.

      ACPI _CRS contains many memory resources that are not xHCI.
      Probing them here can crash the kernel before USB init/fallback.
    */
    if (!sane_ptr(mmio))
        return 0;

    if ((mmio & 0xFFFu) != 0)
        return 0;

    /*
      Keep this tight for your Snapdragon low-MMIO region.
      Known-good examples: 0x0A600000, 0x0A000000, 0x0A800000.
    */
    if (mmio < 0x08000000ull || mmio > 0x0FFFFFFFull)
        return 0;

    return 1;
}

/*
  AML PkgLength parser.
  Returns payload length and stores the number of bytes used by PkgLength.
*/
static uint32_t aml_pkg_len(const uint8_t *p, const uint8_t *end, uint32_t *used)
{
    if (!p || p >= end)
    {
        if (used)
            *used = 0;
        return 0;
    }

    uint8_t b0 = p[0];
    uint32_t byte_count = (b0 >> 6) & 3u;
    uint32_t len = b0 & 0x3Fu;

    if (byte_count == 0)
    {
        if (used)
            *used = 1;
        return len;
    }

    if (p + 1u + byte_count > end)
    {
        if (used)
            *used = 0;
        return 0;
    }

    len = b0 & 0x0Fu;
    for (uint32_t i = 0; i < byte_count; ++i)
        len |= ((uint32_t)p[1u + i]) << (4u + 8u * i);

    if (used)
        *used = 1u + byte_count;

    return len;
}

static uint32_t aml_nameseg_len(const uint8_t *p, const uint8_t *end)
{
    if (!p || p >= end)
        return 0;

    if (*p == '\\')
        return 1u + aml_nameseg_len(p + 1, end);

    if (*p == '^')
    {
        uint32_t n = 0;
        while (p + n < end && p[n] == '^')
            ++n;
        return n + aml_nameseg_len(p + n, end);
    }

    if (*p == 0x00) /* NullName */
        return 1;

    if (*p == 0x2E) /* DualNamePrefix */
        return (p + 9 <= end) ? 9u : 0u;

    if (*p == 0x2F) /* MultiNamePrefix */
    {
        if (p + 2 > end)
            return 0;
        uint32_t count = p[1];
        return (p + 2u + count * 4u <= end) ? 2u + count * 4u : 0u;
    }

    return (p + 4 <= end) ? 4u : 0u;
}

static uint32_t aml_parse_integer_object(const uint8_t *p, const uint8_t *end, uint64_t *out)
{
    if (!p || p >= end)
        return 0;

    if (out)
        *out = 0;

    switch (*p)
    {
    case 0x00: /* ZeroOp */
        return 1;
    case 0x01: /* OneOp */
        if (out)
            *out = 1;
        return 1;
    case 0x0A: /* BytePrefix */
        if (p + 2 > end) return 0;
        if (out) *out = p[1];
        return 2;
    case 0x0B: /* WordPrefix */
        if (p + 3 > end) return 0;
        if (out) *out = rd16(p + 1);
        return 3;
    case 0x0C: /* DWordPrefix */
        if (p + 5 > end) return 0;
        if (out) *out = rd32(p + 1);
        return 5;
    case 0x0E: /* QWordPrefix */
        if (p + 9 > end) return 0;
        if (out) *out = rd64(p + 1);
        return 9;
    default:
        return 0;
    }
}

static const uint8_t *aml_try_buffer(const uint8_t *p,
                                     const uint8_t *end,
                                     uint32_t *out_len)
{
    uint32_t pkg_used = 0;
    uint32_t pkg_len = 0;
    uint64_t declared = 0;
    uint32_t int_used = 0;

    if (out_len)
        *out_len = 0;

    if (!p || p >= end || *p != 0x11) /* BufferOp */
        return NULL;

    pkg_len = aml_pkg_len(p + 1, end, &pkg_used);
    if (!pkg_used)
        return NULL;

    const uint8_t *obj_start = p + 1 + pkg_used;
    const uint8_t *obj_end = (p + 1 + pkg_len <= end) ? (p + 1 + pkg_len) : end;

    int_used = aml_parse_integer_object(obj_start, obj_end, &declared);
    if (!int_used)
        return NULL;

    const uint8_t *data = obj_start + int_used;
    if (data > obj_end)
        return NULL;

    uint32_t actual = (uint32_t)(obj_end - data);
    if (declared < actual)
        actual = (uint32_t)declared;

    if (out_len)
        *out_len = actual;

    return data;
}

static acpi_sdt_t *find_fadt_from_root(acpi_sdt_t *root)
{
    if (!root || root->len < sizeof(acpi_sdt_t))
        return NULL;

    if (sig4_eq(root->sig, "XSDT"))
    {
        acpi_xsdt_t *xsdt = (acpi_xsdt_t *)root;
        uint32_t entries = (xsdt->h.len - (uint32_t)sizeof(acpi_sdt_t)) / 8u;

        for (uint32_t i = 0; i < entries; ++i)
        {
            acpi_sdt_t *h = (acpi_sdt_t *)(uintptr_t)xsdt->entry[i];
            if (sane_ptr(xsdt->entry[i]) && sig4_eq(h->sig, "FACP"))
                return h;
        }
    }
    else if (sig4_eq(root->sig, "RSDT"))
    {
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)root;
        uint32_t entries = (rsdt->h.len - (uint32_t)sizeof(acpi_sdt_t)) / 4u;

        for (uint32_t i = 0; i < entries; ++i)
        {
            acpi_sdt_t *h = (acpi_sdt_t *)(uintptr_t)(uint64_t)rsdt->entry[i];
            if (sane_ptr(rsdt->entry[i]) && sig4_eq(h->sig, "FACP"))
                return h;
        }
    }

    return NULL;
}

static acpi_sdt_t *find_dsdt_from_fadt(acpi_sdt_t *fadt_sdt)
{
    if (!fadt_sdt || !sig4_eq(fadt_sdt->sig, "FACP"))
        return NULL;

    acpi_fadt_t *fadt = (acpi_fadt_t *)fadt_sdt;

    if (fadt_sdt->len >= offsetof(acpi_fadt_t, x_dsdt) + sizeof(uint64_t))
    {
        if (fadt->x_dsdt && sane_ptr(fadt->x_dsdt))
            return (acpi_sdt_t *)(uintptr_t)fadt->x_dsdt;
    }

    if (fadt->dsdt && sane_ptr(fadt->dsdt))
        return (acpi_sdt_t *)(uintptr_t)(uint64_t)fadt->dsdt;

    return NULL;
}

static void scan_crs_resource_buffer(const uint8_t *b,
                                     uint32_t n,
                                     uint64_t *out,
                                     uint32_t *count,
                                     uint32_t max_count)
{
    uint32_t off = 0;

    while (b && off < n)
    {
        uint8_t tag = b[off++];

        if ((tag & 0x80u) == 0)
        {
            uint8_t small_type = tag & 0x78u;
            uint8_t small_len = tag & 0x07u;

            if (small_type == 0x78u) /* EndTag */
                return;

            if (off + small_len > n)
                return;

            off += small_len;
            continue;
        }

        if (off + 2u > n)
            return;

        uint8_t large_type = tag;
        uint32_t len = rd16(b + off);
        off += 2u;

        if (off + len > n)
            return;

        const uint8_t *r = b + off;

        if (large_type == 0x86u && len >= 9u) /* Memory32Fixed */
        {
            uint64_t base = rd32(r + 1);
            uint32_t size = rd32(r + 5);

            if (size && xhci_caps_ok(base))
                xhci_add(out, count, max_count, base);
        }
        else if (large_type == 0x87u && len >= 23u) /* DWord Address Space */
        {
            uint8_t resource_type = r[0];

            if (resource_type == 0u) /* memory */
            {
                uint64_t min = rd32(r + 7);
                uint32_t length = rd32(r + 19);

                if (length && xhci_caps_ok(min))
                    xhci_add(out, count, max_count, min);
            }
        }
        else if (large_type == 0x8Au && len >= 43u) /* QWord Address Space */
        {
            uint8_t resource_type = r[0];

            if (resource_type == 0u) /* memory */
            {
                uint64_t min = rd64(r + 11);
                uint64_t length = rd64(r + 35);

                if (length && xhci_caps_ok(min))
                    xhci_add(out, count, max_count, min);
            }
        }
        else if (large_type == 0x8Bu && len >= 43u) /* Extended Address Space */
        {
            uint8_t resource_type = r[0];

            if (resource_type == 0u) /* memory */
            {
                uint64_t min = rd64(r + 12);
                uint64_t length = rd64(r + 36);

                if (length && xhci_caps_ok(min))
                    xhci_add(out, count, max_count, min);
            }
        }

        off += len;
    }
}

static int bytes_eq4(const uint8_t *p, const char *s)
{
    return p[0] == (uint8_t)s[0] &&
           p[1] == (uint8_t)s[1] &&
           p[2] == (uint8_t)s[2] &&
           p[3] == (uint8_t)s[3];
}

static void scan_device_for_crs_buffers(const uint8_t *dev,
                                        const uint8_t *dev_end,
                                        uint64_t *out,
                                        uint32_t *count,
                                        uint32_t max_count)
{
    for (const uint8_t *p = dev; p + 4u < dev_end; ++p)
    {
        if (!bytes_eq4(p, "_CRS"))
            continue;

        /*
          Practical bridge:
          _CRS may be Name(_CRS, Buffer(...)) or Method(_CRS){ Return(Buffer(...)) }.
          Search shortly after the name for a BufferOp.
        */
        const uint8_t *limit = p + 256u;
        if (limit > dev_end)
            limit = dev_end;

        for (const uint8_t *q = p + 4u; q < limit; ++q)
        {
            uint32_t crs_len = 0;
            const uint8_t *crs = aml_try_buffer(q, limit, &crs_len);

            if (crs && crs_len)
            {
                scan_crs_resource_buffer(crs, crs_len, out, count, max_count);
                break;
            }
        }
    }
}

static void scan_aml_table_for_xhci(const uint8_t *aml,
                                    uint32_t aml_len,
                                    uint64_t *out,
                                    uint32_t *count,
                                    uint32_t max_count)
{
    const uint8_t *begin = aml;
    const uint8_t *end = aml + aml_len;

    for (const uint8_t *p = begin; p + 8u < end; ++p)
    {
        if (p[0] != 0x5Bu || p[1] != 0x82u) /* ExtOp DeviceOp */
            continue;

        uint32_t pkg_used = 0;
        uint32_t pkg_len = aml_pkg_len(p + 2, end, &pkg_used);
        if (!pkg_used || !pkg_len)
            continue;

        const uint8_t *pkg_start = p + 2;
        const uint8_t *obj_end = pkg_start + pkg_len;
        if (obj_end > end)
            obj_end = end;

        const uint8_t *name = p + 2 + pkg_used;
        uint32_t name_len = aml_nameseg_len(name, obj_end);
        if (!name_len)
            continue;

        const uint8_t *body = name + name_len;
        if (body >= obj_end)
            continue;

        int looks_xhci = 0;

        /*
          First: check the ACPI device name itself.
          These are common names for USB/xHCI-ish devices.
        */
        if (name_len >= 4)
        {
            if (bytes_eq4(name, "XHC0") ||
                bytes_eq4(name, "XHC1") ||
                bytes_eq4(name, "XHC2") ||
                bytes_eq4(name, "XHCI") ||
                bytes_eq4(name, "XHC_") ||
                bytes_eq4(name, "USB0") ||
                bytes_eq4(name, "USB1") ||
                bytes_eq4(name, "USB2") ||
                bytes_eq4(name, "USB3") ||
                bytes_eq4(name, "URS0") ||
                bytes_eq4(name, "URS1") ||
                bytes_eq4(name, "URS2"))
            {
                looks_xhci = 1;
            }
        }

        /*
          Second: scan inside the Device body for USB/xHCI hints.
          This avoids scraping _CRS from every random ACPI device.
        */
        if (!looks_xhci)
        {
            for (const uint8_t *s = body; s + 4u <= obj_end; ++s)
            {
                if (bytes_eq4(s, "XHC0") ||
                    bytes_eq4(s, "XHC1") ||
                    bytes_eq4(s, "XHC2") ||
                    bytes_eq4(s, "XHCI") ||
                    bytes_eq4(s, "XHC_") ||
                    bytes_eq4(s, "USB0") ||
                    bytes_eq4(s, "USB1") ||
                    bytes_eq4(s, "USB2") ||
                    bytes_eq4(s, "USB3") ||
                    bytes_eq4(s, "URS0") ||
                    bytes_eq4(s, "URS1") ||
                    bytes_eq4(s, "URS2"))
                {
                    looks_xhci = 1;
                    break;
                }
            }
        }

        if (!looks_xhci)
            continue;

        scan_device_for_crs_buffers(body, obj_end, out, count, max_count);
    }
}

static void scan_sdt_if_aml(acpi_sdt_t *h,
                            uint64_t *out,
                            uint32_t *count,
                            uint32_t max_count)
{
    if (!h || h->len < sizeof(acpi_sdt_t) || h->len > (1024u * 1024u))
        return;

    if (!sig4_eq(h->sig, "DSDT") && !sig4_eq(h->sig, "SSDT"))
        return;

    const uint8_t *aml = ((const uint8_t *)h) + sizeof(acpi_sdt_t);
    uint32_t aml_len = h->len - (uint32_t)sizeof(acpi_sdt_t);

    scan_aml_table_for_xhci(aml, aml_len, out, count, max_count);
}

uint32_t acpi_xhci_get_mmios_from_rsdp(uint64_t rsdp_phys,
                                       uint64_t *out,
                                       uint32_t max_count)
{
    uint32_t count = 0;
    acpi_sdt_t *root = NULL;
    acpi_sdt_t *fadt = NULL;
    acpi_sdt_t *dsdt = NULL;

    if (!rsdp_phys || !out || !max_count || !sane_ptr(rsdp_phys))
        return 0;

    acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(uintptr_t)rsdp_phys;

    if (rsdp->rev >= 2u && rsdp->xsdt && sane_ptr(rsdp->xsdt))
        root = (acpi_sdt_t *)(uintptr_t)rsdp->xsdt;
    else if (rsdp->rsdt && sane_ptr(rsdp->rsdt))
        root = (acpi_sdt_t *)(uintptr_t)(uint64_t)rsdp->rsdt;

    if (!root)
        return 0;

    fadt = find_fadt_from_root(root);
    dsdt = find_dsdt_from_fadt(fadt);

    /* Critical bit: scan DSDT first. */
    if (dsdt)
        scan_sdt_if_aml(dsdt, out, &count, max_count);

    /* Then scan SSDTs directly listed in root. */
    if (sig4_eq(root->sig, "XSDT"))
    {
        acpi_xsdt_t *xsdt = (acpi_xsdt_t *)root;
        uint32_t entries = (xsdt->h.len - (uint32_t)sizeof(acpi_sdt_t)) / 8u;

        for (uint32_t i = 0; i < entries; ++i)
        {
            if (!sane_ptr(xsdt->entry[i]))
                continue;

            scan_sdt_if_aml((acpi_sdt_t *)(uintptr_t)xsdt->entry[i],
                            out,
                            &count,
                            max_count);
        }
    }
    else if (sig4_eq(root->sig, "RSDT"))
    {
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)root;
        uint32_t entries = (rsdt->h.len - (uint32_t)sizeof(acpi_sdt_t)) / 4u;

        for (uint32_t i = 0; i < entries; ++i)
        {
            uint64_t pa = rsdt->entry[i];

            if (!sane_ptr(pa))
                continue;

            scan_sdt_if_aml((acpi_sdt_t *)(uintptr_t)pa,
                            out,
                            &count,
                            max_count);
        }
    }

    return count;
}