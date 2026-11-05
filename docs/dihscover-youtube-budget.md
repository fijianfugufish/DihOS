# Browser-specific memory budget

The guest allocator reported a failed `0xA318D0`-byte request (about 10.2 MiB),
with `0x87C0000` bytes (135.75 MiB) already reserved across fifteen pools.
That request is below the expanded single-allocation ceiling. The renderer's
image, stacks and maximum private-table allowance are charged against the
ordinary 256 MiB process budget before the remaining heap allowance is set.
Its roughly 106 MiB engine therefore leaves substantially less than 256 MiB
for heap reservations.

The trusted browser-loader route now selects a 512 MiB process ceiling.
Ordinary ELF launches remain at 256 MiB, including those granted networking
and window capabilities. Applications cannot request the larger budget with
a syscall or by changing capability bits. This is a maximum allowance, not an
upfront 512 MiB allocation. Image/stacks/table charges, heap guards, zeroing,
NX protections and the existing region/commit limits still apply.

Allocator pool growth tries smaller geometric sizes when its preferred block
cannot be acquired, stopping before a size that cannot fit the request. This
handles remaining-quota and contiguous-backing pressure without unlimited
retry. The existing single-growth-attempt rule prevents unusable TLSF bins
from causing repeated pool replacement.

Native ARM64 probes query their heap allowances through the actual SACX
resources call. They verify that only trusted browser admission receives the
larger allowance, that an ordinary process with the same network/window
grants retains the ordinary allowance, and that each process releases its
pages on exit. Host loader tests check route selection, ownership and
cancellation. Allocator sanitizer tests include backing capped below the
preferred growth size. The native browser/media, response-size and deadline
regression suites use the same renderer. Live YouTube/Shorts still requires
guest validation.

Prepare and build the matched kernel in Windows PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --budget
.\build\youtube-budget-kernel\src\build.ps1 -ProjectRoot .\build\youtube-budget-kernel\src -ArmOnly -KernelOnly
.\tools\test_dihscover_integration.ps1 -Output build/dihscover-youtube-budget-validation/host
```

Build and test in ARM64 Ubuntu WSL:

```sh
python3 tools/webcore_dihos/test_large_allocator.py --output build/dihscover-youtube-budget-validation
python3 tools/webcore_dihos/media/build_renderer.py --mse --output build/dihscover-youtube-budget-validation --resume
python3 tools/webcore_dihos/media/test_mse_page.py --output build/dihscover-youtube-budget-validation
python3 tools/webcore_dihos/media/test_mse_regressions.py --output build/dihscover-youtube-budget-validation
python3 tools/webcore_dihos/media/prepare_mse_frontend.py --output build/dihscover-youtube-budget-validation
```

Then in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-youtube-budget-validation/engine -OutputDirectory build/dihscover-youtube-budget-validation/frontend
python tools/webcore_dihos/package_youtube_bootstrap.py --budget
python tools/webcore_dihos/package_youtube_bootstrap.py --budget --check
```

With the VM off, install from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-youtube-budget-20261104
```

The package contains both kernel and renderer. Previous packages and the
working-tree kernel binary are preserved. Audio output remains unsupported.
