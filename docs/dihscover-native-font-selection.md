# Native WebCore font selection

The experimental font-enabled WebCore configuration now uses actual native
FreeType, HarfBuzz/ICU, Fontconfig and Cairo libraries. It does not search a
host font installation. This extends the [native text-painting milestone](dihscover-native-fonts.md);
it does not yet provide a complete WebCore browser or a rendered HTML page.

[Recorded validation](browser-evaluation/native-font-selection-20261031.json):
all 46 ARM64 regression groups and the focused 31-group font/graphics suite
pass. Initialization, face registration, matching and matched-pattern Cairo
painting make zero filesystem attempts. Ten actual WebCore units compile.
The final diagnostic exits with 114 and reclaims every process page. These
are QEMU results; Hyper-V and hardware runs remain outstanding.

Fontconfig 2.16.0 is pinned to the publisher's
[source archive and checksum](https://www.freedesktop.org/software/fontconfig/release/):
`6a33dc555cc9ba8b10caf7695878ef134eeb36d0af366041f639b1da9b6ed220`.
The native build uses real SACX wait/wake mutexes and kernel randomness. It
uses the fixed C locale. Its private directory, file, link and cache adapters
return failures with an error; none emulate successful disk access. Their
types and aliases are confined to this library, not a public POSIX ABI.

`dihos_fc_config_create_memory()` creates owned empty system/application font
sets and disables rescanning. The caller sets this configuration current
before querying faces, registers real FreeType metadata with
`dihos_fc_config_add_pattern()`, and loads alias rules from memory. Fontconfig
then performs its normal substitution, matching, coverage and fallback sorting.
Patterns borrow their `FT_Face` pointers: faces, their library and font buffers
must remain alive until all configurations and font caches release them.
Browser integration must initialize once before font-cache use and join users
before teardown; the diagnostic fixture tears everything down at test exit.

Memory faces use absolute virtual identifiers. Fontconfig's wrapper-format
inspection reads the first four bytes from a bounded FreeType memory stream;
it does not reopen a filename. Cairo's filename-only fallback routes to the
same counted access denial. Test-only linker wrappers log unexpected I/O,
while the successful run contains no such calls.

The packaged Noto sans, serif and monospace fonts are the test's only faces.
Tests check exact generic-family selection, fractional requests, Omega
coverage, fallback ordering and three concurrent matching workers. A denial
counter must be zero after initialization/registration and remain zero
through matching. The same executable runs the existing Unicode shaping,
ligature/combining-mark, zoom, Cairo painting, image-guard and worker tests.
The kernel harness verifies exit 114 and full process-page reclamation.

The earlier XML archive had been compiled against legacy global `errno`
headers. A separate memory-XML build now uses the proven native thread-local
error and stdio ABI. It disables file/network/module backends and XML features
not required for Fontconfig rules, including XPath, XPointer, canonicalization
and schemas. It is not validation of the complete libxml API. Native C-locale
classification, formatting, character output and binary search provide the
actual remaining C operations; they do not introduce host services.

From PowerShell, after the native font/graphics prerequisites:

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_font_xml.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_fontconfig.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_font_fixture.py --fontconfig
.\tools\test_user_runtime_qemu.ps1 -FontSelect -CairoRaster -ReusePrograms
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_fontconfig_graphics.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/configure_native.py --fonts
```

`build_fontconfig.py --reuse-config` is a development iteration option. Its
report intentionally marks the archive unvalidated until a normal fresh
configuration succeeds; downstream builders reject that intermediate report.
The full commands above require the native toolchain and verified prerequisite
reports. `-ReusePrograms` checks base fixtures; omit it to rebuild them, and
pass `-Python` when needed.

`configure_native.py --fonts` creates separate `*-cairo-fonts` source/build
trees and `build/webcore-fonts-native` reports. It binds imported CMake targets
to verified native archives and includes WebCore's upstream FreeType and Cairo
source lists. The original resource-only and image-only builds remain separate.

WebCore's font cache, platform font data, metrics, custom font data, HarfBuzz
controller and Cairo text-drawing units compile in this configuration, along
with Page, LocalFrame, Document and the HTML document parser. Those
object checks do not prove a complete WebCore link or document execution.
The remaining work includes process-owned font-registry lifecycle in the page
client, complete Page/Frame/platform clients, local DOM/script layout and
painting, window presentation and modern-site validation. No installed-browser
change, Hyper-V validation, physical hardware validation or YouTube support is
claimed by this milestone.
