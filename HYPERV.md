# ARM64 Hyper-V development

The guest uses the same desktop and SACX interfaces as native DihOS. Hyper-V
provides storage, HID input, synthetic video, and synthetic Ethernet. The CPU
compositor is the VM backend; the Qualcomm GPU path remains native-only.

The driver code has host regression tests. Real firmware/display/network
compatibility and the acceptance checks below still require a VM boot.

## Build and recover the existing assets

Run from the repository root. LLVM and Python must be installed. Building does
not mount or update the VM disk; USB deployment requires `-DeployUsb`.

```powershell
.\build.ps1 -ProjectRoot . -ArmOnly
.\tools\test_hyperv.ps1
.\tools\test_hyperv_package.ps1
```

Shut the VM down. In **Administrator PowerShell**, recover the original desktop
resources from the existing disk. This command mounts the disk read-only and
exports `OS/System` and `OS/Fonts` into a new bundle. It never overwrites an
existing bundle. Treat this bundle as personal until its redistribution rights
have been checked.

```powershell
.\tools\hyperv.ps1 -Action ExportAssets `
  -VhdPath 'C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\DihOS.vhdx' `
  -AssetSource .\build\hyperv-assets
```

## Package and update

The package contains recovered assets, repository system resources, and current
ARM64 boot/kernel/Image Viewer binaries. Repository files override recovered
files. Wallpaper, `arrow.cur`, and `Solarize.psf` are required. Firmware and Mesa GPU bundles
are intentionally excluded from this CPU-rendered VM image.

Use a new package directory for each build:

```powershell
.\tools\hyperv.ps1 -Action Package -PackagePath .\build\hyperv-package-001
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\hyperv-package-001
```

`Package` requires no Hyper-V privileges. `Update` requires Administrator
PowerShell and an offline disk. It verifies every manifest hash before mounting,
copies only listed system files, and verifies the destination hashes. Files
outside the manifest are retained. Updates are offline but not transactional;
retain a backup before updating a valuable development disk. No VM is stopped
or force-detached automatically.

Optional combined build/update:

```powershell
.\build.ps1 -ProjectRoot . -ArmOnly -DeployHyperV `
  -VMName DihOS -VhdPath 'C:\VMs\DihOS.vhdx' -AssetSource .\build\hyperv-assets
```

## Create a separate VM for another ARM64 Windows host

Copy the package, repository tools, and asset bundle as needed. Hyper-V must be
available on the host. The ARM64 image is not an x64 guest image.

```powershell
.\tools\hyperv.ps1 -Action CreateImage -VMName DihOS-Test `
  -VhdPath 'C:\VMs\DihOS-Test.vhdx' -PackagePath .\build\hyperv-package-001
.\tools\hyperv.ps1 -Action Configure -VMName DihOS-Test `
  -VhdPath 'C:\VMs\DihOS-Test.vhdx'
```

Creation refuses to replace an existing image. It creates a GPT disk with a
1 GiB FAT32 EFI system volume. Configuration creates a generation-2 VM with
2 GiB RAM, two virtual processors, Secure Boot off, and the Default Switch.
Existing VM configuration changes only its first network adapter and advertised
video modes; the VM must be off. Choose another switch with `-SwitchName`.

## Display and input

Host video configuration advertises up to 1920x1080. Firmware GOP selection
remains available. The synthetic video driver negotiates supported modes and
uses the largest mode no larger than 1080p that fits the firmware-reserved VRAM
aperture. It does not invent a framebuffer address or write beyond that range.
Negotiation failure retains GOP. Live resizing is not implemented.

The HID driver uses advertised report fields/ranges and rejects malformed input.
A bounded queue preserves click ordering and coordinates. Full queues stop
reading until space is available. A transport error releases buttons and
disables that channel until reboot. Explorer uses a 500 ms double-click window.
Cursor shape changes are committed once after UI hover processing; CPU presents
compose the damaged cursor region offscreen before publishing it.

## Networking

NetVSC negotiates NVSP v1/RNDIS with one queue and MTU 1500. NVSP v1
requests include the zeroed reserved tail of the 32-byte message envelope;
the INIT message is not just its 12 bytes of fields. The NDIS announcement is
6.1, matching the Linux/iPXE reference paths. Host tests enforce the wire size,
exercise rejected/truncated/silent initialization replies, and test automatic
DHCP gateway/DNS assignment, lease reacquisition, disconnect recovery and retry.
RNDIS QUERY requests set their information-buffer offset to the 20-byte request
body length even when input length is zero. QUERY completions must place their
result after the fixed 16-byte completion body, and their full result range must
fit the received message. Tests cover rejected/stale replies, short completions,
header-pointing/overflow offsets, and valid padded results. Failed startup also
logs the last RNDIS request OID and returned status/length/offset.
`hv:diag` includes the driver stage plus handshake send/wait results, response
length and host status, separately from DHCP configuration. It uses software
checksums and GPA-direct sends, with no offloads or multiqueue. Receive ranges
are validated and completed even when frames are dropped. Host-owned memory
remains pinned on failure; a fatal transport/TX-completion failure needs reboot.

DHCP starts automatically on a remote worker, obtains IPv4/router/DNS settings,
and reacquires the lease at half its lifetime. Disconnect clears configuration;
timeouts retry after 30 seconds. The worker shares the existing network-request
lock, so background DHCP does not race app fetches. Configure at least two vCPUs
for automatic networking; the UI thread never runs the background job itself.

Existing fetch APIs and `net:get` use the selected Ethernet backend, including
the existing experimental HTTPS implementation. This does not upgrade browser
rendering or change the existing TLS trust behavior.

```text
net:status
net:dhcp
net:get http://example.com/
net:get https://example.com/
hv:trace on
# reproduce the Explorer click or pointer issue
hv:diag
hv:trace off
```

`hv:diag` reports the packaged build marker, display/backend, writable storage,
input state, network configuration, and the last 256 traced input events.
Numeric diagnostic values are hexadecimal. Traces remain in RAM and overwrite
old entries. `manifest.json` records deployed system-file SHA-256 hashes.

Decoder scratch buffers are allocated separately rather than requiring a
contiguous 16 MiB reservation. Adjacent free physical ranges are coalesced so
repeated decoding can reuse memory. The wallpaper regression test uses the
recovered JPEG with an allocator that refuses any allocation larger than 8 MiB.

GPADL page lists can span many channel messages (the 2 MiB NIC receive buffer
uses one header and 18 continuations). Channel-message posting retries transient
Hyper-V memory/buffer/connection errors with bounded exponential backoff on
the NetVSC/video device paths. Input pages remain pinned until reboot, matching
the earlier boot policy; storage/HID keep their original one-shot posting path.
Permanent errors stop immediately. Host tests validate every PFN,
delay creation approval until all continuations arrive, inject queue pressure,
and cover permanent rejection and missing approvals. On failure, the boot log
and `hv:diag` show the GPADL stage, requested/sent page counts, raw host status,
and retries; a creation status of `0xffffffff` means no matching approval arrived.

VMBus rings use coherent shared-memory indices and memory barriers. Notifications
are sent for an unmasked empty-to-nonempty transition, or when freeing receive
space unblocks the host. Storage completions are matched by transaction ID.
A real completion timeout stops further storage I/O until reboot, keeping the
pending DMA buffer pinned instead of reusing it for retries.

CPU rendering completes the dirty regions before presenting them, retaining
separate rectangles for distant widgets and cursor movement. Synthetic video
reports these damaged rectangles rather than marking the whole screen dirty
on every cursor update.

## Memory and responsiveness

Extra app terminals are allocated when first opened and reused afterwards.
Previously all six terminals reserved their full history and script buffers at
boot, consuming about 395 MiB for terminals alone. Keeping only the primary
terminal resident removes about 329 MiB from the kernel's boot reservation,
without reducing history or scripting limits.

`Configure` now applies fixed startup memory to existing VMs too (default 2 GiB).
The guest has no Hyper-V balloon driver, so a larger dynamic-memory maximum
cannot substitute for startup RAM. With the VM off, use:

```powershell
.\tools\hyperv.ps1 -Action Configure -MemoryStartupBytes 2GB
```

Disabling GPU window layers is idempotent. The CPU frame is invalidated only
when switching away from active GPU layers. Repeated software-compositor
preparation therefore preserves dirty-region rendering instead of forcing a
full 1080p repaint every loop. A production-renderer host test verifies idle
frames copy no pixels, cursor movement restores old pixels and copies less than
64 KiB, cached wallpaper survives, and an actual GPU-to-CPU transition redraws
once, at both 1080p and 640x480.

The CPU FPS overlay counts frames with changed pixels, not monitor refreshes.
A stationary desktop can show low FPS because unchanged frames are skipped;
compare responsiveness and FPS while moving the pointer or a window.

`hv:diag` reports free ordinary RAM, its largest contiguous run, reserved
executable RAM, and the last CPU render time in microseconds. JPEG allocation
failures include the requested size and available RAM. NetVSC failures report
the failed initialization stage. Native USB Ethernet probing is skipped in the
VM; native DihOS retains its existing hardware path.

## Diagnosing a stalled boot

On Hyper-V, startup checkpoints are drawn and sent to VMConnect immediately,
including before the main loop. These updates bypass the normal 16 ms video
notification limit. Explorer window/control construction, directory enumeration,
network initialization, wallpaper read/decode, cursor loading, and final log
persistence have separate checkpoints. Note the last visible line if boot stops.

`terminal_flush_log` persists text to the filesystem; it does not draw a frame.
Screen checkpoints do no disk I/O. During desktop construction, Hyper-V batches
log persistence until the first desktop is ready, with visible markers before
and after that write. After startup these checkpoints are disabled, so normal
image loading does not force extra frames. A checkpoint identifies where boot
stopped; it is not confirmation that the underlying freeze has been repaired.

## VM acceptance checklist

- Boot a newly created image without the USB; confirm wallpaper, cursor, fonts,
  taskbar icons, editor, Explorer, and packaged SACX apps.
- Test file/folder selection and opening, scrolling, moved/overlapping windows,
  dialogs, rapid clicks, dragging, and pointer motion across the entire display.
- Repeat at 1080p and a lower GOP fallback mode. Check for trails, flicker,
  duplicate host pointers, and incorrect hit targets.
- Create/edit files, reboot, update the system image, and confirm persistence.
- Confirm automatic DHCP/DNS, HTTP, and existing HTTPS requests in terminal and
  the current browser. Disconnect/reconnect the virtual switch, test missing
  DHCP, and confirm the desktop remains responsive.
- Save `hv:diag` and boot logs, including GOP modes and synthetic driver status.
- Repeat native USB smoke tests when hardware is available. x64, audio, a new
  browser, and dynamic console resizing are separate milestones.

Protocol references: [iPXE NetVSC](https://dox.ipxe.org/netvsc_8h.html), [Linux HID](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-hyperv.c),
[NetVSC definitions](https://github.com/torvalds/linux/blob/master/drivers/net/hyperv/hyperv_net.h),
[synthetic video](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/hyperv/hyperv_drm_proto.c),
[Microsoft VM video settings](https://learn.microsoft.com/en-us/powershell/module/hyper-v/set-vmvideo).

## DHCP receives no offers

Hyper-V receive polling returns immediately when empty. DHCP, ARP, DNS and TCP/HTTP
waits use elapsed-time deadlines in the VM rather than exhausting the legacy USB
poll-count limit. A regression test delays a DHCP offer by 500 empty polls and
also verifies a silent host reaches the actual timeout. DHCP requests are checked
independently for IPv4/UDP checksums, broadcast destination and client MAC.

After a manual DHCP offer timeout, transport diagnostics print automatically;
`hv:diag` also includes them. Compare `netvsc: MAC bytes` with the administrator
PowerShell output of `Get-VMNetworkAdapter -VMName DihOS | Format-List
SwitchName,MacAddress,Status`. `Ethernet TX attempted/OK/failed/last status` counts
data packets, separately from initialization commands. `RX transfers` includes
RNDIS control replies; `Ethernet` counts actual received frames. `invalid` and
`queue full` distinguish rejected data from a host that sends no Ethernet frames.
A successful TX completion proves host acceptance, not DHCP server delivery.

For an adapter on Default Switch with a matching MAC and successful TX but no
received Ethernet, investigate host DHCP/switch traffic before changing guest IP
settings. Live DHCP/DNS/HTTP/HTTPS still require VM validation.

## HTTPS polling and terminal pauses

The Hyper-V TLS transport waits for its 30-second inactivity deadline rather than
aborting after 8192 fast empty polls. Its regression test exercises the production
HTTPS/TCP loop with a mock TLS engine, delayed data after 9000 polls, and a silent
peer timeout. It does not verify cryptography, server certificates or live HTTPS.
The existing root-store and entropy limitations remain; an engine-error-zero timeout is not a certificate rejection.

Retained text rows reuse their raster allocations across scrollback changes;
larger rasters grow storage and object destruction still releases it. Host tests
exercise 2000 content/position mutations without allocation churn, cache growth,
and position-only raster reuse.

After a long pause, run `hv:diag`: UI timing rows show last duration, maximum
duration and counts of pauses >=100ms (hexadecimal milliseconds), separately for
network/input, windows/apps, terminal, rendering and presentation/runtime. Timing
capture does no disk I/O or per-frame printing. These measurements help identify
unreproduced VM stalls; buffer reuse alone is not proof the freeze is fixed.

## TLS error 54 and firmware UTC

BearSSL error 54 means expired or not yet valid. Stage2 now captures UEFI GetTime
before ExitBootServices into a version-2 boot-info extension. The kernel advances
this RTC anchor using monotonic elapsed milliseconds; certificate checks no longer
use the old fixed 2026-08-05 date or frame-count uptime. Firmware timezone offsets
follow the UEFI sign convention. An unspecified RTC timezone is treated as UTC,
as is conventional for a VM RTC; boot_time_flags records that assumption.

Update both STAGE2.EFI and KERNEL.ELF together (the packaged update does this).
Old stage2, failed GetTime, or an invalid date produces an explicit unavailable
clock error rather than disabling validity checks. `hv:diag` and HTTPS startup
print UTC Unix seconds for comparison with the host. Tests cover firmware call
failure/invalid output, EFI_TIME layout, epoch/leap-year/timezone conversions,
clock advancement without UI frames, and the time supplied to BearSSL.

Reference: [UEFI GetTime and timezone rules](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/08_Services_Runtime_Services.rst).

## Package Dihscover for ARM64

Build the active NetSurf-based browser, then include it explicitly in a new
system package (use a fresh PackagePath each time):

```powershell
.\sdk\sacx\apps\dihscover\build.ps1 -ProjectRoot .
.\tools\hyperv.ps1 -Action Package -PackagePath .\build\hyperv-dihscover -IncludeDihscover
# Administrator PowerShell, VM fully shut down:
.\tools\hyperv.ps1 -Action Update -PackagePath .\build\hyperv-dihscover
```

Open `0:/OS/System/Programs/Dihscover/Dihscover.sacx` in Explorer. The package
includes the freshly built ARM64 SACX image and the bundled engine/library
licences in the system manifest. The browser's core stylesheets are built in.
