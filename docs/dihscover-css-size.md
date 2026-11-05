# Large stylesheet admission and SVG sizing

YouTube's main public stylesheet captured on 2026-11-05 is 3,823,111 bytes. WebCore reserves an estimated `source length / 3` CSS tokens before parsing. Each token is 24 bytes in the pinned ARM64 build, so this sheet initially needs about 29 MiB of token storage. DihOS's 16 MiB initial-reservation guard rejected it before parsing. Without these rules, SVG icons can expand to the width of their containers and normally hidden controls can appear in the page. This explains a concrete stylesheet failure independently of browser zoom; a live guest retest is still needed to verify the entire page.

The initial CSS token reservation allowance is now **48 MiB** with metadata headroom. It stays below the allocator's 64 MiB single-region ceiling; allocation failure and null-tokenizer guards remain enabled. Request/response limits, process byte budgets, the matched 32-pool kernel, and browser zoom behaviour are unchanged. No YouTube-specific sizing rules or global SVG clamps are added.

The native layout fixture installs a 4 MiB stylesheet, checks its computed colour, a 24×24 CSS-pixel SVG and a hidden menu, then verifies that a 7 MiB input exceeding the explicit reservation allowance is rejected without replacing the earlier rules. General SVG/image, layout, scrolling, browser, networking, IndexedDB and native media fixtures remain enabled. Public CSS is captured only for diagnostics; the website's stylesheet is not bundled into the shipping browser.

Build/test in Ubuntu WSL from the checkout:

```sh
python3 tools/webcore_dihos/capture_youtube_styles.py --output build/dihscover-css-size-validation
python3 tools/webcore_dihos/validate_optional_trace.py --css-size
```

Package/check in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-css-size-validation/engine -OutputDirectory build/dihscover-css-size-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --css-size
python tools/webcore_dihos/package_optional_trace.py --css-size --check
```

With the VM off:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-css-size-20261105
```

The kernel, assets, fonts, media implementation and previous memory/IndexedDB fixes are inherited from the preceding verified package. Packaging does not modify the VHD or working-tree kernel binary. Live YouTube bootstrap and Shorts playback remain unverified.
