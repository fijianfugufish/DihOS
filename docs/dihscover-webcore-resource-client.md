# Native event-loop HTTP resource client

`tools/webcore_dihos/ResourceLoad` now connects the checked SACX HTTP service
to the actual generic WTF RunLoop and bounded DihOS dispatch queue. Accepted
loads deliver response metadata, borrowed body chunks and terminal completion
on the owning event-loop thread. This is a native resource-client building
block, not a WebCore DOM implementation or a replacement Dihscover browser.

## Client contract

Create the load with the owning RunLoop and that loop's DispatchQueue. All
operations and state/metadata inspection belong on that thread. Supply the
callbacks before calling `start`; a load is single-use after acceptance.
The default transport consists only of userspace SACX syscall stubs, not direct
kernel function pointers. URL ownership transfers by checked copy into the
kernel before the start syscall returns.

- `start` returns an immediate admission error without invoking callbacks, or
  accepts the request and delivers callbacks asynchronously. Network and clock
  capabilities must be granted by the kernel.
- Response metadata precedes data and is delivered once. Metadata references
  and data spans should be copied if retained. Body bytes are valid only until
  the data callback returns; the client reuses its 4096-byte buffer.
- Completion follows successful EOF or an explicit failure, and runs once.
  The request is closed and polling stopped before the completion callback.
  HTTP error statuses remain responses; transport/protocol failures are errors.
- `cancel` is silent and idempotent. It suppresses callbacks that have not
  begun, closes the request and stops polling. A callback already executing
  retains its captures until it returns, including when it calls `cancel`.
- Pending deliveries use weak references. Destruction cancels work rather
  than retaining a request through an internal callback cycle. A protected
  reference keeps the load alive during an executing callback.

The queue permits one pending delivery per load. Each delivery performs at most
one 4 KiB read; the shared queue still limits turns to eight work items. A
10 ms native timer handles readiness retry, queue backpressure and queue closure.
Absolute end-to-end deadlines are checked both on ticks and before delivery.
Queue closure fails a surviving load explicitly; page teardown should cancel
its loads before closing the queue when no further callbacks are wanted.
These are work bounds, not a measured UI-latency guarantee; callback execution
still requires kernel timer preemption.

Protocol validation rejects impossible state/size combinations, premature EOF,
oversized read results and cumulative data beyond the transport limit. Close
errors are surfaced instead of silently reporting success. Existing limits
remain GET only, four admitted requests per process, a 64 KiB cumulative body
cap and one transport owner. This is not parallel TCP/TLS or complete Fetch.

## TLS policy and test seam

The client rejects metadata marked `tls_unverified` by default, before data
delivery. The existing transport still lacks TLS peer authentication. The
fixture explicitly enables a diagnostic exception for its public example.com
request, including any redirect to HTTPS; this exception must not become a
production browser default. No secure-origin or session compatibility is
claimed by a successful diagnostic load.

An injectable userspace transport table supplies deterministic readiness,
failure and clock behavior for tests. It confers no kernel authority. The
production default uses the same capability-gated start/poll/read/close calls
already reported successful on Hyper-V. Custom transports must take ownership
of URL bytes before returning and obey the result/buffer contract.

## Validation

The [exact-binary record](browser-evaluation/native-resource-client-20261031.json)
passes all **42 ARM64 integration groups**, including native JSC, event loops,
the checked HTTP broker and memory/thread regressions. Execution is paced with
16 ms desktop frames. The new fixture checks:

- asynchronous start, metadata/data/completion order and request close before
  completion;
- actual protected JavaScript callbacks and promise microtask checkpoints;
- 100 cancel cycles, cancellation inside metadata/data callbacks with live
  captures, and destruction before queued delivery;
- absolute deadlines, transport errors, truncation, unverified TLS rejection,
  oversized reads, premature EOF and close failure;
- full-queue retry and queue closure without lost completion or leaked handles;
- ordinary-launch denial and explicitly granted loading through the real
  EL0/SVC/VM-copy/HTTP broker path, followed by process-page cleanup.

QEMU models the HTTP transport; its result is not a live NIC or DNS test.
The kernel transport itself has a separate
[reported live Hyper-V GET](browser-evaluation/native-user-http-hyperv-20261031.json).
The subsequent [user-reported Hyper-V resource run](browser-evaluation/native-resource-client-hyperv-20261031.json)
completed with exit `0x6a`, the expected success message, 627 slices, two timer
preemptions and 625 SACX calls. Remaining reserved/committed heap was zero,
with `0x16c000` peak committed heap bytes. Package association follows
conversation context rather than a guest ELF digest. DOM, layout, painting,
physical hardware and end-to-end input latency remain unvalidated; the
diagnostic TLS exception is unchanged.

## Guest package

The verified package is `build/dihos-webcore-resource-20261031`, build
`20261031T121843Z-1AE2BDC6BF3F`. It reuses the exact HTTP kernel, preserving
120 baseline files byte-for-byte, including the browser, fonts and previous
fixtures. The build identity changes and four resource-test artifacts are added.
The working-tree kernel binary and unrelated graphics/Wi-Fi work were retained.

With the VM off, from the repository in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-webcore-resource-20261031
```

Then in DihOS:

```text
proc:run-net 0:/OS/System/Programs/UserRuntime/resource.elf
proc:status
```

Admission reads approximately 49 MiB. Check again until the task reports
`exited`, status `0x6a`, `SACX resource loop: async HTTP headers/body/completion
and cancellation OK`, and zero remaining heap. The fixture first runs its
deterministic client tests, then requires a nonempty successful response from
example.com through the real SACX backend. Failure is status `0xa8`; use
`proc:stop` to cancel. Ordinary `proc:run` should report the denied-access
fixture success `0x6c` rather than perform network access.

## Reproduction and next implementation

After the verified native dependencies are present, build in ARM64 WSL:

```sh
python3 tools/build_jsc_native_test.py --resource-loop --libm-report build/user-libm/report.json --support-report build/user-jsc-support/report.json --stdio-report build/user-stdio/report.json
```

The builder verifies the source/library hashes and uses the actual Release
WTF ABI. It stages only the append-only HTTP header alongside the existing SDK
stage to avoid mixing duplicate pragma-once base headers. The isolated generic
RunLoop tree-removal patch remains enabled. Neither sealed baseline archives
nor the shipping JSC binary are modified.

Refresh the standalone `--event-loop` fixture when queue/builder sources have
changed, then run PowerShell:

```powershell
.\tools\test_user_runtime_qemu.ps1 -Python <python-executable> -WebkitSync -WebkitRandom -WebkitVm -Rtti -FeatureMain -Libm -JscSupport -Stdio -Jsc -JscSpin -JscFramePaced -WebcoreLoop -UserHttp -ResourceLoop -FirmwareL0 -FirmwareL2 -ReusePrograms
python tools/webcore_dihos/resource_test.py --record
python tools/webcore_dihos/resource_test.py --stage
.\tools\hyperv.ps1 -Action Package -ProjectRoot .\build\webcore-resource-validation-20261031\source -AssetSource .\build\hyperv-assets -PackagePath .\build\dihos-webcore-resource-20261031 -IncludeDihscover
python tools/webcore_dihos/resource_test.py --verify
```

Staging refuses an existing source tree. Matching source overlays, build report,
fixture and exact-binary evidence accompany `resource.elf`; extract
`JSC-SOURCES.zip`, then `HTTP-SOURCES.zip`, then `RESOURCE-SOURCES.zip` for the
matching development sources.

Next bring up the native WebCore build and Frame/Page/graphics clients, bridge
its resource callbacks to this layer, and extend response headers/buffering and
verified TLS as required. A native view also needs checked input/surface IPC.
JavaScript websites and muted video are not enabled by this standalone client;
audio output remains deferred.
