# Explicit personal CA builds

Public DihOS builds continue to trust the existing 118-root Mozilla bundle.
`kernel/include/net/dihos_tls_extra_ca.h` defaults to no additional authority.
An explicitly selected personal snapshot can include one extra CA; the public
bundle is not edited, and Windows certificate stores are not changed.

The supplied QE certificate has DER SHA-256:

```
32fee31ac315341a1455d3c7f038b2fa4fc3992c1c5642c8558747a9298bab2b
```

It identifies `QE_Root_Cert`, Queen Ethelburgas College, valid 30 September 2026
through 29 September 2028. Its self-signature and the current intercepted
`html.duckduckgo.com` chain validate against this certificate. This establishes
the cryptographic relationship, rather than independent proof of the issuer's
identity. The user explicitly selected this file and authorized trusting it.

Trusting a CA lets it authenticate certificates for HTTPS sites generally,
not just DuckDuckGo. On inspected school connections this lets the school's
system decrypt and inspect traffic. Hostname, signature, chain and date checks
remain enabled; unrelated unknown authorities are not accepted.

## What the DuckDuckGo test found

The native DihOS engine successfully authenticated the intercepted TLS session
with the added CA. The HTTP response was **403 Forbidden**, with a Smoothwall
`Site Blocked` page and `X-dg-blocked` reason:

> Content of type DuckDuckGo ( Domain/URL filtering ) blocked.

The server also terminated this close-delimited TLS response without a clean
TLS close notification. Both the native probe and curl rejected the incomplete
transfer. No truncation checks or school filtering rules were bypassed. Adding
the certificate fixes the trust error; it does not enable access to DuckDuckGo
on this network. School IT would have to change that policy.

Google, example.com and NetSurf still return complete authenticated responses.
An allowed search URL can be typed directly into Dihscover, for example
`https://www.google.com/search?q=dihos`. As requested, ordinary address-bar
queries now automatically use Google, including single words. Explicit URLs
still open directly and query punctuation/Unicode remain URL-encoded. The
shared normalizer covers the WebCore frontend and the legacy NetSurf frontend.
The packaged WebCore renderer is retained byte-for-byte: the existing frontend
normalizes text before sending the navigation command, and the embedded renderer
does not select a search engine. Its original source archive is retained as
`RENDERER-BASELINE-SOURCES.zip` alongside the updated frontend sources.

## Reproduce the personal build

Use the exact file supplied by your trusted administrator and verify its DER
fingerprint independently when practical. The generator requires an explicit
fingerprint and refuses PEM private-key input and multiple-certificate bundles.
The native engine also rejects malformed or non-CA extra certificates before
allowing requests. A bad extra CA fails initialization rather than silently
falling back to a different trust policy.

```powershell
python tools/prepare_tls_extra_ca.py C:\Users\joshu\Downloads\QE_Root_Cert.crt --sha256 32fee31ac315341a1455d3c7f038b2fa4fc3992c1c5642c8558747a9298bab2b --output build/my-personal-ca/qe-ca.h
python tools/export_user_snapshot.py --output build/my-personal-ca/source --extra-ca-header build/my-personal-ca/qe-ca.h
.\build\my-personal-ca\source\build.ps1 -ProjectRoot .\build\my-personal-ca\source -ArmOnly
```

Each output must be a new directory/header. The generated certificate-bearing
header stays in the personal build directory, not in public source defaults.
The personal kernel's source archive includes its exact header and fingerprint.

In Ubuntu WSL with clang-22, build and check the actual TLS profile:

```sh
python3 tools/build_kernel_tls.py --host --output build/kernel-tls-school-ca --extra-ca-header build/dihscover-school-ca-validation/qe-ca.h
python3 tools/test_kernel_tls.py --output build/kernel-tls-school-ca
python3 tools/test_tls_extra_ca.py
python3 tools/probe_kernel_tls.py --output build/kernel-tls-school-ca www.google.com example.com www.netsurf-browser.org
```

## Install or remove

The optional package changes the shared DihOS TLS trust used by Dihscover and
`net:get`. It retains the existing browser binaries, assets and deployment
manifest behavior. No VM or Windows trust store is changed during packaging.

With the VM off, Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-google-school-ca-20261103
```

To remove this extra trust, deploy the previous public-root-only package:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-modern-tls-20261102
```

The CA is embedded in the selected kernel, not automatically loaded from user
files. Removing a `.crt` file alone therefore does not revoke it. The ordinary
root checkout build also omits it. Host tests and ARM64 compilation do not
substitute for a live Hyper-V or physical-hardware retest.
