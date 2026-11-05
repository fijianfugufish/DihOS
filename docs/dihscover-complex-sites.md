# Complex-site compatibility: native memory blobs

The YouTube guest report showed an exited renderer, status `0xB4`, with the
SACX diagnostic `Page: unexpected blob strategy`. This was a deliberate exit
from a diagnostic placeholder, before useful site compatibility could be
assessed. A host public-homepage fetch returned HTML and JavaScript asset links;
that probe does not establish successful rendering in DihOS.

The browser now supplies a native BlobRegistry backed by the pinned WebCore
BlobRegistryImpl. Memory Blob parts, slices, type/size queries, blob URL aliases,
reference handles, revocation and registration top-origin data use WebCore's
implementation. The registry also installs WebCore's built-in blob resource
loaders, keeping memory data out of the HTTP/USB/Hyper-V network transport.
No filesystem capability is granted. Internal file-backed registrations are
rejected, optional file fallbacks are discarded, and temporary-file requests
complete with an empty failure result. Memory use remains within the renderer's
existing process-owned allocator; this is not persistent blob/IndexedDB storage.

The ARM64 fixture uses Blob and FileReader for binary slices, XHR and Fetch for
blob URLs, revocation, an SVG blob image and a cross-origin iframe read attempt.
Its subsequent typed navigation, Back/Forward, scrolling and cleanup remain in
the same renderer. Network delivery is modeled, while DOM, JavaScript, blob
loading, origin checks and software painting are native WebCore/JSC code.

F12 now works while the address bar has focus and prints frontend state even
with a stopped renderer. Live renderer diagnostics are still forwarded. Native
JavaScript console warnings/errors are logged with control characters replaced,
a 319-byte line limit and at most 32 messages per renderer. Cookie/header values
are not added by the diagnostic code. An upstream EmptyChromeClient method was
marked final; a private header view changes only that annotation to permit the
console override. Cached engine headers/archives and vtable layout stay intact.

Build the renderer in WSL:

```sh
python3 tools/make_browser_blob_fixture.py
python3 tools/webcore_dihos/build_complex_sites.py
python3 tools/webcore_dihos/test_complex_sites.py --include-page
```

Host UI checks use `tools/test_dihscover_integration.ps1`. Build the frontend
with `sdk/sacx/apps/dihscover/build_webcore.ps1 -OutputDirectory
build/dihscover-complex-sites-validation/frontend -EngineDirectory
build/dihscover-complex-sites-validation/renderer`, then seal/check using
`tools/webcore_dihos/package_complex_sites.py` and its `--check` option.
The matching preceding kernel, request limits, school CA, fonts and resources
are preserved; the working checkout kernel and unrelated native changes are
not deployed by these build steps.

With the VM off, run Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-complex-sites-20261103
```

Retest YouTube, then Back or a typed Google URL without closing Dihscover. If
it remains blank, send F12 output, `WebCore console:` messages and `proc:status`.
Live YouTube UI compatibility is unverified. This milestone does not implement
video codecs, MediaSource, streaming media, audio playback or promise that every
site API is present. The pinned engine currently has `ENABLE_VIDEO`,
`ENABLE_MEDIA_SOURCE` and `ENABLE_WEB_AUDIO` disabled. Blob support alone cannot
play YouTube videos. The next capability should follow the new guest evidence.
