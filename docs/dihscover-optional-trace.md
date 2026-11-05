# Dihscover hardened optional failure diagnostics

The YouTube guest test exited with status 151 (`0x97`): libc++ rejected dereferencing an empty `std::optional`. This is a renderer abort, not evidence of another allocation failure. The precise WebCore caller is not yet known. This candidate adds diagnostics; it does not claim to fix YouTube playback.

The assertion handler retains the last 96 characters of the reason and up to seven caller addresses in a single bounded SACX log message. The first address is the direct return address. Subsequent frames are read only from aligned, forward records within the current thread's kernel-reported stack bounds. Hardening, checked syscalls, memory budgets, and exit status remain enabled. No allocation or formatter is required on the failure path.

The browser explicitly links the rebuilt assertion handler before the cached runtime archive. Keep the matching `page.symbols.elf`: subtract the `owned backing code` address from a reported caller, then subtract four to identify the ARM64 call instruction with `addr2line`. An intentional empty-optional fixture verifies this mapping in actual ARM64 EL0. Sanitizer tests cover invalid, partial, backward and cyclic frame records and long reasons. The existing native MSE and browser suites exercise the new linked runtime.

Build and test in Ubuntu WSL from the checkout:

```sh
python3 tools/webcore_dihos/media/build_renderer.py --mse --output build/dihscover-optional-trace-validation --resume
python3 tools/webcore_dihos/test_optional_abort.py --output build/dihscover-optional-trace-validation
python3 tools/webcore_dihos/media/test_mse_page.py --output build/dihscover-optional-trace-validation
python3 tools/webcore_dihos/media/test_mse_regressions.py --output build/dihscover-optional-trace-validation
python3 tools/webcore_dihos/media/prepare_mse_frontend.py --output build/dihscover-optional-trace-validation
```

Build the existing frontend in Windows PowerShell:

```powershell
.\sdk\sacx\apps\dihscover\build_webcore.ps1 -EngineDirectory build/dihscover-optional-trace-validation/engine -OutputDirectory build/dihscover-optional-trace-validation/frontend
python tools/webcore_dihos/package_optional_trace.py
python tools/webcore_dihos/package_optional_trace.py --check
```

With the VM off, update using:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihscover-optional-trace-20261104
```

If YouTube fails again, photograph `proc:status`, including the `libc++:` message, its following `callers:` lines and `owned backing code/stack`. This package preserves the preceding kernel, assets, frontend controls, and user-file update policy. The VHD is not modified by packaging.
