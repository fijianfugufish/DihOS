#include "hyperv/hyperv.h"
#include "asm/asm.h"
#include "memory/pmem.h"
#include "terminal/terminal_api.h"

enum
{
    HV_X64_MSR_GUEST_OS_ID = 0x40000000u,
    HV_X64_MSR_HYPERCALL = 0x40000001u,
};

static void *G_hyperv_hypercall_page;
static uint64_t G_hyperv_hypercall_phys;
static uint32_t G_hyperv_core_ready;
static uint32_t G_hyperv_arm64_legacy_hvc;
static uint64_t G_hyperv_last_hvc_esr;
static uint32_t G_hyperv_last_hvc_immediate;

enum
{
    HV_STATUS_SUCCESS = 0,
    HV_CALL_SET_VP_REGISTERS = 0x0051u,
    HV_REGISTER_GUEST_OS_ID = 0x00090002u,
};

#define HV_PARTITION_ID_SELF (~0ull)
#define HV_VP_INDEX_SELF 0xFFFFFFFEu
#define HV_HYPERCALL_REP_COUNT(n) ((uint64_t)(n) << 32)
#define DIHOS_HYPERV_GUEST_ID 0x4449484F53000001ull

static void hyperv_zero_info(hyperv_info *out)
{
    if (!out)
        return;

    out->present = 0;
    out->hypervisor_bit = 0;
    out->max_leaf = 0;
    out->interface_id = 0;
    out->discovery = 0;
    for (uint32_t i = 0; i < sizeof(out->vendor); ++i)
        out->vendor[i] = 0;
}

#if defined(DIHOS_ARCH_X64) || defined(KERNEL_ARCH_X64) || defined(__x86_64__) || defined(_M_X64)
static void hyperv_cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    uint32_t ra = 0;
    uint32_t rb = 0;
    uint32_t rc = 0;
    uint32_t rd = 0;

    __asm__ __volatile__(
        "cpuid"
        : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
        : "a"(leaf), "c"(subleaf)
        : "memory");

    if (a)
        *a = ra;
    if (b)
        *b = rb;
    if (c)
        *c = rc;
    if (d)
        *d = rd;
}

static uint64_t hyperv_rdmsr(uint32_t msr)
{
    uint32_t lo = 0;
    uint32_t hi = 0;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static void hyperv_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

int hyperv_detect(hyperv_info *out, uint64_t acpi_rsdp)
{
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    hyperv_zero_info(out);
    if (!out)
        return -1;
    (void)acpi_rsdp;

    hyperv_cpuid(1u, 0u, &a, &b, &c, &d);
    out->hypervisor_bit = (c >> 31) & 1u;

    /*
     * Probe the reserved hypervisor leaf even when leaf 1 omits the
     * hypervisor-present bit. This also gives the early console a useful
     * signature when firmware or VM configuration masks that feature bit.
     */
    hyperv_cpuid(0x40000000u, 0u, &a, &b, &c, &d);
    out->max_leaf = a;
    ((uint32_t *)out->vendor)[0] = b;
    ((uint32_t *)out->vendor)[1] = c;
    ((uint32_t *)out->vendor)[2] = d;
    out->vendor[12] = 0;

    if (b != 0x7263694Du || c != 0x666F736Fu || d != 0x76482074u)
        return -1;

    out->present = 1;
    out->discovery = 1;
    if (out->max_leaf >= 0x40000001u)
    {
        hyperv_cpuid(0x40000001u, 0u, &a, &b, &c, &d);
        out->interface_id = a;
    }
    return 0;
}

int hyperv_core_init(const hyperv_info *info)
{
    uint64_t ctrl = 0;
    uint8_t *page = 0;

    if (G_hyperv_core_ready)
        return 0;
    if (!info || !info->present)
        return -1;

    G_hyperv_hypercall_page = pmem_alloc_pages(1);
    if (!G_hyperv_hypercall_page)
    {
        terminal_error("hyperv: hypercall page allocation failed");
        return -1;
    }

    page = (uint8_t *)G_hyperv_hypercall_page;
    for (uint32_t i = 0; i < 4096u; ++i)
        page[i] = 0;

    G_hyperv_hypercall_phys = pmem_virt_to_phys(G_hyperv_hypercall_page);

    /*
     * Non-zero guest OS ID is required before enabling the hypercall page.
     * This local ID is just a DihOS marker plus low version bits.
     */
    hyperv_wrmsr(HV_X64_MSR_GUEST_OS_ID, DIHOS_HYPERV_GUEST_ID);
    ctrl = (G_hyperv_hypercall_phys & ~0xFFFULL) | 1ull;
    hyperv_wrmsr(HV_X64_MSR_HYPERCALL, ctrl);
    ctrl = hyperv_rdmsr(HV_X64_MSR_HYPERCALL);

    if ((ctrl & 1ull) == 0u)
    {
        terminal_error("hyperv: hypercall page did not enable");
        return -1;
    }

    G_hyperv_core_ready = 1u;
    terminal_print("hyperv: hypercall page enabled phys=");
    terminal_print_inline_hex64(G_hyperv_hypercall_phys);
    return 0;
}

uint64_t hyperv_hypercall(uint64_t control,
                          uint64_t input_gpa,
                          uint64_t output_gpa)
{
    typedef uint64_t(__attribute__((ms_abi)) * hypercall_fn)(
        uint64_t, uint64_t, uint64_t);
    hypercall_fn call;

    if (!G_hyperv_core_ready || !G_hyperv_hypercall_page)
        return ~0ull;

    call = (hypercall_fn)G_hyperv_hypercall_page;
    return call(control, input_gpa, output_gpa);
}
#elif defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
enum
{
    /*
     * ARM_SMCCC_CALL_VAL(FAST, SMC32, VENDOR_HYP, QUERY_CALL_UID):
     * bit 31 | owner 6 in bits 29:24 | function 0xff01.
     */
    HV_ARM64_SMCCC_UID = 0x8600FF01u,
    HV_ARM64_UID_0 = 0x4D32BA58u,
    HV_ARM64_UID_1 = 0xCD244764u,
    HV_ARM64_UID_2 = 0x8EEF6C75u,
    HV_ARM64_UID_3 = 0x16597024u,
};

static void hyperv_arm64_uid(uint64_t *r0, uint64_t *r1,
                             uint64_t *r2, uint64_t *r3)
{
    register uint64_t x0 __asm__("x0") = HV_ARM64_SMCCC_UID;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;

    __asm__ __volatile__(
        "hvc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "memory");

    *r0 = x0;
    *r1 = x1;
    *r2 = x2;
    *r3 = x3;
}

static uint32_t hyperv_acpi_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t hyperv_acpi_u64(const uint8_t *p)
{
    return (uint64_t)hyperv_acpi_u32(p) |
           ((uint64_t)hyperv_acpi_u32(p + 4) << 32);
}

static int hyperv_acpi_sig(const uint8_t *p, const char *sig, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (p[i] != (uint8_t)sig[i])
            return 0;
    }
    return 1;
}

static int hyperv_acpi_fadt_is_hyperv(uint64_t rsdp_phys)
{
    const uint8_t *rsdp = (const uint8_t *)(uintptr_t)rsdp_phys;
    const uint8_t *root;
    uint64_t root_phys;
    uint32_t root_len;
    uint32_t entry_size;
    uint32_t entry_count;

    if (rsdp_phys < 0x1000ull ||
        !hyperv_acpi_sig(rsdp, "RSD PTR ", 8))
        return 0;

    if (rsdp[15] >= 2 && hyperv_acpi_u64(rsdp + 24) != 0)
    {
        root_phys = hyperv_acpi_u64(rsdp + 24);
        entry_size = 8;
    }
    else
    {
        root_phys = hyperv_acpi_u32(rsdp + 16);
        entry_size = 4;
    }

    if (root_phys < 0x1000ull)
        return 0;
    root = (const uint8_t *)(uintptr_t)root_phys;
    if ((entry_size == 8 && !hyperv_acpi_sig(root, "XSDT", 4)) ||
        (entry_size == 4 && !hyperv_acpi_sig(root, "RSDT", 4)))
        return 0;

    root_len = hyperv_acpi_u32(root + 4);
    if (root_len < 36 || root_len > (1024u * 1024u))
        return 0;
    entry_count = (root_len - 36u) / entry_size;

    for (uint32_t i = 0; i < entry_count; ++i)
    {
        uint64_t table_phys = entry_size == 8
                                  ? hyperv_acpi_u64(root + 36u + i * 8u)
                                  : hyperv_acpi_u32(root + 36u + i * 4u);
        const uint8_t *table;
        uint32_t table_len;

        if (table_phys < 0x1000ull)
            continue;
        table = (const uint8_t *)(uintptr_t)table_phys;
        if (!hyperv_acpi_sig(table, "FACP", 4))
            continue;

        table_len = hyperv_acpi_u32(table + 4);
        if (table_len >= 276u &&
            hyperv_acpi_sig(table + 268u, "MsHyperV", 8))
            return 1;
    }
    return 0;
}

int hyperv_detect(hyperv_info *out, uint64_t acpi_rsdp)
{
    uint64_t uid0 = 0;
    uint64_t uid1 = 0;
    uint64_t uid2 = 0;
    uint64_t uid3 = 0;
    static const char vendor[] = "Microsoft Hv";

    hyperv_zero_info(out);
    if (!out)
        return -1;

    hyperv_arm64_uid(&uid0, &uid1, &uid2, &uid3);
    if ((uint32_t)uid0 == HV_ARM64_UID_0 &&
        (uint32_t)uid1 == HV_ARM64_UID_1 &&
        (uint32_t)uid2 == HV_ARM64_UID_2 &&
        (uint32_t)uid3 == HV_ARM64_UID_3)
    {
        out->discovery = 2;
    }
    else if (hyperv_acpi_fadt_is_hyperv(acpi_rsdp))
    {
        out->discovery = 3;
    }
    else
    {
        terminal_warn("hyperv: ARM64 SMCCC UID mismatch");
        terminal_print("hyperv: uid0=");
        terminal_print_inline_hex64(uid0);
        terminal_print(" uid1=");
        terminal_print_inline_hex64(uid1);
        terminal_print(" uid2=");
        terminal_print_inline_hex64(uid2);
        terminal_print(" uid3=");
        terminal_print_inline_hex64(uid3);
        return -1;
    }

    out->present = 1;
    out->hypervisor_bit = 1;
    for (uint32_t i = 0; i < sizeof(vendor); ++i)
        out->vendor[i] = vendor[i];
    return 0;
}

int hyperv_core_init(const hyperv_info *info)
{
    uint64_t status;
    uint64_t current_el;

    if (G_hyperv_core_ready)
        return 0;
    if (!info || !info->present)
        return -1;

    __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(current_el));
    current_el = (current_el >> 2) & 3u;
    terminal_print("hyperv: ARM64 CurrentEL=");
    terminal_print_inline_hex32((uint32_t)current_el);
    if (current_el != 1u)
    {
        terminal_error("hyperv: HVC guest ABI requires EL1");
        return -1;
    }

    /*
     * Hyper-V ARM64 systems which advertise through ACPI but reject the
     * standard SMCCC UID call use the historical HVC #1 register ABI.
     */
    G_hyperv_arm64_legacy_hvc = (info->discovery == 3) ? 1u : 0u;

    /*
     * A single ARM64 VP-register write is a fast REP hypercall. Hyper-V
     * consumes the partition, VP, register name, and value directly from
     * X2 through X7; it does not consume the memory packet used by x64.
     */
    status = ~0ull;
    G_hyperv_last_hvc_esr = 0u;
    if (asm_aa64_try_hv_set_vpreg(HV_REGISTER_GUEST_OS_ID,
                                  DIHOS_HYPERV_GUEST_ID,
                                  &status,
                                  &G_hyperv_last_hvc_esr) != 0 ||
        (uint16_t)status != HV_STATUS_SUCCESS)
    {
        terminal_error("hyperv: set GuestOsId hypercall failed status=");
        terminal_print_inline_hex64(status);
        terminal_print("hyperv: HVC exception ESR=");
        terminal_print_inline_hex64(G_hyperv_last_hvc_esr);
        return -1;
    }

    G_hyperv_arm64_legacy_hvc = 0u;
    G_hyperv_core_ready = 1u;
    terminal_print("hyperv: ARM64 HVC ready; GuestOsId registered");
    return 0;
}

uint64_t hyperv_hypercall(uint64_t control,
                          uint64_t input_gpa,
                          uint64_t output_gpa)
{
    if (!G_hyperv_core_ready)
        return ~0ull;

    if (G_hyperv_arm64_legacy_hvc)
    {
        uint64_t x0 = control;
        uint64_t x1 = input_gpa;
        uint64_t x2 = output_gpa;
        uint64_t x3 = 0u;

        G_hyperv_last_hvc_immediate = 1u;
        if (asm_aa64_try_hvc(1u, &x0, &x1, &x2, &x3,
                             &G_hyperv_last_hvc_esr) == 0 &&
            x0 != ~0ull)
            return x0;

        /*
         * Some ARM64 Hyper-V firmware advertises via ACPI while only
         * accepting the newer SMCCC HVC #0 convention. The guarded call
         * makes this fallback safe even when that conduit is absent.
         */
        x0 = 0x46000001u;
        x1 = control;
        x2 = input_gpa;
        x3 = output_gpa;
        G_hyperv_last_hvc_immediate = 0u;
        G_hyperv_last_hvc_esr = 0u;
        if (asm_aa64_try_hvc(0u, &x0, &x1, &x2, &x3,
                             &G_hyperv_last_hvc_esr) == 0)
        {
            G_hyperv_arm64_legacy_hvc = 0u;
            return x0;
        }
        return ~0ull;
    }
    else
    {
        uint64_t x0 = 0x46000001u;
        uint64_t x1 = control;
        uint64_t x2 = input_gpa;
        uint64_t x3 = output_gpa;

        G_hyperv_last_hvc_immediate = 0u;
        if (asm_aa64_try_hvc(0u, &x0, &x1, &x2, &x3,
                             &G_hyperv_last_hvc_esr) != 0)
            return ~0ull;
        return x0;
    }
}
#else
int hyperv_detect(hyperv_info *out, uint64_t acpi_rsdp)
{
    hyperv_zero_info(out);
    (void)acpi_rsdp;
    return -1;
}

int hyperv_core_init(const hyperv_info *info)
{
    (void)info;
    return -1;
}

uint64_t hyperv_hypercall(uint64_t control,
                          uint64_t input_gpa,
                          uint64_t output_gpa)
{
    (void)control;
    (void)input_gpa;
    (void)output_gpa;
    return ~0ull;
}
#endif

int hyperv_core_ready(void)
{
    return G_hyperv_core_ready ? 1 : 0;
}

int hyperv_set_vpreg(uint32_t reg, uint64_t value)
{
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    uint64_t status = ~0ull;
    uint64_t esr = 0u;

    if (!G_hyperv_core_ready)
        return -1;
    if (asm_aa64_try_hv_set_vpreg(reg, value, &status, &esr) != 0)
    {
        terminal_error("hyperv: VP register HVC exception ESR=");
        terminal_print_inline_hex64(esr);
        return -1;
    }
    if ((uint16_t)status != HV_STATUS_SUCCESS)
    {
        terminal_error("hyperv: VP register write failed reg=");
        terminal_print_inline_hex32(reg);
        terminal_print(" status=");
        terminal_print_inline_hex64(status);
        return -1;
    }
    return 0;
#else
    (void)reg;
    (void)value;
    return -1;
#endif
}

void hyperv_log_detection(const hyperv_info *info)
{
    if (!info || !info->present)
        return;

    terminal_print("hyperv: detected vendor=");
    terminal_print_inline(info->vendor);
#if defined(DIHOS_ARCH_AARCH64) || defined(KERNEL_ARCH_AA64) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    terminal_print(info->discovery == 3
                       ? " discovery=ACPI-FADT transport=HVC"
                       : " discovery=SMCCC transport=HVC");
#else
    terminal_print(" hv_bit=");
    terminal_print_inline_hex32(info->hypervisor_bit);
    terminal_print(" max_leaf=");
    terminal_print_inline_hex32(info->max_leaf);
    terminal_print(" interface=");
    terminal_print_inline_hex32(info->interface_id);
#endif
}
