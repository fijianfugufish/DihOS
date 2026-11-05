"""Export the published browser baseline plus only the EL0 runtime changes."""
from pathlib import Path
import argparse, hashlib, io, json, subprocess, zipfile, sys

root = Path(__file__).resolve().parents[1]
base = subprocess.check_output(['git', 'rev-parse', '43f5d2ad'], cwd=root, text=True).strip()
parser = argparse.ArgumentParser()
parser.add_argument('--output', default='build/user-validation/source')
parser.add_argument('--extra-ca-header',type=Path,help='Explicit personal CA configuration, generated with a pinned DER SHA-256')
args = parser.parse_args()
dependency=json.loads(subprocess.check_output([sys.executable,str(root/'tools/prepare_user_libcxx.py')],cwd=root,text=True))
snapshot = (root / args.output).resolve()
if snapshot.exists():
    raise SystemExit('Snapshot already exists; retain it and choose another --output directory.')
snapshot.mkdir(parents=True)
archive = subprocess.check_output(['git', 'archive', '--format=zip', base], cwd=root)
with zipfile.ZipFile(io.BytesIO(archive)) as source:
    source.extractall(snapshot)
paths = '''kernel/asm/aa64/aa64_exceptions.c
kernel/asm/aa64/aa64_user_slice.c
kernel/asm/aa64/aa64_user_timer.c
kernel/asm/aa64/aa64_user_vectors.inc
kernel/include/asm/aa64_user_slice.h
kernel/include/process/dihos_user_runtime.h
kernel/include/process/dihos_process.h
kernel/include/process/dihos_user_sacx.h
kernel/process/dihos_user_sacx.c
kernel/include/process/dihos_user_memory.h
kernel/process/dihos_user_memory.c
sdk/sacx/include/sacx_user_syscall.h
sdk/sacx/include/sacx_user_api.h
kernel/include/process/dihos_user_elf.h
kernel/process/dihos_user_elf.c
kernel/include/process/dihos_user_loader.h
kernel/process/dihos_user_loader.c
kernel/process/dihos_user_runtime.c
kernel/process/dihos_user_threads.inc
sdk/sacx/include/sacx_user_threads.h
kernel/include/system/cpu_info.h
kernel/system/cpu_info.c
kernel/include/memory/aarch64_user_vm.h
kernel/memory/aarch64_user_vm.c
kernel/memory/pmem.c
kernel/kernel.c
kernel/shell/dihos_user_commands.inc'''.splitlines()
paths += ['tools/build_user_programs.ps1','tools/user_program.c','tools/user_program.ld','tools/user_cpp_program.cpp','tools/user_cpp_program.ld','sdk/sacx/user_runtime/runtime.cpp']
paths += ['tools/user_allocator_program.cpp','sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp','sdk/sacx/user_runtime/include/dihos_user_allocator.h','sdk/sacx/user_runtime/include/dihos_user_tlsf.h','third_party/tlsf/tlsf.c','third_party/tlsf/tlsf.h']
paths += ['tools/user_library_program.cpp','tools/build_user_library.ps1','tools/prepare_user_libcxx.py','tools/user-libraries.lock.json']
paths += ['tools/user_threads_program.cpp','tools/user_threads_program.ld']
paths += ['tools/user_lifetime_program.cpp','sdk/sacx/user_runtime/thread_lifetime.cpp','sdk/sacx/user_runtime/include/dihos_user_sync.h']
paths += ['tools/user_sync_program.cpp','tools/build_user_sync.ps1','sdk/sacx/include/sacx_user_sync.hpp']
paths += ['tools/user_stdthreads_program.cpp','tools/user_keys_program.cpp','sdk/sacx/user_runtime/thread_keys.cpp','sdk/sacx/include/sacx_user_keys.h']
paths += ['tools/user_large_program.c','kernel/process/dihos_user_image.inc']
paths += ['tools/user_vm_program.cpp','sdk/sacx/user_runtime/virtual_memory.cpp','sdk/sacx/include/sacx_user_vm.h']
paths += ['sdk/sacx/include/sacx_user_resources.h']
paths += ['sdk/sacx/include/sacx_user_network.h','kernel/include/process/dihos_user_network.h',
          'kernel/apps/sacx_user_network.inc','kernel/apps/sacx_network_service.inc','kernel/apps/sacx_runtime.cpp']
paths += ['kernel/include/net/tcp_receive.h','sdk/sacx/include/sacx_user_http_request.h','kernel/include/net/http_request_headers.h',
          'kernel/include/apps/sacx_network_internal.h','kernel/include/net/knet_usb.h','kernel/net/knet_usb.c']
paths += ['sdk/sacx/include/sacx_http_limits.h','kernel/include/net/http_limits.h','kernel/include/net/http_stream.h','kernel/net/http_stream.c']
paths += ['kernel/include/net/tls_verify.h','kernel/net/tls_roots.inc',
          'third_party/ca-certificates/roots.lock.json','third_party/ca-certificates/cacert-2026-10-20.pem',
          'third_party/ca-certificates/LICENSE-MPL-2.0.txt']
paths += ['kernel/include/apps/sacx_api.h','kernel/apps/sacx_browser_session.inc',
          'kernel/include/process/dihos_browser_bridge.h','kernel/process/dihos_browser_bridge.c',
          'sdk/sacx/include/sacx_browser_session.h','sdk/sacx/include/sacx_browser_frontend.h']
paths += ['sdk/sacx/include/sacx_user_window.h','sdk/sacx/include/sacx_window_keys.h','kernel/include/process/dihos_user_window.h',
          'kernel/process/dihos_user_window.c','kernel/apps/dihos_user_window_desktop.cpp',
          'kernel/include/apps/sacx_runtime.h']
paths += ['kernel/memory/aarch64_user_sparse.inc','kernel/process/dihos_user_sparse.inc',
          'kernel/include/memory/pmem.h','sdk/sacx/include/sacx_user_sparse_vm.h','tools/user_sparse_program.c']
paths += ['tools/user_context_program.cpp','tools/user_platform_program.cpp','tools/user_calendar_program.cpp','tools/generate_user_calendar_vectors.py','tools/user_calendar_vectors.inc','sdk/sacx/include/sacx_user_thread_context.h','sdk/sacx/user_runtime/fenv.cpp','sdk/sacx/user_runtime/time.cpp','sdk/sacx/freestanding/include/time.h','sdk/sacx/freestanding/include/fenv.h','sdk/sacx/freestanding/include/stdlib.h','sdk/sacx/freestanding/include/stdio.h','sdk/sacx/freestanding/include/string.h','sdk/sacx/freestanding/include/sys/types.h','kernel/include/system/dihos_time.h','kernel/system/dihos_wallclock.inc']
paths += ['include/bootinfo.h','src/stage2/stage2.c','src/stage2/stage2_load_kernel.c','src/stage2/stage2_rng.inc','kernel/system/entropy.c','kernel/include/system/entropy.h','kernel/hyperv/hyperv_diag.c','tools/user_random_program.cpp','sdk/sacx/include/sacx_user_random.h']
paths += [p.relative_to(root).as_posix() for p in (root/'sdk/sacx/user_runtime/libcxx').rglob('*') if p.is_file()]
paths += [p.relative_to(root).as_posix() for p in Path(dependency['bundle']).rglob('*') if p.is_file()]
paths += ['build.ps1','kernel/include/net/dihos_tls_build.h','kernel/include/net/dihos_tls_config.h','kernel/include/net/dihos_tls_extra_ca.h','kernel/include/net/knet_tls.h',
          'kernel/net/knet_tls.c','kernel/net/knet_tls_platform.c','kernel/net/knet_modern_https.inc','kernel/net/tls_roots_pem.inc']
paths += [p.relative_to(root).as_posix() for p in (root/'third_party/mbedtls').rglob('*') if p.is_file()]
manifest = {}
for relative in paths:
    data = (root / relative).read_bytes()
    target = snapshot / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    manifest[relative] = hashlib.sha256(data).hexdigest()
if args.extra_ca_header:
    relative='kernel/include/net/dihos_tls_extra_ca.h'
    data=args.extra_ca_header.read_bytes()
    (snapshot/relative).write_bytes(data)
    manifest[relative]=hashlib.sha256(data).hexdigest()
# Reconstruct only our shell additions; preserve the unrelated local ripple work.
shell = snapshot / 'kernel/shell/dihos_shell.c'
text = shell.read_text()
text = text.replace('#include "process/dihos_process.h"', '#include "process/dihos_process.h"\n#include "process/dihos_user_runtime.h"\n#include "system/cpu_info.h"', 1)
marker = 'static const dihos_shell_command G_commands[] = {'
commands = '''#include "dihos_user_commands.inc"

static const dihos_shell_command G_commands[] = {
    {"proc:test", "proc:test", "Start bounded, preemptible EL0 process fixtures.", 0u, dihos_cmd_proc_test},
    {"proc:run", "proc:run <path>", "Queue a static ARM64 user ELF image.", 0u, dihos_cmd_proc_run},
    {"proc:run-net", "proc:run-net <path>", "Queue an ARM64 ELF with explicit network access.", 0u, dihos_cmd_proc_run_net},
    {"proc:run-ui", "proc:run-ui <path>", "Queue an ARM64 ELF with an owned-window grant.", 0u, dihos_cmd_proc_run_ui},
    {"proc:run-web", "proc:run-web <path>", "Queue an ARM64 ELF with window and network grants.", 0u, dihos_cmd_proc_run_web},
    {"proc:status", "proc:status", "Show EL0 task preemption and fault state.", 0u, dihos_cmd_proc_status},
    {"proc:stop", "proc:stop", "Cancel and clean up EL0 process fixtures.", 0u, dihos_cmd_proc_stop},'''
assert text.count(marker) == 1
text = text.replace(marker, commands, 1)
shell.write_text(text)
manifest['kernel/shell/dihos_shell.c'] = hashlib.sha256(shell.read_bytes()).hexdigest()
for relative in ('OS/aa64/KERNEL.ELF', 'OS/x64/KERNEL.ELF'):
    (snapshot / relative).unlink(missing_ok=True)
record = {'path': str(snapshot), 'base': base, 'files': manifest}
record['overlay_sha256'] = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
(snapshot.parent / 'source.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record))
