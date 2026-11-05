# CSS tokenizer allocation failure

The guest supplied PC `0x1701A744` and image base `0x15EF2000`. Their difference,
`0x1128744`, resolves in the exact deployed renderer to
`WebCore::CSSTokenizer::tokenRange() const`, at `ldr x0, [x0, #0x30]`.
The reported FAR was `0x30`. A large-CSS fixture reproduces that same relative
instruction and invalid address in the unchanged deployed binary. A separate
failed/corrupt-image fixture completes without a renderer fault: the preceding
`missingImage` resource warning was not sufficient to diagnose the crash.

CSSTokenizer::tryCreate can return null when its token vector cannot reserve
space. Eight CSSParser entry points used it without checking. A private copy of
the pinned parser translation unit now rejects allocation failure before token
access: property parsing returns an error, declaration updates return false,
rule parsing returns null, stylesheet parsing marks the header invalid, and
inspector parsing returns without processing an invalid tokenizer. Successful
parsing is unchanged. The cached WebCore source and archives remain untouched.

The general userspace allocator previously rejected a single allocation above
8 MiB, even when a 16 MiB pool could satisfy it. The ceiling is now
`16 MiB - 64 KiB`, leaving space for metadata/alignment within the existing
16 MiB kernel region limit. Pool count, process memory caps, owner checks and
syscalls are unchanged. The renderer links a rebuilt allocator object ahead of
the unchanged runtime archive. Oversized input remains bounded; guards are
required even with the increased ceiling.

The native fixture successfully parses a larger stylesheet whose initial token
reservation exceeds the former ceiling, then attempts larger stylesheet,
inline/declaration, rule and custom-property inputs that cannot fit. It verifies
that rejected input does not replace the prior style, and continues through
Blob/image, iframe, typed navigation, Back/Forward, Stop/Reload and cleanup tests.
Network delivery is modeled; DOM, parser, allocation, painting and EL0 are real.
The host allocator passes ASan/UBSan, 12 MiB allocation and cleanup, bounded
16 MiB rejection, randomized operations, exhaustion and concurrent workers.

Rebuild in WSL with `python3 tools/webcore_dihos/build_css_parser.py`, then
`python3 tools/webcore_dihos/test_css_parser.py --include-page`.
`test_css_parser_repro.py` deliberately runs the failing fixture against the
unchanged preceding renderer; its failure is expected and preserved separately.
`tools/test_user_allocator_host.sh` supplies allocator sanitizer checks.
Build the frontend using `build_webcore.ps1 -OutputDirectory
build/dihscover-css-parser-validation/frontend -EngineDirectory
build/dihscover-css-parser-validation/renderer`, and seal/check using
`tools/webcore_dihos/package_css_parser.py` and `--check`.

With the VM off, run Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-css-parser-20261103
```

Retest YouTube and navigation back to Google. The reported crash path is
reproduced and fixed in tests; live YouTube compatibility remains unverified.
Video, MediaSource and audio are still disabled. A stylesheet that exceeds the
new resource ceiling is rejected, so unusually large pages may lose styling
rather than crashing. This update does not grant filesystem access or change
the working-tree kernel, selected kernel, school CA, fonts or native GPU work.
