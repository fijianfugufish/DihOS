# ICU 78.2 for the native DihOS engine build

All 456 upstream common/internationalization translation units compile for
`aarch64-none-elf` with the native SACX SDK. This produces `libicuuc.a` and
`libicui18n.a`; a separate target archive contains the actual Unicode data.
These are target archives, not a completed dependency link or a guest ICU test.
Unresolved runtime services must still be supplied at the final executable link.

The pinned [official ICU release](https://github.com/unicode-org/icu/releases/tag/release-78.2)
source archive is `icu4c-78.2-sources.tgz`, 27,970,651 bytes, SHA-256
`3e99687b5c435d4b209630e2d2ebb79906c984685e78635078b672e03c89df35`.
This matches the version used by the earlier ARM64 Linux reference build;
the target does not link its Linux libraries.

## Native profile

`tools/jsc_dihos/icu-platform.h` identifies the actual ARM64 little-endian,
four-byte-wchar ABI. Byte strings use UTF-8 and wide code units use UTF-32.
File access, dynamic loading and POSIX platform services are disabled.
Timezone/locale environment services are not declared available. The private
locale parser still requires C category constants; `icu-locale.h` supplies
those tags without implementing or claiming any OS service.

ICU's actual mutex/condition-variable implementation compiles against SACX's
standard C++ synchronization adapter. Its normalizer requires `dynamic_cast`,
so these library objects enable RTTI while retaining the no-exception profile.
The matching [libc++abi RTTI slice](../dihscover-user-rtti.md) passes its native
EL0 QEMU fixture. ICU itself still needs final link/runtime validation.

The data archive contains all 33,107,248 bytes of the packaged `icudt78l.dat`,
not stub data. Its data hash is
`a3d49eacd189624769dc77457c6aa1789a89219fb217a6d87d5b076c269aaf1f`.
The builder verifies its ICU common-data header, endianness, character family
and UChar size, then embeds it read-only at 16-byte alignment under
`icudt78_dat`. Final linked size must be measured against the ELF loader's
limits before anything is packaged for the guest; archive sizes alone do not
measure the final image.

## Reproduce

Place the verified source archive under `build/browser-engine-arm64`. First run
the [JSC SDK inventory](jsc-dihos.md) and retain that completed report before
retrying engine builds. From the repository directory in ARM64 WSL:

```sh
cp build/jsc-dihos/report.json build/jsc-dihos/pre-icu-report.json
python3 tools/icu_dihos_probe.py --all --sdk-report build/jsc-dihos/pre-icu-report.json
python3 tools/build_icu_data.py
python3 tools/stage_icu_dihos.py
python3 tools/jsc_dihos_probe.py --inventory --icu build/icu-dihos/stage-report.json
```

Prepare/build the matching RTTI slice before the final command, following its
documentation; `build_user_rtti.py` also accepts `--sdk-report` with the retained
report path. The selected `libdihos-libcxx.a` must exist from the standard user
runtime build. No Linux libc, libm, pthread or ICU archive is substituted.

The default ICU invocation compiles a representative subset only. Use `--all`
for complete archives. `--inventory` records errors without returning failure;
it is useful for diagnostics but does not make failed builds pass. Successful
unchanged objects are reused only after checking source, arguments, SDK/profile
inputs, object hash and log hash. Output reports identify cached objects.

The staging script verifies archived outputs, public headers, source pin,
profile and SDK source hashes before copying to the experimental target
sysroot. It retains the ICU licence. This sysroot is a development artifact;
it is not copied to a VHDX. After staging, JSC requires the explicit `--icu`
report so it can verify the target inputs before using them.

Reports are under `build/icu-dihos`: `report.json`, `data-report.json` and
`stage-report.json`. Logs are overwritten on retries. Source extraction lives
under `~/.cache/dihos-jsc-target`; original source files are checked against
the pinned archive rather than patched in place.

The [retained build evidence](icu-dihos-78.2.json) records 202 common and 254
internationalization source files compiled with zero failures. JavaScriptCore's
subsequent native configure finds these archives and passes; its build then
exposes the separate native allocator/platform work. No ICU guest execution is
claimed by those results.
