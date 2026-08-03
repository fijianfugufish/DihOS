#include "system/cpu_info.h"
#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

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
} cpu_acpi_rsdp;

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
} cpu_acpi_sdt;

typedef struct
{
    cpu_acpi_sdt h;
    uint32_t local_interrupt_controller_address;
    uint32_t flags;
    uint8_t entries[1];
} cpu_acpi_madt;

typedef struct
{
    uint8_t type;
    uint8_t length;
} cpu_acpi_madt_entry;

typedef struct
{
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t cpu_interface_number;
    uint32_t acpi_processor_uid;
    uint32_t flags;
    uint32_t parking_protocol_version;
    uint32_t performance_interrupt_gsiv;
    uint64_t parked_address;
    uint64_t physical_base_address;
    uint64_t gicv;
    uint64_t gich;
    uint32_t vgic_maintenance_interrupt;
    uint64_t gicr_base_address;
    uint64_t mpidr;
} cpu_acpi_gicc_min;
#pragma pack(pop)

enum
{
    CPU_MADT_GICC = 0x0Bu,
    CPU_GICC_ENABLED = 1u << 0
};

static cpu_info_snapshot g_cpu_info;

static int cpu_sig4_eq(const char sig[4], const char *lit)
{
    return sig[0] == lit[0] && sig[1] == lit[1] && sig[2] == lit[2] && sig[3] == lit[3];
}

static int cpu_sig8_eq(const char sig[8], const char *lit)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        if (sig[i] != lit[i])
            return 0;
    return 1;
}

static int cpu_sane_ptr(uint64_t p)
{
    return p >= 0x1000ull && p < 0x0001000000000000ull;
}

static const cpu_acpi_sdt *cpu_acpi_find_table(uint64_t rsdp_phys, const char *sig)
{
    const cpu_acpi_rsdp *rsdp;

    if (!cpu_sane_ptr(rsdp_phys))
        return NULL;
    rsdp = (const cpu_acpi_rsdp *)(uintptr_t)rsdp_phys;
    if (!cpu_sig8_eq(rsdp->sig, "RSD PTR "))
        return NULL;

    if (rsdp->xsdt_address && cpu_sane_ptr(rsdp->xsdt_address))
    {
        const cpu_acpi_sdt *xsdt = (const cpu_acpi_sdt *)(uintptr_t)rsdp->xsdt_address;
        if (cpu_sig4_eq(xsdt->signature, "XSDT") && xsdt->length >= sizeof(cpu_acpi_sdt))
        {
            const uint64_t *entry = (const uint64_t *)((const uint8_t *)xsdt + sizeof(cpu_acpi_sdt));
            uint32_t n = (xsdt->length - (uint32_t)sizeof(cpu_acpi_sdt)) / 8u;
            for (uint32_t i = 0u; i < n; ++i)
            {
                const cpu_acpi_sdt *h;
                if (!cpu_sane_ptr(entry[i]))
                    continue;
                h = (const cpu_acpi_sdt *)(uintptr_t)entry[i];
                if (cpu_sig4_eq(h->signature, sig))
                    return h;
            }
        }
    }

    if (rsdp->rsdt_address && cpu_sane_ptr(rsdp->rsdt_address))
    {
        const cpu_acpi_sdt *rsdt = (const cpu_acpi_sdt *)(uintptr_t)(uint64_t)rsdp->rsdt_address;
        if (cpu_sig4_eq(rsdt->signature, "RSDT") && rsdt->length >= sizeof(cpu_acpi_sdt))
        {
            const uint32_t *entry = (const uint32_t *)((const uint8_t *)rsdt + sizeof(cpu_acpi_sdt));
            uint32_t n = (rsdt->length - (uint32_t)sizeof(cpu_acpi_sdt)) / 4u;
            for (uint32_t i = 0u; i < n; ++i)
            {
                const cpu_acpi_sdt *h;
                if (!cpu_sane_ptr(entry[i]))
                    continue;
                h = (const cpu_acpi_sdt *)(uintptr_t)(uint64_t)entry[i];
                if (cpu_sig4_eq(h->signature, sig))
                    return h;
            }
        }
    }

    return NULL;
}

static uint64_t cpu_read_mpidr(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_midr(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_revidr(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, revidr_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_ctr(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_id_aa64pfr0(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_id_aa64isar0(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, id_aa64isar0_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t cpu_read_id_aa64mmfr0(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static void cpu_parse_madt(uint64_t rsdp_phys)
{
    const cpu_acpi_sdt *h = cpu_acpi_find_table(rsdp_phys, "APIC");
    const cpu_acpi_madt *madt;
    const uint8_t *p;
    const uint8_t *end;

    if (!h || h->length < sizeof(cpu_acpi_madt))
        return;

    madt = (const cpu_acpi_madt *)h;
    g_cpu_info.acpi_madt_found = 1u;
    p = madt->entries;
    end = (const uint8_t *)h + h->length;

    while (p + sizeof(cpu_acpi_madt_entry) <= end)
    {
        const cpu_acpi_madt_entry *entry = (const cpu_acpi_madt_entry *)p;
        if (entry->length < sizeof(cpu_acpi_madt_entry) || p + entry->length > end)
            break;

        if (entry->type == CPU_MADT_GICC && entry->length >= sizeof(cpu_acpi_gicc_min))
        {
            const cpu_acpi_gicc_min *gicc = (const cpu_acpi_gicc_min *)p;
            if (g_cpu_info.core_count < CPU_INFO_MAX_CORES)
            {
                cpu_core_info *out = &g_cpu_info.cores[g_cpu_info.core_count++];
                out->acpi_uid = gicc->acpi_processor_uid;
                out->gicc_id = gicc->cpu_interface_number;
                out->flags = gicc->flags;
                out->enabled = (gicc->flags & CPU_GICC_ENABLED) ? 1u : 0u;
                out->mpidr = gicc->mpidr;
                if (out->enabled)
                    ++g_cpu_info.enabled_count;
            }
        }

        p += entry->length;
    }
}

void cpu_info_init(uint64_t acpi_rsdp_phys)
{
    g_cpu_info = (cpu_info_snapshot){0};
    g_cpu_info.boot_mpidr = cpu_read_mpidr();
    g_cpu_info.midr = cpu_read_midr();
    g_cpu_info.revidr = cpu_read_revidr();
    g_cpu_info.ctr = cpu_read_ctr();
    g_cpu_info.id_aa64pfr0 = cpu_read_id_aa64pfr0();
    g_cpu_info.id_aa64isar0 = cpu_read_id_aa64isar0();
    g_cpu_info.id_aa64mmfr0 = cpu_read_id_aa64mmfr0();
    cpu_parse_madt(acpi_rsdp_phys);
    g_cpu_info.initialized = 1u;
}

void cpu_info_get(cpu_info_snapshot *out_snapshot)
{
    if (!out_snapshot)
        return;
    *out_snapshot = g_cpu_info;
}
