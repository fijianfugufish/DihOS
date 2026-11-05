"""Record native sparse VM tests, checking exact test ELF bytes in the kernel."""
from pathlib import Path
import argparse,hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--variant',choices=['l0','l1'],required=True);parser.add_argument('--sparse-adapter',action='store_true');args=parser.parse_args()
def sha(data):return hashlib.sha256(data).hexdigest()
log_path=ROOT/'build/user-runtime/qemu.log';log_bytes=log_path.read_bytes();log=log_bytes.decode('utf-8-sig')
groups=[line for line in log.splitlines() if line.startswith('PASS:')]
for prefix in ['PASS: sparse GiB EL0 reservations','PASS: sparse quota/slot bounds']:
    if not any(line.startswith(prefix) for line in groups):raise SystemExit('Sparse VM test did not finish')
if any(word in log for word in ['FAIL:','FATAL']) or 'NX hardware faults' not in log or 'process isolation' not in log:
    raise SystemExit('Sparse VM coverage is incomplete')
kernel=(ROOT/'build/user-runtime/user-runtime.elf').read_bytes()
h=struct.unpack_from('<16sHHIQQQIHHHHHH',kernel)
if h[0][:6]!=b'\x7fELF\x02\x01' or h[2]!=183:raise SystemExit('Expected native AArch64 ELF64')
sections=[struct.unpack_from('<IIQQQQIIQQ',kernel,h[6]+i*h[11]) for i in range(h[12])]
symbols={}
for section in sections:
    if section[1]!=2:continue
    strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
    for offset in range(section[4],section[4]+section[5],section[9]):
        symbol=struct.unpack_from('<IBBHQQ',kernel,offset)
        name=names[symbol[0]:].split(b'\0',1)[0].decode(errors='replace');symbols[name]=symbol
programs={}
for name in ['sparse','sparse_guard','sparse_decommit','sparse_readonly','sparse_spin','sparse_nx']:
    data=(ROOT/f'build/user-programs/{name}.elf').read_bytes();symbol=symbols.get(f'user_{name}_elf')
    if not symbol or symbol[5] not in (0,len(data)):raise SystemExit('Missing embedded sparse fixture: '+name)
    embedded=None
    for i in range(h[10]):
        p=struct.unpack_from('<IIQQQQQQ',kernel,h[5]+i*h[9])
        if p[0]==1 and symbol[4]>=p[3] and symbol[4]-p[3]+len(data)<=p[5]:
            start=p[2]+symbol[4]-p[3];embedded=kernel[start:start+len(data)]
    if embedded!=data:raise SystemExit('Embedded sparse fixture changed: '+name)
    programs[name]={'sha256':sha(data),'bytes':len(data)}
paths=['kernel/memory/aarch64_user_vm.c','kernel/memory/aarch64_user_sparse.inc',
    'kernel/include/memory/aarch64_user_vm.h','kernel/memory/pmem.c','kernel/include/memory/pmem.h',
    'kernel/process/dihos_user_memory.c','kernel/process/dihos_user_sparse.inc',
    'kernel/include/process/dihos_user_memory.h','kernel/process/dihos_user_sacx.c',
    'sdk/sacx/include/sacx_user_syscall.h','sdk/sacx/include/sacx_user_sparse_vm.h',
    'tools/user_sparse_program.c','tools/user_runtime_qemu_tests.c','tools/build_user_programs.ps1',
    'tools/test_user_runtime_qemu.ps1','tools/hyperv_memory_tests.c','tools/user_sacx_tests.c']
proof={'date':'2026-10-31','scope':'Native ARM64 sparse SACX service; legacy allocator and browser adapter retained',
    'qemu_validated':True,'hyperv_validated':False,'physical_hardware_validated':False,
    'requested_firmware_variant':args.variant,'pass_groups':groups,
    'legacy_javascriptcore_compatibility':any('PASS: actual native JavaScriptCore creates' in line for line in groups),
    'abi_version':1,'operations_bitmap':'0x7ffffff','maximum_virtual_region_bytes':4<<30,
    'private_arena_bytes':16<<30,'maximum_commit_or_release_step_bytes':65536,
    'reservation_acquires_payload_backing':False,'metadata_quota_charged':True,
    'programs':programs,'embedded_programs_verified':True,'success_exit_status':101,
    'kernel_sha256':sha(kernel),'qemu_log_sha256':sha(log_bytes),
    'source_hashes':{p:sha((ROOT/p).read_bytes()) for p in paths}}
if args.sparse_adapter:
    proof['scope']='Sparse SACX kernel regression alongside migrated browser VM adapter and native JavaScriptCore'
    proof['migrated_javascriptcore_compatibility']=proof.pop('legacy_javascriptcore_compatibility')
out=ROOT/(f'docs/browser-evaluation/sparse-vm-adapter-{args.variant}-20261031.json' if args.sparse_adapter else f'docs/browser-evaluation/sparse-vm-arm64-{args.variant}-20261031.json')
out.write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({'proof':str(out),'groups':len(groups),'embedded_programs_verified':True}))
