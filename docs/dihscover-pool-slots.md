# Bounded allocator pool growth

The guest reported a failed `0xa46bbf`-byte allocation with sixteen occupied TLSF pools and `0xc7c0000` bytes reserved. The later fault resolves to `JSC::Lexer<unsigned char>::setCode`, reading its source buffer. These are distinct observations: the allocation failure is confirmed; a guest retest is required to establish whether removing the slot bottleneck also resolves that subsequent lexer fault.

TLSF previously had sixteen pool records, matching sixteen kernel-owned region records. Every occupied pool can retain live objects even when much of its space is fragmented. Empty pools can be recycled, but live pools cannot safely be moved. A process could therefore fail to allocate another large buffer while still below its byte budget.

The allocator and kernel-owned heap table now allow **32** regions/pools. The browser remains limited to **512 MiB**, ordinary processes to **256 MiB**, each owned region to **64 MiB**, and sparse VM reservations to **16 slots**. Sparse and owned-region table constants are separate so the change does not expand the sparse reservation interface or its SDK registry. Commit/copy work stays bounded and all ownership/guard checks remain in place. Allocation failure diagnostics now include live bytes and a reason code: 1=request bounds, 2=occupied pool slots, 3=backing/quota failure, 4=TLSF fit after bounded growth.

The sanitizer regression keeps live data in sixteen pools, then requests the exact failing guest allocation size. The archived previous allocator must reject it without attempting new backing; the updated allocator must use a seventeenth pool, preserve every existing object and reclaim all backing. Remaining exhaustion, alignment, realloc failure, random-allocation and concurrency tests stay enabled. ARM64 kernel tests cover 32 distinct owned regions, rejection at the table limit, cleanup and unchanged 16-slot sparse limits. The test-only boot stack is 512 KiB to accommodate multiple enlarged memory-table fixtures; production stacks are unchanged. Native browser/media and IndexedDB crash regressions are repeated.

Build a matched isolated kernel in Windows PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --pool-slots
.\build\youtube-pool-slots-kernel\src\build.ps1 -ProjectRoot .\build\youtube-pool-slots-kernel\src -ArmOnly -KernelOnly
```

Validate in Ubuntu WSL from the checkout:

```sh
python3 tools/webcore_dihos/test_large_allocator.py --output build/dihscover-pool-slots-validation --slot-control
python3 tools/webcore_dihos/validate_optional_trace.py --pool-slots
```

Package/check in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-pool-slots-validation/engine -OutputDirectory build/dihscover-pool-slots-validation/frontend
python tools/webcore_dihos/package_optional_trace.py --pool-slots
python tools/webcore_dihos/package_optional_trace.py --pool-slots --check
```

With the VM off, deploy the complete matched package:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-pool-slots-20261105
```

Updating only the browser would leave the old kernel's sixteen-region ceiling in place. The working-tree kernel binary and unrelated GPU/native work are preserved. Packaging does not change the VHD. Live YouTube/Shorts playback remains unverified, and actual IndexedDB storage is still unavailable.
