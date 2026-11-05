# Hyper-V entropy test package

Build `20261029T165252Z-BE3B60A21A4A` was compiled from the isolated source
snapshot under `build/user-validation-random-20261029/source` and packaged as
`build/dihos-user-random-20261029`. All 93 manifest entries passed size/hash
validation. The updated stage 2 and kernel include firmware seed handoff, native
RNDR detection, the entropy service and the permission-checked random syscall.
The working browser and assets were retained from the previously validated
package; unrelated working graphics sources and the working-tree kernel binary
were preserved.

Administrator deployment succeeded into
`C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\DihOS.vhdx`, with VM
`DihOS` verified Off. The updater checked every deployed file hash and dismounted
the disk. Guest files outside the manifest were preserved. Deployment evidence
is in `build/user-validation-random-20261029/deployment-result.json` and
`deployment.log`. This confirms deployment, not guest entropy availability.

After starting the VM from this fresh boot, run:

```text
hv:diag
proc:run 0:/OS/System/Programs/UserRuntime/random.elf
proc:status
```

If still loading, run `proc:status` again after a few seconds. Expected final
state is **exited**, status **0x5d**, and:

```text
SACX random: bounded chunks, owned buffers and protected destination rejection OK
```

Photograph the `RNG ready/sources/hardware/error/requests/reseeds` and
`RNG firmware EFI status` lines from `hv:diag`, and the final `proc:status`.
RNG readiness should be 1. Source bits are firmware 1, raw firmware 2 and RNDR 4.
If readiness is zero or the fixture fails, retain those exact diagnostic lines;
the service intentionally refuses to manufacture an untrusted seed.

Do not use `random_unready.elf` as a normal guest success test; that is a
negative fixture for the pre-seeded-state test harness.

## Reported Hyper-V result

The user-reported fresh-boot log for this exact build passes the SDK test.
Readiness is 1, sources are `0x5` (default firmware RNG plus RNDR), hardware
availability is 1, last error and firmware EFI status are zero, and there are
two initial seed operations. Raw-firmware source bit 2 is not set.

`random.elf` loads 40,568 bytes, exits with `0x5d`, logs the expected success
message and leaves zero reserved/committed heap and no live threads. It records
16 SACX calls, grants `0x1e4` and last result zero. This confirms the source
availability and owned-buffer SDK path inside Hyper-V. It is not an entropy
quality certification or a guest run of the separate WebKit adapter ELF.
Native hardware, VM-resume reseeding and the full engine remain separate gates.
