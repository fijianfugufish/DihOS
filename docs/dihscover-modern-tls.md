# Dihscover HTTPS transport

The normal Dihscover application and `net:get` now share a freestanding Mbed TLS
3.6.7 client. The earlier BearSSL client stalled partway through Google's response
in a host reproduction as well as the VM. With the same request and public roots,
the replacement returned the complete Google page. This is an interoperability
fix; it does not prove a particular server or BearSSL defect.

TLS 1.2 and 1.3 use the existing single network owner, TCP transport and cooperative
checkpoints. There are no host sockets inside the guest. DNS, TCP, DHCP, HTTP
framing, the browser UI and the WebCore renderer retain their existing interfaces.
Both terminal and browser requests release the TLS session after success, error,
cancellation or timeout. The old client remains available to legacy host tests;
the shipping kernel selects the new client and has no unverified fallback.

Certificate-chain, hostname and date verification remain required before sending
an HTTP request or accepting plaintext. The existing pinned 118-root Mozilla/curl
bundle is unchanged. UTC and random input come from DihOS; missing sources fail
closed. An intercepted DuckDuckGo certificate from the school's Smoothwall CA
still fails verification. That separate trust decision is not fixed by changing
TLS engines.

Memory uses zeroed physical pages with a 16 MiB total budget and 32 KiB queues
for each ciphertext direction. The public root cache remains resident (1,223
pages in the host profile); session allocations return to that baseline. No
user file, Windows certificate store or test CA is loaded into production.

## Build and checks

Vendored C/header/license hashes and upstream archive/framework revisions are in
`third_party/mbedtls/source.lock.json`. Compatibility headers and the DihOS profile
are separate, locally maintained inputs. The default upstream Release assertion
policy is used; certificate checks are regular runtime checks.

```powershell
python tools/check_kernel_tls_sources.py
python tools/build_kernel_tls.py --cc 'C:\Program Files\LLVM\bin\clang.exe' --output build/kernel-tls-arm64
.\tools\test_hyperv.ps1
.\tools\test_user_http_headers.ps1
python tools/export_user_snapshot.py --output build/my-tls-build/source
.\build\my-tls-build\source\build.ps1 -ProjectRoot .\build\my-tls-build\source -ArmOnly
```

In Ubuntu WSL with clang-22, from the repository directory:

```sh
python3 tools/build_kernel_tls.py --host --output build/kernel-tls-host
python3 tools/test_kernel_tls.py
python3 tools/probe_kernel_tls.py www.google.com example.com www.netsurf-browser.org
```

The live probe links the exact freestanding engine with host implementations of
page allocation, entropy, clock and socket delivery. It does not simulate
Hyper-V delivery or prove browser layout. Offline tests use real RSA/ECDSA
verification, reject malformed/untrusted/expired/wrong-host certificates, check
the pre-authentication gate and exercise 100 cancellations and 16 allocation
failure positions. The guest TCP-loop test uses a deterministic TLS adapter to
check multi-chunk delivery, deadlines, cancellation and next-request recovery.

Use a fresh snapshot output directory for each build. `-ArmOnly` limits kernel
validation to the supported ARM64 milestone; x64 validation is deferred.

The packaged desktop kernel is built from an isolated source snapshot, preserving
the checkout's working kernel and unrelated graphics changes. The existing sealed
browser binaries and their resources are retained. Source and license archives,
input hashes and test evidence accompany the package.

## Install and guest validation

With the VM off, run in Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-modern-tls-20261102
```

Start the VM and open Dihscover normally. Try Google, NetSurf and example.com, then
repeat in `net:get`. Check that a cancelled or failed navigation permits the next
request. This milestone has host and ARM64 build coverage; live guest delivery,
frame latency and physical hardware still need testing. Existing GET-only,
cookie and media limitations remain.
