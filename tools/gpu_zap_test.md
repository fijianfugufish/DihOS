# X1-85 zap bring-up, revision 48

The loader in `kernel/gpu/gpu_zap.c` authenticates the existing operator-provided
`OS/Firmware/adreno-x1-85/oem/qcdxkmsuc8380.mbn` using Qualcomm PAS before
the runtime submits CP_SET_SECURE_MODE. It does not replace signature checking
with a file hash and does not force the trust register from EL1.

The intentionally narrow ELF32 parser accepts this monolithic three-header,
one-relocatable-load-segment format only. Metadata is the ELF/header prefix
concatenated with the hash segment. Secure firmware, not this parser, validates
the cryptographic signature. Revision 48 uses the Lenovo 83ED active Windows
GPU driver's local blob rather than the generic reference-board blob. See
the OEM directory's README for provenance and hash. Hardware acceptance is
not established by the parser or by the fact that this is an installed file.

Memory is a private pmem allocation, with a 1-MiB-aligned image subrange and
separate 4-KiB metadata page. It is not an assumed platform carveout. PAS must
accept that relocation. Exclusively owned identity mappings are made
Normal non-cacheable (MAIR 0x44), inner-shareable before secure calls; Device
memory is not a valid substitute for this shared RAM. The mapper reuses an
existing MAIR slot and fails closed if none is available. Revision 46 reserves
an unused slot on the boot CPU before SMP startup, after scanning the active
TTBR0 and (when enabled) TTBR1 leaf mappings. It does not infer that a zero MAIR
byte means an unused slot. These PAS buffers remain private to the boot CPU;
moving PAS work to another CPU requires matching MAIR setup there first.
Revision 47 corrects the revision-46 boot crash: the root scan uses the entry
count derived from TCR.TnSZ and the corresponding TTBR alignment, rather than
reading 512 descriptors from a shortened root. All descriptor reads are
guarded, and a failed scan leaves MAIR unchanged. The portable scanner is
tested by `tools/aarch64_attr_scan_test.c`, including garbage immediately after
a short root, inaccessible child tables, invalid leaves and exhausted budgets.
Revision 44 incorrectly
used Device-nGnRE, and the hardware run trapped during INIT_IMAGE with
ESR 0x96000035 (unsupported exclusive/atomic access). Revision 45 corrects
the memory type. The revision-45 run stopped safely because inherited MAIR
was 0xFFBB0400 with no Normal-NC slot. Hardware authentication remains to be tested.
The pages remain reserved for the entire
boot, including failed or unfinished authentication. There is deliberately no
automatic shutdown/retry/free sequence against uncertain secure-world state.

PAS calls use standard 64-bit SiP transport, GPU peripheral 13. Interrupted,
busy, and wait-queue results currently fail closed (no resume protocol yet).
Each stage logs raw x0 transport, x1 service result and trapped ESR separately.
If the platform demands a reserved region or additional platform setup, that
must be established from platform evidence, not guessed from a physical address.

## Host regression tests (PowerShell, repository root)

```powershell
& 'C:\Program Files\LLVM\bin\clang.exe' -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -I kernel/include -idirafter include tools/gpu_zap_test.c kernel/gpu/gpu_zap.c kernel/gpu/gpu_zap_image.c kernel/gpu/gpu_qcom_scm.c -o build/gpu_zap_test.exe
if ($LASTEXITCODE) { throw 'test compile failed' }
.\build\gpu_zap_test.exe 0
if ($LASTEXITCODE) { throw 'success-path test failed' }
foreach ($kind in 0..2) {
    foreach ($stage in 1..5) {
        .\build\gpu_zap_test.exe $stage $kind
        if ($LASTEXITCODE) { throw 'failure-path test failed' }
    }
}
```

Tests use the actual MBN plus corrupted/truncated variants, mock all five SMC
stages, check register arguments, packed metadata, relocated bytes/zero fill,
and ensure neither failed nor successful starts repeat firmware calls.
They do not validate actual TrustZone acceptance or GPU output.
Pass `OS/Firmware/adreno-x1-85/oem/qcdxkmsuc8380.mbn` as the third argument
after stage and failure kind to test the local OEM blob; the default remains
the redistributable upstream fixture.

On hardware, run `mesart:triangle start` once (`mesart:triangle` alone reports
status). Expected progression is availability,
GPU-13 support, INIT_IMAGE, MEM_SETUP, AUTH_AND_RESET, then the existing CP
handoff and pixel-change validation. Preserve the full terminal log if it fails.

Protocol references (not copied implementations):
- https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/firmware/qcom/qcom_scm.c
- https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/firmware/qcom/qcom_scm.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/soc/qcom/mdt_loader.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/msm/adreno/a6xx_gpu.c
