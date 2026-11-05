# Larger bounded browser allocations

The guest fault maps exactly to the failed-allocation trap in
`WTF::fastMalloc(unsigned long)` in the previously packaged renderer:
PC `0x11D455A0`, owned code base `0xF317000`, ELF offset `0x2A2E5A0`.
The instruction writes to `0xBBADBEEF` after `malloc` returns null.
The original allocation size was not included in the guest snapshot.

The allocator previously rejected individual allocations near 16 MiB and
could fill sixteen pool slots with a relatively small working set. Large
decoded scripts and parsing buffers can require bigger contiguous allocations
even when the process has not exhausted its memory quota.

Owned regions now support up to 64 MiB. TLSF pools grow to that bound while
retaining room for metadata and alignment; an exact 64 MiB malloc request is
rejected. Idle secondary pools can be replaced when all pool slots are used.
A failed allocation makes at most one growth attempt, preventing repeated
recycling of an unusable pool near TLSF bin boundaries. Large malloc failures
retain the requested size, pool count and reserved bytes in the process log.

The sixteen region slots, 256 MiB process memory quota, guard pages, zeroing,
NX permissions and checked 64 KiB commit steps remain. The kernel commitment
bitmap grows with the region limit. The renderer rebuilds its allocator
platform adapter so it uses the matching SDK limit instead of an older cached
adapter. No JavaScript engine or syscall ABI structures are changed.
The CSS parser retains an explicit 16 MiB initial-token reservation budget;
its oversized-input handling no longer depends on the general heap ceiling.

Host sanitizer tests exercise aligned 24 MiB allocation, growth to 40 MiB,
preservation on failed growth, idle-pool replacement, near-ceiling termination,
randomized operations and concurrent allocation. Native ARM64 tests commit and
verify the full 64 MiB region, protect/decommit across the former 16 MiB
boundary, and exercise a real 24 MiB browser allocation and cleanup. Browser,
network-deadline, response-size and MediaSource regressions use the same build.
The QEMU harness stack is enlarged for its larger local test fixtures; the
production kernel stack is unchanged.

Prepare and build the isolated matched kernel in Windows PowerShell:

```powershell
python tools/webcore_dihos/prepare_youtube_kernel.py --large
.\build\youtube-memory-kernel\src\build.ps1 -ProjectRoot .\build\youtube-memory-kernel\src -ArmOnly -KernelOnly
```

Build and test in ARM64 Ubuntu WSL:

```sh
python3 tools/webcore_dihos/test_large_allocator.py
python3 tools/webcore_dihos/media/build_renderer.py --mse --output build/dihscover-youtube-memory-validation --resume
python3 tools/webcore_dihos/media/test_mse_page.py --output build/dihscover-youtube-memory-validation
python3 tools/webcore_dihos/media/test_mse_regressions.py --output build/dihscover-youtube-memory-validation
python3 tools/webcore_dihos/media/prepare_mse_frontend.py --output build/dihscover-youtube-memory-validation
```

Then in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-youtube-memory-validation/engine -OutputDirectory build/dihscover-youtube-memory-validation/frontend
python tools/webcore_dihos/package_youtube_bootstrap.py --memory
python tools/webcore_dihos/package_youtube_bootstrap.py --memory --check
```

With the VM off, Administrator PowerShell can install the matched package:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-youtube-memory-20261104
```

This addresses an allocation limitation, not a claim of live YouTube or Shorts
success. If another allocation fails, `proc:status` can report its size and
reserved-pool state. Previous packages and the working-tree kernel binary are
preserved.
