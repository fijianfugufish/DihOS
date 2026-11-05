# Checked asynchronous HTTP for EL0

The native resource-loading boundary now queues HTTP GET requests through
SACX syscalls. The kernel copies request data, owns response buffers and binds
generation-tagged handles to the calling process. DNS/TCP/TLS work runs later
on the existing core-0 network continuation, shared with legacy SACX apps.
Starting, polling or reading a request never invokes that network worker inline.

This is the transport boundary for a future native WebCore resource client.
It is not a complete Fetch implementation and is not integrated into Dihscover.
The shipping NetSurf binary remains unchanged.

## ABI and permission

The append-only ABI v1 header is `sdk/sacx/include/sacx_user_network.h`.
QUERY now advertises operation mask `0x7fffffff`; earlier operation numbers
and structure layouts are unchanged.

| Operation | Arguments | Result |
|---|---|---|
| 27 START | URL address, byte length excluding NUL, timeout in ms | Positive request handle or negative error |
| 28 POLL | Handle, metadata output address, exact metadata size | Metadata snapshot, without waiting |
| 29 READ | Handle, writable output address, capacity | Copied bytes, zero at successful EOF, or negative error |
| 30 CLOSE | Handle, zero, zero | Revoke handle and cancel/release its request |

All four require kernel-owned network capability bit 9. Normal `proc:run`
does not grant it. The privileged shell's explicit `proc:run-net` launch does.
EL0 cannot add grants by changing its local QUERY result, supplying another
process ID or selecting a handle from another process. Full application
manifest policies, origin restrictions and permission UI remain future work.

The initial service accepts ASCII HTTP/HTTPS URLs up to 767 bytes and GET
requests only, with 1–120 second transport timeouts and at most five followed
redirects. It rejects embedded NUL/control bytes and unsupported schemes.
Invalid URL pointers fail before admission. Submitted URL bytes are copied;
subsequent caller mutation cannot alter a queued request.

## Ownership and limits

There are 16 global requests and at most four per process. Each has a fixed
64 KiB cumulative response-body capacity. This is a separate bounded broker
budget, up to 1 MiB of payload globally or 256 KiB per process plus fixed
metadata; it is not included in the process heap counter. Metadata exposes
state, HTTP status, available/received body bytes, truncation, MIME type,
transport error text and the TLS authentication limitation.

READ validates the entire requested writable range before copying or advancing
the cursor, and copies at most 4096 bytes. Metadata copies are 248 bytes. Partial
body prefixes can be read while loading. `-11` means no bytes are available yet;
successful EOF is zero only after an untruncated completed response. Failed,
cancelled or truncated requests terminate with explicit errors rather than
silently treating an incomplete body as success. Reading does not recycle the
64 KiB capacity: larger streamed/ring-buffer responses are a later extension.

CLOSE immediately invalidates the process's handle. Queued/completed requests
can release backing immediately. An active continuation retains its buffer
until the worker observes cancellation and returns; it cannot retain a pointer
to a freed application stack or reused request slot. Process exit, fault and
stop close its remaining requests. Pending native requests participate in the
same round-robin selection as legacy requests; there is one transport owner.
This retains cooperative network checkpoints and does not establish a measured
end-to-end UI latency bound or parallel TCP/TLS support.

## HTTPS limitation

The existing HTTPS transport does **not authenticate the TLS peer** in this
bring-up profile. The metadata explicitly reports `tls_unverified` for HTTPS,
including followed HTTPS redirects. Successful HTTPS bytes are not certificate
validation evidence. Verified roots/hostname/time checks and browser origin
semantics remain prerequisites for a secure native browser and sessions.

## Validation

The [execution record](browser-evaluation/native-user-http-20261031.json)
contains 41 passing ARM64 integration groups, including existing native JSC,
event-loop and sparse-memory regressions. The HTTP fixture crosses real EL0,
SVC, capability, VM-copy and broker code. QEMU uses a deterministic modeled
transport; it is not a live DNS, NIC or TLS test. It checks denied/default and
explicitly granted launches, asynchronous completion, protected output,
stale handles and process-stop cleanup. Host tests exercise owned URLs,
cross-process/generation handles, partial reads, truncation, quota, allocation
failure and deferred active-continuation cancellation. Hyper-V host regressions,
HTTP framing/parser tests, generated resource/dependency checks and compiler
cache checks pass. Both fixture ELFs pass the production loader validator.

During testing, the inherited-user-access rejection fixture exposed a layout-
dependent instruction-permission fault: it changed the leaf containing the
running kernel/vector code. Captured ESR `0x8600000e` confirmed the fault. That
test now changes an unused inherited leaf while preserving the same rejection
assertion. No production MMU permission check was relaxed. The QEMU wrapper
also requires the final suite marker, so an early monitor quit is not a pass.

## Hyper-V test

The verified package is `build/dihos-user-http-20261031`, build
`20261031T115330Z-1AE2BDC6BF3F`, with 121 manifest files. It contains a newly
built isolated kernel and two small C fixtures, preserving the prior browser,
fonts, assets and engine/event-loop test binaries. The checkout's working
kernel binary and unrelated GPU/ripple/Wi-Fi work were preserved.
The subsequent [user-reported Hyper-V GET](browser-evaluation/native-user-http-hyperv-20261031.json)
completed with exit `0x68`, the expected success message, 41 SACX calls and
grant mask `0x3e4` (including network bit 9). This supplies the live GET gate
in addition to the modeled tests. Package identity follows conversation context;
the transcript contains no binary digest or separate HTTP/DNS trace. Default-
launch denial, HTTPS, certificate authentication and measured UI latency were
not reported in that guest run. Resource cleanup remains covered separately
by the deterministic tests.

With the VM off, from the repository in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-user-http-20261031
```

After booting, verify the denial boundary if desired:

```text
proc:run 0:/OS/System/Programs/UserRuntime/http.elf
proc:status
```

Expected: `exited`, status `0x69`, and `network denied without explicit launch
grant OK`. Then test an actual request:

```text
proc:run-net 0:/OS/System/Programs/UserRuntime/http.elf
proc:status
```

Expected: `exited`, status `0x68`, and `SACX HTTP: asynchronous GET, checked
copies and close OK`, with zero remaining heap. The fixture requires a nonempty
2xx response from `http://example.com/`. A network failure reports status `0xa7`;
the desktop should remain usable while it waits. `proc:stop` cancels it.
Optional `https.elf` uses `https://example.com/` with the authentication
limitation above. No browser, cookie or credential testing is implied.

## Reproduction and next work

The isolated kernel source overlay and build log are retained under
`build/user-validation-http-20261031`. The package retains `JSC-SOURCES.zip`
and adds `HTTP-SOURCES.zip` with the kernel/SDK overlay, fixture, recipes and
exact test record. Unpack the latter over the former for matching sources.

```powershell
.\tools\webcore_dihos\build_http.ps1
.\tools\test_user_runtime_qemu.ps1 -Python <python-executable> -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc -JscSpin -JscFramePaced -WebcoreLoop -UserHttp -FirmwareL0 -FirmwareL2 -ReusePrograms
python tools/webcore_dihos/record_http.py
python tools/webcore_dihos/package_http.py
.\tools\hyperv.ps1 -Action Package -ProjectRoot .\build\user-validation-http-20261031\source -AssetSource .\build\hyperv-assets -PackagePath .\build\dihos-user-http-20261031 -IncludeDihscover
python tools/webcore_dihos/package_http.py --verify
```

Build the isolated kernel and host loader inspector before recording. The
recorder deliberately rejects changed snapshot sources or nonmatching embedded
ELFs. With the live guest GET now reported successful, adapt owner-thread completion delivery to
the native event loop, extend response/header buffering, and implement the
native WebCore platform/Page/graphics configuration. DOM execution, JavaScript
websites and muted video are still later gates; audio output remains deferred.
