# Dihscover: existing UI, WebCore backend

The normal `OS/System/Programs/Dihscover/Dihscover.sacx` now builds from the
existing `main.cpp`. Its window, search/address textbox, five toolbar buttons,
status/progress controls and preferences are retained. `webcore_backend.cpp`
implements the existing backend interface; the historical `netsurf` function
names are compatibility names in that interface. No second browser toolbar is
compiled into the renderer used by this application.

Open Dihscover from Explorer as usual. Its existing Go/Enter action sends the
address or search to WebCore. Back, Forward, Reload/Stop, page input, font
selection and zoom use the same controls. Font, minimum-size and zoom settings
continue to use `0:/Documents/Dihscover.ini`. Background windows do not poll
browser shortcuts, and leaving the viewport releases a held page button.

WebCore runs in an EL0 helper stored inside Dihscover's application directory as
`WebCore.bin`. The existing frontend starts it asynchronously. Its owned pixel
surface is attached to the existing page viewport, rather than creating another
desktop window. Navigation, input and preferences cross a bounded value-copy
bridge; title, URL, status and cursor updates return through that bridge.
Messages contain copied values; widget pointers and callbacks are not shared
with the renderer.

The renderer's extra grants remain network access and owned surfaces.
The established SACX frontend still uses the existing trusted GUI API; migrating
that frontend to the future fully restricted application ABI is separate work.
The append-only browser extension preserves the pinned base SDK layout. The
kernel and application must be updated together; the package includes both.

## Install this milestone

With the VM turned off, run in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-webcore-integrated-20261102
```

Start the VM and open the usual Dihscover application. No manual helper launch
is part of normal browsing. Updates preserve user-created files and preferences.

## Build and verify

Use the existing pinned native WebCore/dependency build described in
`dihscover-native-webcore-build.md`, then build its embedded frontend adapter
in ARM64 WSL from the repository directory:

```sh
python3 tools/webcore_dihos/compile_page_fixture.py --report build/webcore-engine-openssl/report.json --embedded
python3 tools/webcore_dihos/build_page_fixture.py --report build/webcore-engine-openssl/report.json --strip
```

Build the normal frontend and stage its engine binary:

```powershell
.\sdk\sacx\apps\dihscover\build.ps1
```

This produces `build/sacx/Dihscover.sacx`, `Dihscover-WebCore.bin` and an engine
metadata file with hashes. `tools/hyperv.ps1 -Action Package -IncludeDihscover`
includes and checks both components. Use `-Engine NetSurf` with the application
build script when explicitly building the older backend.

The repeatable checks are:

```powershell
.\tools\test_user_runtime_qemu.ps1 -NativePage -UserHttp -Python <python.exe>
.\tools\test_dihscover_integration.ps1
```

The host tests compile the actual existing `main.cpp` and verify its controls,
search submission, preference persistence, URL callbacks, foreground input and
close behaviour. Separate tests execute the kernel attachment/lifecycle code,
ABI layout, loader ownership, checked copies, command ordering and overflow
recovery. The real ARM64 WebCore ELF runs in QEMU against production runtime,
window, HTTP and bridge code, exercising redirects, CSS/JavaScript pixels,
resize, Stop/Reload, form editing and DOM cancellation, history and cleanup.
QEMU's network transport is modeled; this is not a live Hyper-V application
smoke test. The previous HTTPS milestone's transport and trust-root sources are
retained unchanged.

Current limits include one WebCore browser session, a page surface up to
1024×768, 64 KiB responses, incomplete bitmap decoding, disabled cookies and no
media playback. Clipboard, IME and undo/redo are also pending. This integration
does not establish YouTube compatibility.
