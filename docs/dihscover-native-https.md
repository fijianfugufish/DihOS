# Verified HTTPS for the native browser

The native preview now opens `https://example.com/`. Its existing TLS 1.2
transport verifies the certificate chain, hostname and validity dates against
118 pinned Mozilla roots distributed by curl. The previous unknown-issuer
exception is removed. TLS entropy comes from DihOS's seeded random service;
missing randomness or UTC time stops the request.

With the VM off, update from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-native-https-20261101
```

Then launch inside DihOS:

```text
proc:run-web 0:/OS/System/Programs/UserRuntime/web.elf
```

The screenshot supplied before this update confirms the prior HTTP preview
rendered a fetched page on Hyper-V. HTTPS on the guest still needs the live
deployment test. The shipping NetSurf browser and packaged desktop assets are
retained.

## Trust boundary

`kernel/net/tls_roots.inc` embeds public trust anchors in the kernel. Their
source is the [curl Mozilla CA snapshot](https://curl.se/docs/caextract.html)
dated 2026-10-20, SHA-256
`796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe`.
The PEM, MPL-2.0 licence and lock file are included under
`OS/System/Certificates` for auditing. Changing that guest PEM does not modify
the running kernel's trust anchors.

BearSSL's actual validator handles signatures, chain constraints, server names
and validity times. The observer wrapper preserves every validation error.
Certificate success alone is insufficient: the transport must reach its
completed-handshake application state before publishing verification or sending
the HTTP request. Renegotiation is disabled. The stream sink checks the verified
state before parsing decrypted HTTP bytes, and the checked SACX broker refuses
unauthenticated HTTPS header/body copies. Pending requests expose no response.

The existing metadata layout and syscall numbers remain compatible. The broker
no longer labels every HTTPS response unverified merely because of its scheme;
it uses the transport's actual result. Network and certificate failures reach
the browser's terminal log. No verification bypass was added to the browser.
This applies to USB and Hyper-V transports through their shared network owner.

Certificate-chain inspection UI, revocation/CT services and additional browser
root-policy constraints are not implemented. In particular, curl documents that
its PEM conversion omits some additional Firefox policy constraints. Certificate
details remain unavailable in WebCore's `CertificateInfo`; authentication is
enforced at the transport/resource boundary. This is TLS 1.2 support.

## Validation and rebuild

The tests use real BearSSL verification for RSA/intermediate and ECDSA chains.
Wrong names, expired/not-yet-valid certificates, absent/unknown roots, altered
signatures and truncated DER must fail. Test CAs are absent from production
anchors; their private keys stay in ignored build storage.

Host-only interoperability probes authenticate example.com, NetSurf, Wikipedia
and GitHub with the same roots and preserve the validator's errors. Wikipedia's
probe received HTTP 403 after successful TLS; this is not a page-compatibility
pass. Expired, wrong-host and untrusted-root badssl endpoints are rejected before
HTTP application data is sent. These probes do not supply guest page contents.

The transport scheduling test checks successful authentication publication,
failure reset, missing entropy, delayed data and deadlines with an explicitly
modeled TLS engine. Separate real-certificate tests cover cryptographic checks.
The ARM64 guest regression exercises the real Frame/DOM/JS/renderer/SVC path
with modeled successful HTTPS outcomes, plus links, Stop, Reload and cleanup.

In ARM64 WSL, build the host verification tool once:

```sh
make -C third_party/BearSSL -j4 BUILD=/home/joshu/.cache/dihos-tls-host CC=clang-22 LD=clang-22
python3 tools/test_tls_trust.py --live
```

Root updates are explicit: review a newer curl/Mozilla snapshot, update the
dated URL/count in `tools/fetch_tls_roots.py`, fetch over verified HTTPS, then run
`tools/generate_tls_roots.py --brssl /home/joshu/.cache/dihos-tls-host/brssl`.
The generated data and lock hashes must be reviewed and the kernel rebuilt.
Ordinary guest startup never downloads or imports roots from Windows.

Build the isolated kernel as documented for the navigation preview. Build the
browser with `compile_page_fixture.py --https` followed by
`build_page_fixture.py --strip`, using `--report build/webcore-engine-openssl/report.json`.
Run `test_user_runtime_qemu.ps1 -NativePage -UserHttp`, then record and package
with `tools/webcore_dihos/record_https.py` and `package_https.py`.

Address/keyboard entry, larger responses, bitmap codecs and media are separate
remaining work. The existing 64 KiB response limit and fixed viewport still
apply; this update does not claim general modern-site or video compatibility.
