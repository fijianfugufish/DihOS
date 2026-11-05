# Dihscover controls on native WebCore

The existing NetSurf Dihscover already has navigation controls and keyboard
input. This step carries those behaviours into the isolated WebCore preview.
Its kernel-widget callbacks cannot be reused as raw function pointers in EL0.
Both frontends now use `sdk/sacx/apps/dihscover/address.h` to turn a hostname into
HTTPS or a search phrase into a DuckDuckGo HTML search, with checked lengths and
percent encoding. The old shipping application remains available.

The WebCore toolbar provides Back, Forward, Reload, Stop, an editable address,
font-family cycling (`Aa`), and Go. Clicking the address or Ctrl+L selects it;
Enter navigates. Backspace, Delete, Home/End, arrows, Shift selection and Ctrl+A
edit the address. F5 reloads, Escape stops, Alt+Left/Right navigates history,
and Ctrl+plus/minus/0 changes page zoom. History retains at most 64 entries.
Family and zoom choices apply for the current process; they are not persisted.

Keyboard events append types to the existing 32-byte owned-window event ABI.
Only the focused owned window receives them. Focus loss sends RESET, held keys
from another window are suppressed, and key repeat is bounded. Event copies,
owner checks, scalar validation and queue overflow recovery remain in the
kernel broker. No global key-state syscall or widget pointer is exposed.
The initial physical keyboard translation uses a US layout. Clipboard, IME,
international keyboard layouts, undo/redo and persistent preferences remain
future work. Document key events first reach WebCore DOM handlers and form
defaults. If not handled or prevented, text and basic editing commands reach
WebCore's editor. Native-only client policies permit edits within the document;
they grant no additional filesystem or kernel access. Address typing is handled
by the browser's owned control state.

Run the host controls and focused-input tests:

```powershell
.\tools\test_browser_controls.ps1
```

The packaged milestone is `build/dihos-native-controls-20261102`. With the VM
turned off, update it from Administrator PowerShell:

```powershell
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\dihos-native-controls-20261102
```

After starting the guest, launch the native preview from its terminal:

```text
proc:run-web 0:/OS/System/Programs/UserRuntime/web.elf
```

The desktop Dihscover application still opens the existing NetSurf build. This
preview uses a separate launch while its remaining compatibility and resource limits
are being addressed. Updating the package preserves user-created files.

Build the preview in ARM64 WSL using the existing pinned engine configuration:

```sh
python3 tools/webcore_dihos/compile_page_fixture.py --report build/webcore-engine-openssl/report.json --https
python3 tools/webcore_dihos/build_page_fixture.py --report build/webcore-engine-openssl/report.json --strip
```

The native QEMU integration uses a modeled authenticated transport and exercises
typed address navigation, page JavaScript key dispatch, input editing and
`preventDefault`, history, external CSS
and JavaScript, Stop, Reload, close during loading and reclamation. The previous
milestone's separate real BearSSL verification tests cover TLS authentication.
Neither modeled network tests nor compilation establish live Hyper-V success.

The fixed 640×480 viewport, 64 KiB per-response bound, missing bitmap codecs,
disabled cookies and absent media playback still limit live sites. This is
browser control integration, not a claim of YouTube compatibility.
