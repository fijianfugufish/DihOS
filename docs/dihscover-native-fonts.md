# Native WebCore font bring-up

The native EL0 font build uses the existing pinned FreeType 2.14.1 and
HarfBuzz 11.4.5 source archives. It links the proven native ICU adapter and
libc++ thread implementation. HarfBuzz uses real `std::mutex` support;
`HB_NO_MT` is not enabled. The legacy NetSurf font build remains separate.

[Recorded validation](browser-evaluation/native-fonts-20261031.json): the final
painting fixture passes all 31 runtime/graphics/font groups in ARM64 QEMU,
with exit 114, intact image guards and complete process-page reclamation.
The production loader accepts its 42,544,984-byte ELF, with 4,882 relative
relocations, 1,224 TLS bytes and zero unresolved imports. Existing limits are
unchanged. These results have not yet been reproduced on Hyper-V or hardware.

Fonts come from the already packaged `OS/System/Fonts/Web` Noto sans, serif
and monospace files and their OFL notices. The diagnostic executable embeds
these files and opens FreeType memory faces. It neither reads Windows fonts
nor acquires a filesystem or network capability. Future document font loading
still needs a checked resource/permission path.

Build from PowerShell with the native runtime, ICU and Cairo prerequisites
already present:

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_fonts.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_font_fixture.py
.\tools\test_user_runtime_qemu.ps1 -NativeFonts -CairoRaster -ReusePrograms
```

The shaping fixture tests proportional versus fixed advances, fractional
sizes, real ascender/descender metrics, Unicode byte clusters, OpenType
ligatures, combining-mark normalization, zoom, missing glyph detection,
malformed font rejection, nonempty FreeType bitmap pixels and three concurrent
workers using separate faces. All 31 runtime/graphics/font groups passed.

The painting variant builds a separate Cairo library with its FreeType backend
enabled and Fontconfig disabled. It adds FreeType's format-query and LCD-filter
modules to a copy of the font archive; the image-only Cairo and tested font
archives are preserved. Cairo's compiler stack-allocation extension is bound
explicitly to `__builtin_alloca` through a private ABI header.

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_font_graphics.py
wsl.exe -d Ubuntu --cd $PWD.Path -- python3 tools/webcore_dihos/build_font_fixture.py --paint
.\tools\test_user_runtime_qemu.ps1 -FontPaint -CairoRaster -ReusePrograms
```

The painting fixture feeds HarfBuzz glyph IDs and fractional positions to
`cairo_show_glyphs` using the same FreeType memory face. It checks painted
pixels, baseline/advance bounds and image guard bytes. This is text painting
into an owned image surface, not yet a WebCore document or desktop window.

Each builder records source archive pins, configuration/ABI hashes, commands
and target library digests. The diagnostic ELF must pass the production
loader validator and contain no unresolved imports. The harness checks bounded
runtime service, expected exit 114 and complete process-page reclamation.

WebCore's upstream FreeType integration also depends on Fontconfig and its
font-selection contracts. Those are not provided by merely linking HarfBuzz
or Cairo. The remaining work includes font selection/fallback, WebCore's
font-cache integration, Page/Frame clients and document painting/presentation.
No Arabic/emoji coverage, downloaded-font validation, full CSS font conformance,
live JavaScript website support or YouTube playback is established by these
fixtures. Hyper-V and physical hardware font validation remain outstanding.

The subsequent [native font-selection milestone](dihscover-native-font-selection.md)
adds real Fontconfig matching/fallback and its Cairo backend. Its 46-group
regression pass and ten WebCore compile checks are recorded separately;
Page/Frame client integration and actual HTML rendering remain unfinished.
