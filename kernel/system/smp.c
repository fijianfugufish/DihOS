#include "system/smp.h"
#include "asm/asm.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"
#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

enum
{
    /*
     * A remote network request builds a 6 KiB HTTP request on its call stack,
     * then descends through DNS/TLS/xHCI.  The old 16 KiB secondary-core
     * stack could overflow before the exception path had room to report it.
     */
    SMP_STACK_PAGES = 16u,
    SMP_MAGIC = 0x534D5043u,
    SMP_PSCI_NONE = 0u,
    SMP_PSCI_SMC = 1u,
    SMP_PSCI_HVC = 2u,
    SMP_PSCI_VERSION = 0x84000000u,
    SMP_PSCI_CPU_ON_64 = 0xC4000003u,
};

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
} smp_acpi_rsdp;

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
} smp_acpi_sdt;
#pragma pack(pop)

typedef struct
{
    uint64_t stack_top;
    uint32_t magic;
    uint32_t logical_id;
    uint32_t acpi_uid;
    uint32_t enabled;
    uint32_t state;
    volatile uint32_t online;
    volatile uint32_t busy;
    volatile uint32_t jobs_completed;
    volatile uint32_t poll_ticks;
    volatile uint32_t worker_stage;
    volatile uintptr_t worker_poll_entry __attribute__((aligned(64)));
    int64_t last_psci_status;
    uint64_t mpidr;
    void *stack_base;
} smp_core;

static smp_core G_smp_cores[SMP_MAX_CORES] __attribute__((aligned(64)));
static uint32_t G_smp_initialized;
static uint32_t G_smp_core_count;
static uint32_t G_smp_psci_conduit;

static int smp_sig4_eq(const char sig[4], const char *lit)
{
    return sig[0] == lit[0] && sig[1] == lit[1] && sig[2] == lit[2] && sig[3] == lit[3];
}

static int smp_sig8_eq(const char sig[8], const char *lit)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        if (sig[i] != lit[i])
            return 0;
    return 1;
}

static int smp_sane_ptr(uint64_t p)
{
    return p >= 0x1000ull && p < 0x0001000000000000ull;
}

static const smp_acpi_sdt *smp_acpi_find_table(uint64_t rsdp_phys, const char *sig)
{
    const smp_acpi_rsdp *rsdp;

    if (!smp_sane_ptr(rsdp_phys))
        return NULL;
    rsdp = (const smp_acpi_rsdp *)(uintptr_t)rsdp_phys;
    if (!smp_sig8_eq(rsdp->sig, "RSD PTR "))
        return NULL;

    if (rsdp->xsdt_address && smp_sane_ptr(rsdp->xsdt_address))
    {
        const smp_acpi_sdt *xsdt = (const smp_acpi_sdt *)(uintptr_t)rsdp->xsdt_address;
        if (smp_sig4_eq(xsdt->signature, "XSDT") && xsdt->length >= sizeof(smp_acpi_sdt))
        {
            const uint64_t *entry = (const uint64_t *)((const uint8_t *)xsdt + sizeof(smp_acpi_sdt));
            uint32_t n = (xsdt->length - (uint32_t)sizeof(smp_acpi_sdt)) / 8u;
            for (uint32_t i = 0u; i < n; ++i)
            {
                const smp_acpi_sdt *h;
                if (!smp_sane_ptr(entry[i]))
                    continue;
                h = (const smp_acpi_sdt *)(uintptr_t)entry[i];
                if (smp_sig4_eq(h->signature, sig))
                    return h;
            }
        }
    }

    if (rsdp->rsdt_address && smp_sane_ptr(rsdp->rsdt_address))
    {
        const smp_acpi_sdt *rsdt = (const smp_acpi_sdt *)(uintptr_t)(uint64_t)rsdp->rsdt_address;
        if (smp_sig4_eq(rsdt->signature, "RSDT") && rsdt->length >= sizeof(smp_acpi_sdt))
        {
            const uint32_t *entry = (const uint32_t *)((const uint8_t *)rsdt + sizeof(smp_acpi_sdt));
            uint32_t n = (rsdt->length - (uint32_t)sizeof(smp_acpi_sdt)) / 4u;
            for (uint32_t i = 0u; i < n; ++i)
            {
                const smp_acpi_sdt *h;
                if (!smp_sane_ptr(entry[i]))
                    continue;
                h = (const smp_acpi_sdt *)(uintptr_t)(uint64_t)entry[i];
                if (smp_sig4_eq(h->signature, sig))
                    return h;
            }
        }
    }

    return NULL;
}

static const char *smp_conduit_name(uint32_t conduit)
{
    if (conduit == SMP_PSCI_SMC)
        return "SMC";
    if (conduit == SMP_PSCI_HVC)
        return "HVC";
    return "none";
}

static void smp_log_hex(const char *label, uint64_t value)
{
    terminal_print(label);
    terminal_print_inline_hex64(value);
    terminal_flush_log();
}

static void smp_log_text(const char *text)
{
    terminal_print(text);
    terminal_flush_log();
}

static int smp_psci_version_plausible(uint64_t value)
{
    uint32_t major = (uint32_t)((value >> 16) & 0xFFFFu);
    uint32_t minor = (uint32_t)(value & 0xFFFFu);
    if (major == 0u)
        return minor >= 2u && minor < 32u;
    return major < 3u && minor < 32u;
}

static uint32_t smp_probe_psci_conduit(uint32_t conduit, uint64_t *out_version, uint64_t *out_esr)
{
    uint64_t x0 = SMP_PSCI_VERSION;
    uint64_t x1 = 0u;
    uint64_t x2 = 0u;
    uint64_t x3 = 0u;
    uint64_t esr = 0u;
    int rc;

    if (out_version)
        *out_version = 0u;
    if (out_esr)
        *out_esr = 0u;

    if (conduit == SMP_PSCI_SMC)
        rc = asm_aa64_try_smc(0u, &x0, &x1, &x2, &x3, &esr);
    else if (conduit == SMP_PSCI_HVC)
        rc = asm_aa64_try_hvc(0u, &x0, &x1, &x2, &x3, &esr);
    else
        return 0u;

    if (out_version)
        *out_version = x0;
    if (out_esr)
        *out_esr = esr;
    return rc == 0 && smp_psci_version_plausible(x0);
}

static uint32_t smp_detect_psci_conduit(uint64_t rsdp_phys)
{
    const smp_acpi_sdt *fadt = smp_acpi_find_table(rsdp_phys, "FACP");
    const uint8_t *bytes = (const uint8_t *)fadt;
    uint16_t arm_boot_arch = 0u;
    uint64_t version = 0u;
    uint64_t esr = 0u;
    uint32_t conduit = SMP_PSCI_NONE;

    smp_log_hex("[K:SMP] ACPI RSDP=", rsdp_phys);

    if (!fadt || fadt->length < 111u)
    {
        smp_log_text("[K:SMP] FADT/FACP missing or too short; probing PSCI");
    }
    else
    {
        smp_log_hex("[K:SMP] FADT length=", fadt->length);
        arm_boot_arch = (uint16_t)bytes[109] | ((uint16_t)bytes[110] << 8);
        smp_log_hex("[K:SMP] FADT arm_boot_arch=", arm_boot_arch);
        if (arm_boot_arch & 1u)
            conduit = (arm_boot_arch & 2u) ? SMP_PSCI_HVC : SMP_PSCI_SMC;
        terminal_print("[K:SMP] FADT PSCI conduit: ");
        terminal_print(smp_conduit_name(conduit));
        terminal_flush_log();
    }

    if (conduit && smp_probe_psci_conduit(conduit, &version, &esr))
    {
        smp_log_hex("[K:SMP] PSCI VERSION via advertised conduit=", version);
        return conduit;
    }
    if (conduit)
    {
        smp_log_hex("[K:SMP] advertised PSCI probe failed version=", version);
        smp_log_hex("[K:SMP] advertised PSCI probe ESR=", esr);
    }

    if (smp_probe_psci_conduit(SMP_PSCI_SMC, &version, &esr))
    {
        smp_log_hex("[K:SMP] PSCI VERSION via SMC probe=", version);
        return SMP_PSCI_SMC;
    }
    smp_log_hex("[K:SMP] PSCI SMC probe failed version=", version);
    smp_log_hex("[K:SMP] PSCI SMC probe ESR=", esr);

    if (smp_probe_psci_conduit(SMP_PSCI_HVC, &version, &esr))
    {
        smp_log_hex("[K:SMP] PSCI VERSION via HVC probe=", version);
        return SMP_PSCI_HVC;
    }
    smp_log_hex("[K:SMP] PSCI HVC probe failed version=", version);
    smp_log_hex("[K:SMP] PSCI HVC probe ESR=", esr);

    return SMP_PSCI_NONE;
}

static uint64_t smp_read_mpidr(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
#else
    return 0u;
#endif
}

static uint64_t smp_mpidr_affinity(uint64_t mpidr)
{
    return mpidr & 0xFF00FFFFFFull;
}

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
__attribute__((naked)) void smp_secondary_entry(void)
{
    __asm__ volatile(
        "mov x19, x0\n"
        "ldr x1, [x19, #0]\n"
        "mov sp, x1\n"
        "mov x0, x19\n"
        "bl smp_secondary_main\n"
        "1: wfe\n"
        "b 1b\n");
}
#else
void smp_secondary_entry(void) {}
#endif

static int64_t smp_psci_cpu_on(uint64_t target_mpidr, uint64_t entry_phys, uint64_t ctx)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    register uint64_t x0 __asm__("x0") = SMP_PSCI_CPU_ON_64;
    register uint64_t x1 __asm__("x1") = target_mpidr;
    register uint64_t x2 __asm__("x2") = entry_phys;
    register uint64_t x3 __asm__("x3") = ctx;

    if (G_smp_psci_conduit == SMP_PSCI_HVC)
        __asm__ volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    else if (G_smp_psci_conduit == SMP_PSCI_SMC)
        __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    else
        return -99;
    return (int64_t)x0;
#else
    (void)target_mpidr;
    (void)entry_phys;
    (void)ctx;
    return -99;
#endif
}

void smp_secondary_main(smp_core *core)
{
    if (!core || core->magic != SMP_MAGIC)
        for (;;)
            asm_wait();

#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    __asm__ volatile("msr daifset, #0xf; isb" ::: "memory");
    asm_aa64_install_exception_vectors();
#endif
    asm_enable_fp_simd();
    core->state = SMP_CORE_ONLINE;
    core->worker_stage = 1u;
    core->online = 1u;

    for (;;)
    {
        smp_worker_poll_fn fn;
        core->poll_ticks = core->poll_ticks + 1u;
        core->worker_stage = 2u;
        fn = (smp_worker_poll_fn)(uintptr_t)core->worker_poll_entry;
        if (fn)
        {
            core->worker_stage = 3u;
            fn(core->logical_id);
            core->worker_stage = 4u;
        }
        asm_wait();
    }
}

void smp_init(uint64_t acpi_rsdp_phys)
{
    cpu_info_snapshot cpu;
    uint64_t boot_aff;
    uint64_t entry_phys;

    if (G_smp_initialized)
        return;

    cpu_info_get(&cpu);
    boot_aff = smp_mpidr_affinity(cpu.boot_mpidr);
    G_smp_psci_conduit = smp_detect_psci_conduit(acpi_rsdp_phys);
    G_smp_core_count = cpu.core_count < SMP_MAX_CORES ? cpu.core_count : SMP_MAX_CORES;

    smp_log_hex("[K:SMP] init core_count=", G_smp_core_count);
    smp_log_hex("[K:SMP] enabled_count=", cpu.enabled_count);
    terminal_print("[K:SMP] selected PSCI conduit: ");
    terminal_print(smp_conduit_name(G_smp_psci_conduit));
    terminal_flush_log();

    for (uint32_t i = 0u; i < G_smp_core_count; ++i)
    {
        smp_core *core = &G_smp_cores[i];
        core->magic = SMP_MAGIC;
        core->logical_id = i;
        core->acpi_uid = cpu.cores[i].acpi_uid;
        core->enabled = cpu.cores[i].enabled;
        core->mpidr = cpu.cores[i].mpidr;
        core->last_psci_status = -99;
        core->state = core->enabled ? SMP_CORE_DISCOVERED : SMP_CORE_ABSENT;
        if (smp_mpidr_affinity(core->mpidr) == boot_aff)
        {
            core->state = SMP_CORE_BOOT;
            core->online = 1u;
            smp_log_hex("[K:SMP] boot core logical=", i);
            smp_log_hex("[K:SMP] boot core mpidr=", core->mpidr);
        }
    }

    G_smp_initialized = 1u;

    if (!G_smp_psci_conduit)
    {
        smp_log_text("[K:SMP] no PSCI conduit; secondary cores remain parked");
        return;
    }

    entry_phys = pmem_virt_to_phys((const void *)(uintptr_t)&smp_secondary_entry);
    asm_sync_executable_range((const void *)(uintptr_t)&smp_secondary_entry, 128u);
    smp_log_hex("[K:SMP] secondary entry phys=", entry_phys);

    for (uint32_t i = 0u; i < G_smp_core_count; ++i)
    {
        smp_core *core = &G_smp_cores[i];
        uint64_t spins = 0u;
        if (!core->enabled || core->state == SMP_CORE_BOOT)
            continue;

        smp_log_hex("[K:SMP] CPU_ON logical=", i);
        smp_log_hex("[K:SMP] CPU_ON target mpidr=", smp_mpidr_affinity(core->mpidr));
        core->stack_base = pmem_alloc_pages(SMP_STACK_PAGES);
        if (!core->stack_base)
        {
            core->state = SMP_CORE_FAILED;
            core->last_psci_status = -12;
            smp_log_text("[K:SMP] CPU_ON failed: stack alloc");
            continue;
        }
        core->stack_top = (uint64_t)(uintptr_t)core->stack_base + (uint64_t)SMP_STACK_PAGES * 4096u;
        core->state = SMP_CORE_STARTING;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        asm_dma_clean_range(core, sizeof(*core));
        asm_dma_clean_range(core->stack_base, (uint64_t)SMP_STACK_PAGES * 4096u);
        core->last_psci_status = smp_psci_cpu_on(smp_mpidr_affinity(core->mpidr),
                                                 entry_phys,
                                                 (uint64_t)(uintptr_t)core);
        smp_log_hex("[K:SMP] CPU_ON status=", (uint64_t)core->last_psci_status);
        if (core->last_psci_status != 0)
        {
            core->state = SMP_CORE_FAILED;
            continue;
        }

        while (!core->online && spins++ < 1000000u)
            asm_relax();
        if (!core->online)
        {
            core->state = SMP_CORE_FAILED;
            smp_log_text("[K:SMP] CPU_ON timed out waiting for online");
        }
        else
        {
            smp_log_hex("[K:SMP] secondary online logical=", i);
        }
    }
}

void smp_set_worker_poll(smp_worker_poll_fn fn)
{
    uintptr_t entry = (uintptr_t)fn;
    for (uint32_t i = 0u; i < G_smp_core_count; ++i)
    {
        __atomic_store_n(&G_smp_cores[i].worker_poll_entry, entry, __ATOMIC_RELEASE);
        asm_dma_clean_range(&G_smp_cores[i], sizeof(G_smp_cores[i]));
    }
    smp_log_hex("[K:SMP] worker poll entry=", entry);
    smp_signal_workers();
}

void smp_signal_workers(void)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    __asm__ volatile("dsb sy; sev; isb" ::: "memory");
#endif
}

uint32_t smp_current_logical_id(void)
{
    uint64_t aff = smp_mpidr_affinity(smp_read_mpidr());
    for (uint32_t i = 0u; i < G_smp_core_count; ++i)
        if (smp_mpidr_affinity(G_smp_cores[i].mpidr) == aff)
            return i;
    return 0u;
}

void smp_mark_busy(uint32_t logical_id, uint32_t busy)
{
    if (logical_id < G_smp_core_count)
        G_smp_cores[logical_id].busy = busy ? 1u : 0u;
}

void smp_mark_job_done(uint32_t logical_id)
{
    if (logical_id < G_smp_core_count)
        G_smp_cores[logical_id].jobs_completed = G_smp_cores[logical_id].jobs_completed + 1u;
}

void smp_get_snapshot(smp_snapshot *out_snapshot)
{
    if (!out_snapshot)
        return;

    *out_snapshot = (smp_snapshot){0};
    out_snapshot->initialized = G_smp_initialized;
    out_snapshot->psci_available = G_smp_psci_conduit ? 1u : 0u;
    out_snapshot->psci_conduit = G_smp_psci_conduit;
    out_snapshot->core_count = G_smp_core_count;
    for (uint32_t i = 0u; i < G_smp_core_count; ++i)
    {
        smp_core *core = &G_smp_cores[i];
        smp_core_status *dst = &out_snapshot->cores[i];
        asm_dma_invalidate_range(core, sizeof(*core));
        dst->logical_id = core->logical_id;
        dst->state = core->state;
        dst->acpi_uid = core->acpi_uid;
        dst->enabled = core->enabled;
        dst->busy = core->busy;
        dst->jobs_completed = core->jobs_completed;
        dst->poll_ticks = core->poll_ticks;
        dst->worker_stage = core->worker_stage;
        dst->worker_poll_entry = core->worker_poll_entry;
        dst->last_psci_status = core->last_psci_status;
        dst->mpidr = core->mpidr;
        if (core->online)
            ++out_snapshot->online_count;
        if (core->online && core->state != SMP_CORE_BOOT)
            ++out_snapshot->worker_count;
    }
}
