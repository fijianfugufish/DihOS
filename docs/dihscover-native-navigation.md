# Native WebCore HTTP navigation preview

The subsequent [verified HTTPS update](dihscover-native-https.md) adds packaged
root certificates and enables authenticated HTTPS. The instructions below
describe the earlier HTTP milestone.

This preview connects a real WebCore Page and Frame loader to the checked SACX
network transport. Rendering and JavaScript execute inside the ARM64 DihOS EL0
process. It opens `http://example.com/`, follows HTTP links, loads external CSS
and JavaScript, and offers pointer-operated Reload and Stop controls.

It is packaged as `UserRuntime/web.elf`; the installed NetSurf Dihscover stays
available. This is an integration preview rather than a replacement browser.

## Try on Hyper-V

Turn the VM off. From Administrator PowerShell in the checkout:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-native-navigation-20261101
```

After booting DihOS:

```text
proc:run-web 0:/OS/System/Programs/UserRuntime/web.elf
```

`proc:run-web` explicitly grants network access and one owned window. Normal
`proc:run`, `proc:run-ui` and `proc:run-net` keep their separate permissions.
`proc:status` reports startup progress and task state. Closing the preview
cancels its loaders; `proc:stop` also cancels and releases the process.

## Boundaries

`ResourceHandleDihOS` posts request work to the owner RunLoop. Accepted starts
return before callbacks. It owns the request, forwards response/data/completion
events, and pauses body delivery while WebCore decides response policy. Redirects
are bounded to eight hops and pass through WebCore's redirect client, rather
than being followed invisibly by the kernel. Weak captures, generations and
explicit detachment prevent old callbacks accessing a canceled frame.

`NavigationStrategies` creates actual SubresourceLoaders. The Frame client
commits received bytes through DocumentLoader and accepts supported document MIME
types. Native WebCore handles parsing, CSS, DOM/script execution, layout and
pointer dispatch. Stop uses FrameLoader cancellation; Reload uses its reload
operation. Closing stops the loaders before releasing the owned window.

This profile disables cookies and credential storage. Until a public suffix
dataset is packaged, site grouping uses the complete host and document.domain
relaxation is denied. The null storage provider does not supply a cookie store.
File URLs and URL credentials are rejected; no filesystem grant is added.

## Current limits and next work

HTTPS remains rejected because the transport cannot yet attest root trust.
The toolbar reports that condition; verification is not bypassed. The next
network milestone is authenticated HTTPS with a portable root bundle.

The page viewport is fixed at 640×444. There is no keyboard/address editing yet.
Bitmap image codecs, cookies, media and broader live-site validation remain.
Requests are GET-only, with at most 2048 bytes of supported request fields and
64 KiB of cumulative response data. Unsupported operations fail explicitly.

The preview currently uploads complete frames. Besides loader/input changes,
it refreshes timer-driven DOM changes at a 250 ms diagnostic cadence. Connecting
ChromeClient damage/animation scheduling and partial updates remains performance
work; no input-latency or smooth-animation target is claimed by this milestone.

## Build and checks

From ARM64 WSL, using the already staged pinned native dependencies:

```sh
python3 tools/webcore_dihos/configure_native.py --engine --openssl
python3 tools/webcore_dihos/compile_native.py --report build/webcore-engine-openssl/report.json --target WebCore --jobs 4 --timeout 1800
python3 tools/webcore_dihos/compile_page_fixture.py --report build/webcore-engine-openssl/report.json --navigation
python3 tools/webcore_dihos/build_page_fixture.py --report build/webcore-engine-openssl/report.json --strip
```

The production ELF validator checks the stripped result, including relative
relocations and zero unresolved imports. Kernel builds use an isolated snapshot,
preserving the working-tree kernel and unrelated native graphics work:

```powershell
python tools/export_user_snapshot.py --output build/native-navigation-validation/source
.\build.ps1 -ProjectRoot .\build\native-navigation-validation\source -ArmOnly
.\tools\test_user_runtime_qemu.ps1 -NativePage -UserHttp -Python C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe
```

The guest test uses the real Frame, script engine, renderer, syscalls and VM-copy
checks with modeled HTTP responses and a kernel-owned test compositor. It checks
a redirect, external stylesheet, external script changing visible layout, clicked
link navigation, Stop during a stalled request, Reload, and closing with pending
network work. Success requires the expected pixels and complete process/window
cleanup. This does not substitute for a Hyper-V live-network test.

The proof and package are produced after validation:

```powershell
python tools/webcore_dihos/record_navigation.py
python tools/webcore_dihos/package_navigation.py
```

The package includes manifests, dependency licences and the tested source overlay.
Packaging verifies baseline files and preserves the shipping browser and assets.
