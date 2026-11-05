# Native WebCore build

This experiment builds WebCore from the same WPEWebKit 2.54.2 source archive
as the tested native JavaScriptCore port. It uses `PORT=DihOS` and the
`aarch64-none-elf` toolchain in a separate source/build cache. The JSC-only
configuration explicitly disables WebCore and cannot serve as the browser
platform configuration.

The initial configuration milestone kept the shipping NetSurf browser, sealed
JSC archives and guest kernel unchanged. The subsequent
[resource-types milestone](dihscover-webcore-resource-types.md) now provides a
separately tested kernel/package. Neither milestone enables DOM execution or
page rendering in the shipping browser.

## Dependencies

The XML prerequisite uses the existing pinned libxml2 2.11.6 source archive in
the Mesa package cache, verified against its recorded upstream SHA-256. All
45 library build steps complete with the native toolchain. Network, dynamic
modules, catalogs and threading are disabled for this initial owner-thread
profile. Descriptor reads/writes/close and current-directory queries fail
explicitly; no POSIX authority is granted. Native stdio keeps its existing
restricted behavior. This older XML pin is an experimental build input and
needs a supported-version/security review before shipping the replacement
browser.

SQLite 3.54.1 is pinned to the official amalgamation SHA3-256 published on the
[SQLite download page](https://sqlite.org/download.html). It uses
`SQLITE_OS_OTHER`, no registered VFS, no loadable extensions, no WAL and a
single owner. It supplies real SQL code, but opening databases is unavailable
until a scoped VFS is implemented. It is not a persistent storage backend.

The memory-only compression archive uses zlib 1.3.2 and the SHA-256 published
on the [zlib release page](https://zlib.net/). It retains upstream allocator
defaults and excludes all gzip file units. The isolated `zutil.c` overlay
removes its unused gzip-file header include; it does not replace the inflater,
deflater or allocator with substitutes.

Upstream copyright/license files remain in each extracted source directory.
They accompany the subsequent resource-types diagnostic package.

## Reproduction

Run these commands in ARM64 WSL from the checkout, after the native JSC/ICU/
runtime reports have been built. Dependency archives must already be present;
the builders do not silently download or use installed Linux libraries.

```sh
python3 tools/webcore_dihos/build_xml.py
python3 tools/webcore_dihos/build_dependencies.py
python3 tools/webcore_dihos/configure_native.py
python3 tools/webcore_dihos/compile_native.py --target WTF_CopyHeaders --target JavaScriptCore_CopyHeaders --target PAL_CopyHeaders --target WebCore/DerivedSources/HTMLNames.h --target WebCore/DerivedSources/TagName.h --target WebCore/DerivedSources/CSSPropertyNames.h --target WebCore/DerivedSources/CSSValueKeywords.h --target WebCore/DerivedSources/HTMLEntityTable.cpp --target WebCore/PrivateHeaders/WebCore/ResourceError.h
python3 tools/webcore_dihos/compile_units.py
```

Download locations for the two added archives:

- `https://sqlite.org/2026/sqlite-amalgamation-3540100.zip`
- `https://zlib.net/zlib-1.3.2.tar.gz`

Place them in `build/browser-engine-arm64`. Exact digest checks are in the
builder. Reports and logs live in `build/webcore-xml`,
`build/webcore-dependencies`, and `build/webcore-native`.

The configure report records native compiler/link checks and preserves failed
checks. Missing dependencies are not replaced with dummy CMake targets.
Configuration now succeeds and generates actual WebCore, PAL, DOM/rendering,
style and binding targets. The SQLite target-name alias points at the real
native SQLite archive. The core platform removes the upstream unconditional
OpenGL link and rejects enabling a GL backend; optional LCMS, WOFF2, AVIF and
JPEG XL support are off during bring-up. No graphics implementation is implied.

The compilation runner defaults to the upstream HTML and CSS tokenizer object
targets, including their actual Ninja prerequisites and generated headers. It
records a bounded build attempt, compiler errors and exact object hashes when
successful. `--target` selects another genuine target and `--timeout` controls
the attempt deadline.

The initial full prerequisite build reached its 300-second deadline during
JavaScriptCore interpreter compilation. It did not reach the selected parser
objects. Its log/report remain under `full-prerequisites-timeout*`. Focused
compilation uses the unmodified compiler commands from CMake, with PCH disabled
through CMake itself. Only the output object location changes. The header-only
targets above run their genuine generators without rebuilding the whole engine.

Eight units now compile and pass little-endian ARM64 relocatable-object checks:
HTMLTokenizer, HTMLEntityParser, HTMLEntitySearch, SegmentedString, CSSTokenizer,
CSSParserToken, CSSTokenizerInputStream and ResourceErrorBase. The native
ResourceError type carries owned metadata and retains sanitization during
isolated copies, with no host transport object or implicit permission. The
already tested timer tree-removal patch is also applied in this separate tree.

The initial document-dependent HTMLParserOptions attempt stopped at the absent
native `ResourceResponse.h` contract. Its diagnostic is retained in
`document-options-attempt.log`. Implement ResourceRequest/ResourceResponse and
certificate-state contracts before connecting the actual WebCore resource
loader. Do not substitute Soup/Linux types or claim trusted TLS from missing
certificate information. The
[compile evidence](browser-evaluation/native-webcore-core-build-20261031.json)
does not claim a complete library, linked parser executable or guest execution.
The subsequent resource-types milestone implements that metadata boundary and
executes it in EL0; consult its reproduction commands and evidence for the
current state. The earlier compile-only JSON is historical.

Further work includes platform graphics/font clients, complete Page clients,
WebCore's resource-loader integration, checked window/input/surface services,
and actual local HTML/CSS/JavaScript page tests. JavaScript websites and video
playback remain unavailable in the shipping browser.
