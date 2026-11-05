# Native WebCore request boundary

The Hyper-V local Page window has been reported working: clicking the blue box
executes its DOM JavaScript and paints it green. The next integration is native
Frame navigation over SACX. This change supplies request fields and response
deferral; it does not yet connect the Page window to live URLs or enable YouTube.

`sacx_user_http_request.h` appends operation 38 to ABI v1. Its 32-byte descriptor
contains URL/header addresses, lengths, a timeout and a zero reserved field.
The kernel checks and copies all data before queuing a manual-redirect GET.
Network permission bit 9 remains mandatory. Old calls and response layouts are
unchanged; QUERY advertises bits 0–38 (`0x7fffffffff`).

Request fields are limited to 2048 bytes, including CRLF separators. The service
rejects invalid names, control characters, folding, missing CRLF, duplicate names,
empty separator lines and framing/hop-by-hop overrides. Host and Connection are
transport-owned; Accept-Encoding remains identity because compressed response
decoding is not implemented here. Supported fields, including WebCore's Accept,
User-Agent, Origin and Referer, are copied verbatim. It rejects unsupported
fields instead of silently discarding them. URL credentials, uploads and methods
other than GET remain unsupported.

The wire transport preserves the old defaults when Accept/User-Agent are absent.
Plain HTTP requests are segmented to at most 1300 payload bytes per packet; the
packet builder also checks its buffer and MTU before copying. This fixes a whole
request copy into the 1536-byte packet buffer when a request is larger than one
packet. TCP retransmission/reliability work remains separate.

`ResourceLoad::setDeferred` pauses polling/body delivery while keeping deadlines
and cancellation active. Resuming posts bounded work back to the owner RunLoop.
`WebCoreResourceLoad` serializes and validates WebCore fields and uses the new
checked syscall. Custom test transports must explicitly support the extended
start operation. A response callback can pause delivery before any body callback.

HTTPS remains rejected by the native resource adapter: the kernel transport has
no authenticated root trust store. No TLS bypass was added. The existing 64 KiB
cumulative response limit also remains. Frame/ResourceHandle redirect and CORS
policy, larger streaming responses, verified HTTPS, keyboard/navigation controls
and general live-site compatibility are still integration work.

Focused host checks:

```powershell
.\tools\test_user_http_headers.ps1
```

Native resource build commands, run from the repository in ARM64 WSL:

```sh
python3 tools/build_jsc_native_test.py --resource-loop \
  --libm-report build/user-libm/report.json \
  --support-report build/user-jsc-support/report.json \
  --stdio-report build/user-stdio/report.json
python3 tools/webcore_dihos/configure_native.py
python3 tools/webcore_dihos/build_resource_bridge.py
```

Run the real EL0 resource fixtures with modeled HTTP transport:

```powershell
.\tools\webcore_dihos\build_http.ps1
.\tools\test_user_runtime_qemu.ps1 -Jsc -UserHttp -ResourceLoop -WebcoreResource -ReusePrograms -Python C:\Users\joshu\AppData\Local\Programs\Python\Python312\python.exe
```

Omit `-ReusePrograms` when base program inputs changed. The first live-URL window
package will follow Frame integration; retain the working native-window package.

The recorded regression also includes `-NativePage` with the newly rebuilt
native window ELF. In ARM64 WSL, the bounded parser corpus can be checked with
`gcc -std=c11 -g -O1 -fsanitize=address,undefined -Ikernel/include tools/http_request_headers_tests.c -o build/request-headers-sanitized`
followed by `build/request-headers-sanitized`.
