#include <stdint.h>
#include <stdio.h>
#include "gpu/gpu_qcom_scm.h"

static unsigned calls;
static uint64_t query_status, available, service_status, service_result;
static int trap_call, bad_args;

int asm_aa64_try_smc6(uint32_t immediate, uint64_t *x0, uint64_t *x1,
                     uint64_t *x2, uint64_t *x3, uint64_t *x4,
                     uint64_t *x5, uint64_t *esr)
{
    ++calls;
    if (immediate) bad_args = 1;
    if (calls == 1) {
        if (*x0 != 0xc2000601u || *x1 != 1u || *x2 != 0x02000c1bu)
            bad_args = 1;
        *x0 = query_status;
        *x1 = available;
    } else {
        if (*x0 != 0xc2000c1bu || *x1 != 4u || *x2 != 0xffff0000u ||
            *x3 != 0xffffffffu || *x4 != 0xffffffffu || *x5 != 0xffffffffu)
            bad_args = 1;
        *x0 = service_status;
        *x1 = service_result;
    }
    *esr = trap_call == (int)calls ? 0x1234u : 0u;
    return trap_call == (int)calls ? -1 : 0;
}

static int run(uint64_t qs, uint64_t avail, uint64_t ss, uint64_t sr,
               int trap, int expected, unsigned expected_calls,
               uint64_t expected_status)
{
    uint64_t a = 99, s = 99, e = 99;
    calls = bad_args = 0;
    query_status = qs; available = avail;
    service_status = ss; service_result = sr; trap_call = trap;
    int rc = gpu_qcom_scm_open_gpu_smmu_aperture(0, &a, &s, &e);
    if (rc != expected || calls != expected_calls || bad_args || s != expected_status ||
        a != ((!qs && trap != 1) ? avail : 0) || e != (trap ? 0x1234u : 0)) {
        printf("FAIL rc=%d calls=%u status=%llu available=%llu esr=%llu args=%d\n",
               rc, calls, (unsigned long long)s, (unsigned long long)a,
               (unsigned long long)e, bad_args);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += run(0, 1, 0, 0, 0, 0, 2, 0);
    failures += run(0, 0, 0, 0, 0, 1, 1, 0);
    failures += run(~0ull, 1, 0, 0, 0, -4, 1, ~0ull);
    failures += run(0, 1, ~0ull, 0, 0, -4, 2, ~0ull);
    failures += run(0, 1, 0, 7, 0, -4, 2, 7);
    failures += run(0, 1, 0, 0, 1, -2, 1, 0);
    failures += run(0, 1, 0, 0, 2, -3, 2, 0);
    calls = 0;
    if (gpu_qcom_scm_open_gpu_smmu_aperture(256, 0, 0, 0) != -1 || calls)
        ++failures;
    printf("SCM ABI tests: %s (8 cases)\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
