# Native WebCore resource types and SACX streaming

November 1 update: [checked request fields and response deferral](dihscover-native-http-requests.md)
extend this boundary. The description and proof below record the earlier,
URL-only resource milestone.

Actual WebCore request, response, error, header map, header parser and FormData
ownership code now links into a native ARM64 EL0 fixture. The complete 43-group
QEMU suite passes with the exact embedded fixture verified against its build
output. QEMU models HTTP transport; it exercises the actual SVC, capability,
page-table copy and broker paths. The subsequent
[user-reported Hyper-V run](browser-evaluation/native-webcore-resource-types-hyperv-20261031.json)
also passes: exit `0x6f`, 182 SACX calls and zero remaining reserved/committed
heap. Native document/graphics integration is the next gate.

This is a resource adapter, not a complete WebCore browser. Frame/ResourceHandle
integration, DOM/script page execution, software graphics, fonts and window/input
services remain. The installed shipping browser remains NetSurf.

## Boundary and lifecycle

`WebCoreResourceLoad` converts an owned WebCore URL-only GET into the existing
owner-thread `ResourceLoad`. Start returns before callbacks. It obtains raw
headers through checked slices, uses WebCore's strict parser and header-value
validation, and constructs a real ResourceResponse with status, headers, MIME
type, charset and URL. Body spans are borrowed only for the callback duration.
Headers precede body chunks; completion closes the request first. Cancellation
is silent and safe from response/data callbacks; weak captures avoid cycles.

The adapter rejects uploads, non-GET methods, URL credentials and custom request
headers rather than discarding them. URL fragments are removed before transport.
The existing 64 KiB cumulative body bound remains. Custom request headers,
larger streaming responses and full redirect/CORS processing are prerequisites
for connecting this to general website navigation.

The certificate type explicitly represents unavailable verification data. It
contains no host certificate object and cannot represent a verified peer. The
adapter exposes no diagnostic TLS bypass: ResourceLoad rejects unverified TLS
before response/body delivery. Trusted HTTPS is still pending.

Native request/response metadata uses WebCore's canonical representation; there
is no second host object. ResourceError's isolated-copy hook preserves its
sanitization classification. Native response filename suggestions use WebCore's
parser and sanitizer and confer no filesystem authority.

FormData's ownership code needs WebCore's image declarations even for an empty
request body. A pinned port edit supplies the typed `RefPtr<DihOSImage>` forward
declaration. It supplies no drawing implementation, dummy pixels or successful
image operations. NativeImage construction/rendering still requires the real
software backend.

## Append-only native syscall extension

Existing operations 27–30 and their wire layouts remain intact. Operation 27
retains its existing redirect behavior; the shipping legacy SACX API is unchanged.

| Operation | Native SDK entry point | Semantics |
| --- | --- | --- |
| 31 | `sacx_user_http_headers` | Non-consuming raw manual-response header slices, up to 4096 bytes per call and 16 KiB total |
| 32 | `sacx_user_http_start_manual` | Owned asynchronous GET that exposes redirects without following them |

Both require the network grant and process/generation-bound handles. Header reads
reject followed-mode requests, invalid/protected output buffers, invalid offsets,
excessive lengths, malformed internal lengths and closed handles. They return
pending before headers, EOF at the end and explicit failure/cancellation codes.
No body-read offset changes. The operation mask is now `0x1ffffffff`; ABI version
remains 1. Older clients retain their entry points, and missing calls on an older
kernel fail rather than falling back to hidden redirect following.

## Validation and live test

The [recorded evidence](browser-evaluation/native-webcore-resource-types-20261031.json)
contains 43 passing groups, exact embedded ELF checks and hashes for the isolated
production kernel and corresponding sources. Host browser tests and the native
SACX capability tests pass as well. The new fixture checks real request copies,
MIME/charset conversion, strict header failures, visible 302 responses, TLS
rejection, reentrant response/data cancellation, unsupported request rejection,
missing grants and the default syscall transport. No UI latency or live-site
compatibility claim is made.

The full QEMU harness uses 1 GiB RAM because it embeds several independent engine
ELFs alongside its unchanged test allocator. This does not raise process quotas.
The bridge ELF is 41,212,248 bytes, with a 39,776,256-byte mapped image, 10,325
relative relocations and 832 bytes of TLS; the production loader accepts it with
no unresolved imports.

Package: `build/dihos-webcore-resource-types-20261031`, build
`20261031T133557Z-28B442ADFF3D`, 133 manifest-verified files. The prior desktop,
assets and Dihscover bytes are preserved. Boot/kernel files are rebuilt from an
isolated source snapshot; the working-tree kernel is unchanged. The package adds
the exact tested `webcore_resource.elf` and `http_manual.elf`, a corresponding
source overlay and pinned WebCore license texts. The upstream WebKit archive
already accompanies `JSC-SOURCES.zip`; extract the new overlay last.

With the VM turned off, run Administrator PowerShell in the checkout:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-webcore-resource-types-20261031
```

Boot DihOS, then run:

```text
proc:run-net 0:/OS/System/Programs/UserRuntime/webcore_resource.elf
proc:status
```

Expected: `exited`, status `0x6f`, and
`SACX WebCore resource: native request/response, header slices and async body OK`.
Ordinary `proc:run` instead expects denied-access success `0x70`.
The optional syscall-only test is `proc:run-net
0:/OS/System/Programs/UserRuntime/http_manual.elf`, expected status `0x68`.

## Reproduction

After building the pinned native JSC/ICU/runtime prerequisites, run in ARM64 WSL:

```sh
python3 tools/webcore_dihos/configure_native.py
python3 tools/webcore_dihos/compile_native.py --target WebCore_CopyPrivateHeaders --target JavaScriptCore_CopyPrivateHeaders --target WTF_CopyHeaders --target PAL_CopyHeaders --target WebCore/DerivedSources/HTTPHeaderNames.cpp
python3 tools/build_jsc_native_test.py --resource-loop --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
python3 tools/webcore_dihos/build_resource_bridge.py
```

The bridge builder reuses supporting objects only when their recorded source,
configuration and object hashes still match. It links only verified native
archives, validates the result with the production loader and rejects unresolved
imports. In PowerShell, build the C fixture and run the full suite:

```powershell
.\tools\webcore_dihos\build_http.ps1
.\tools\test_user_runtime_qemu.ps1 -Python <python-executable> -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc -JscSpin -JscFramePaced -WebcoreLoop -UserHttp -ResourceLoop -WebcoreResource -FirmwareL0 -FirmwareL2
```

`resource_types_test.py record|stage|verify` records/stages/verifies this dated
milestone and refuses to overwrite an existing staged source overlay. Recording
also verifies that the separately built kernel matches the isolated snapshot.
Historical earlier milestone JSON files and source archives remain intact.
