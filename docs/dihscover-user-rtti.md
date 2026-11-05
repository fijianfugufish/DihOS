# Native C++ RTTI for engine dependencies

ICU 78.2's normalizer uses `dynamic_cast`; the no-RTTI runtime profile cannot
compile it. The selected LLVM libc++abi 22.1.0 RTTI implementation now builds
as a freestanding ARM64 archive and passes an isolated EL0 fixture in QEMU.
Exceptions remain disabled. Other applications can keep their no-RTTI profile.

The source commit matches libc++:
`4434dabb69916856b824f68a64b029c67175e532`. `prepare_user_abi.py` verifies each
selected file against that commit's Git blob identity and retains the licence.
The bundle contains upstream type hierarchy traversal, type-info objects and
exception class metadata; it is not a replacement cast algorithm.

The small native adapter provides byte-correct `strcmp` and process termination
for fatal ABI invariants. It logs the diagnostic format literally, without
printf interpolation. Throwing casts terminate through the existing no-exception
runtime. Exception unwinding is not implemented.

## Reproduce

After the [JSC target inventory](browser-evaluation/jsc-dihos.md), prepare the
matching source bundle from PowerShell:

```powershell
python tools/prepare_user_abi.py --fetch
```

From the repository in ARM64 WSL:

```sh
python3 tools/build_user_rtti.py --sdk-report build/jsc-dihos/pre-icu-report.json
```

Then run the existing EL0 integration suite from PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Rtti -Python <python-executable>
```

The fixture stays in `build/user-rtti/rtti.elf`; it is not automatically added
to a VHDX. The optional test embeds that exact ELF alongside the existing
runtime fixtures. Check the QEMU log for `PASS: native libc++abi RTTI in EL0`.
If no engine retry has overwritten the original SDK report, omit `--sdk-report`
to use `build/jsc-dihos/report.json` directly.

## Recorded validation

On 2026-10-29 the 47,080-byte static PIE linked with no undefined symbols:
SHA-256 `51b9c5adb934d5103ee90886d28c0b5a010237e54a4ecc9aaba111092e3b3000`.
Four EL0 launches passed virtual-diamond traversal, sibling cross-casting,
null/private-path rejection, complete-object adjustment and dynamic type
identity, with resources reclaimed after every launch. Exit status is 88
(`0x58`). The existing synchronization, thread, large-image and fault-isolation
suite also passed. Hyper-V and native hardware have not run this fixture yet.

Reports are in `build/user-rtti/report.json` and `build/user-runtime/qemu.log`.
This removes one actual ICU runtime dependency; it does not establish a complete
ICU/JSC link or JavaScript execution inside DihOS.
