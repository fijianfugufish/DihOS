"""Record a passing JSC run only when its exact ELF is in the tested kernel."""
from pathlib import Path
import argparse,hashlib,json,re,struct

parser=argparse.ArgumentParser();parser.add_argument('--extended',action='store_true');parser.add_argument('--desktop',action='store_true');parser.add_argument('--sparse-adapter',action='store_true');args=parser.parse_args()
if args.sparse_adapter:args.desktop=True
if args.desktop:args.extended=True

ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
report_path=ROOT/'build/user-jsc-native/report.json'
report=json.loads(report_path.read_text())
key=Path(report['output']).name
if len(key)!=12 or any(c not in '0123456789abcdef' for c in key):
    raise SystemExit('Unexpected native engine artifact path')
engine=(ROOT/'build/user-jsc-native'/key/'jsc.elf').read_bytes()
if not report['linked'] or not report['loader_accepted'] or sha(engine)!=report['elf_sha256']:
    raise SystemExit('Engine link/loader evidence is missing or changed')
log_path=ROOT/'build/user-runtime/qemu.log';log_bytes=log_path.read_bytes()
log=log_bytes.decode('utf-8-sig')
success='PASS: actual native JavaScriptCore creates a context, evaluates JavaScript to 4950, releases the context and reclaims process memory'
groups=[line for line in log.splitlines() if line.startswith('PASS:')]
expected_groups=39 if args.sparse_adapter else (37 if args.desktop else (35 if args.extended else 34))
if success not in groups or len(groups)!=expected_groups or any(word in log for word in ['FAIL:','FATAL','JSC abort']):
    raise SystemExit('The complete native engine test did not pass')
kernel_path=ROOT/'build/user-runtime/user-runtime.elf';kernel=kernel_path.read_bytes()
header=struct.unpack_from('<16sHHIQQQIHHHHHH',kernel)
if header[0][:6]!=b'\x7fELF\x02\x01' or header[2]!=183:
    raise SystemExit('Expected little-endian ELF64 AArch64 kernel')
sections=[struct.unpack_from('<IIQQQQIIQQ',kernel,header[6]+i*header[11]) for i in range(header[12])]
symbol=None
for section in sections:
    if section[1]!=2:continue
    strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
    for offset in range(section[4],section[4]+section[5],section[9]):
        entry=struct.unpack_from('<IBBHQQ',kernel,offset)
        if names[entry[0]:].split(b'\0',1)[0]==b'user_jsc_elf':symbol=entry
# The assembly incbin label has no .size directive. Accept its zero-size symbol
# only after comparing every expected ELF byte within a file-backed LOAD.
if symbol is None or symbol[5] not in (0,len(engine)):
    raise SystemExit('Embedded engine symbol is missing or has the wrong size')
embedded=None
for i in range(header[10]):
    segment=struct.unpack_from('<IIQQQQQQ',kernel,header[5]+i*header[9])
    if segment[0]==1 and symbol[4]>=segment[3] and symbol[4]-segment[3]+len(engine)<=segment[5]:
        offset=segment[2]+symbol[4]-segment[3];embedded=kernel[offset:offset+len(engine)]
if embedded!=engine:raise SystemExit('Test kernel does not contain this exact engine ELF')
sources={path:sha((ROOT/path).read_bytes()) for path in [
    'sdk/sacx/apps/jsc_probe/main.cpp','tools/jsc_dihos/AbortDihOS.cpp',
    'tools/build_jsc_native_test.py','tools/test_user_runtime_qemu.ps1',
    'tools/user_runtime_qemu_tests.c','tools/user_threads_program.ld',
    'kernel/process/dihos_user_runtime.c','kernel/process/dihos_user_elf.c',
    'kernel/include/process/dihos_user_elf.h']}
if args.desktop:
    for path in ['kernel/include/process/dihos_user_runtime.h','kernel/kernel.c',
                 'kernel/asm/aa64/aa64_user_timer.c','kernel/include/asm/aa64_user_slice.h']:
        sources[path]=sha((ROOT/path).read_bytes())
proof={'date':'2026-10-30','scope':'Actual native interpreter-only JavaScriptCore in DihOS ARM64 EL0',
    'qemu_validated':True,'hyperv_validated':False,'hardware_validated':False,
    'browser_integrated':False,'websites_validated':False,'host_engine_reused':False,
    'structure_heap_kib':4194304 if args.sparse_adapter else 8192,'exit_status':87,'pass_groups':groups,
    'engine_elf_sha256':sha(engine),'engine_bytes':len(engine),'kernel_sha256':sha(kernel),
    'embedded_elf_verified':True,'qemu_log_sha256':sha(log_bytes),
    'build_report_sha256':sha(report_path.read_bytes()),'layout':report['measured_layout'],
    'startup_tables':report['startup_tables'],'loader':report['loader'],
    'libraries':report['libraries'],'source_hashes':sources}
if args.extended:
    checks=['classes, closures and modern syntax','collections, JSON and BigInt',
        'typed arrays and DataView','Unicode, RegExp and ICU Intl',
        'native callbacks and exception crossing','promise microtask checkpoint',
        'protected roots and full GC cycles','context isolation']
    if any('JSC suite: '+check+' OK' not in log for check in checks):
        raise SystemExit('Extended JavaScript suite evidence is incomplete')
    if not any(line.startswith('PASS: infinite native JavaScript timer preemption') for line in groups):
        raise SystemExit('Actual busy-engine cancellation did not pass')
    spin_path=ROOT/'build/user-jsc-native/spin-report.json';spin=json.loads(spin_path.read_text())
    spin_key=Path(spin['output']).name
    if len(spin_key)!=12 or any(c not in '0123456789abcdef' for c in spin_key):raise SystemExit('Unexpected spin artifact path')
    spin_elf=(ROOT/'build/user-jsc-native'/spin_key/'jsc.elf').read_bytes()
    if not spin['linked'] or not spin['loader_accepted'] or sha(spin_elf)!=spin['elf_sha256'] or spin['fixture_sha256']!=report['fixture_sha256']:
        raise SystemExit('Busy-engine build changed')
    # Compare the complete second executable in a file-backed kernel LOAD too.
    spin_symbol=None
    for section in sections:
        if section[1]!=2:continue
        strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
        for offset in range(section[4],section[4]+section[5],section[9]):
            entry=struct.unpack_from('<IBBHQQ',kernel,offset)
            if names[entry[0]:].split(b'\0',1)[0]==b'user_jsc_spin_elf':spin_symbol=entry
    embedded_spin=None
    if spin_symbol:
        for i in range(header[10]):
            segment=struct.unpack_from('<IIQQQQQQ',kernel,header[5]+i*header[9])
            if segment[0]==1 and spin_symbol[4]>=segment[3] and spin_symbol[4]-segment[3]+len(spin_elf)<=segment[5]:
                offset=segment[2]+spin_symbol[4]-segment[3];embedded_spin=kernel[offset:offset+len(spin_elf)]
    if embedded_spin!=spin_elf:raise SystemExit('Test kernel does not contain this exact busy-engine ELF')
    proof.update(extended_checks=checks,spin_elf_sha256=sha(spin_elf),
        spin_build_report_sha256=sha(spin_path.read_bytes()),spin_embedded_elf_verified=True,
        busy_engine_preemptions_minimum=24,api_free_preemptions_minimum=8,
        cancellation_cleanup_verified=True)
if args.desktop:
    for prefix in ['PASS: actual JavaScriptCore full suite and GC complete with bounded desktop service and 16 ms execution frames',
                   'PASS: bounded desktop service batches short syscalls']:
        if not any(line.startswith(prefix) for line in groups):raise SystemExit('Desktop service evidence is incomplete')
    proof.update(desktop_service_operation_cap=64,desktop_execution_budget_us=1000,
                 execution_frame_pacing_ms=16,admission_frame_paced=False,
                 concurrent_gc_enabled=False,collector_profile='upstream synchronous stop-the-world scheduler')
    stats=next(line for line in log.splitlines() if line.startswith('JSC execution slices/calls/waits/wakes:'))
    values=[int(value,16) for value in re.findall(r'0x[0-9a-fA-F]{16}',stats)]
    if len(values)!=4:raise SystemExit('Missing paced execution counters')
    proof['execution_counters']=dict(zip(['slices','syscalls','waits','wakes'],values))
if args.sparse_adapter:
    stats=next(line for line in log.splitlines() if line.startswith('JSC peak reserved/committed bytes:'))
    peaks=[int(value,16) for value in re.findall(r'0x[0-9a-fA-F]{16}',stats)]
    if len(peaks)!=2 or peaks[0]<4<<30 or not 0<peaks[1]<128<<20:
        raise SystemExit('Actual large sparse engine reservation was not demonstrated')
    for path in ['sdk/sacx/user_runtime/virtual_memory.cpp','sdk/sacx/include/sacx_user_vm.h',
                 'sdk/sacx/include/sacx_user_sparse_vm.h','tools/user_vm_program.cpp',
                 'tools/user_webkit_vm_program.cpp','tools/jsc_dihos/OSAllocatorDihOS.cpp']:
        sources[path]=sha((ROOT/path).read_bytes())
    proof.update(date='2026-10-31',sparse_adapter=True,structure_heap_override=False,
                 peak_reserved_bytes=peaks[0],peak_committed_bytes=peaks[1])
    adapter_programs={}
    for name,path in [('vm','build/user-programs/vm.elf'),('webkit_vm','build/user-webkit-vm/webkit-vm.elf')]:
        data=(ROOT/path).read_bytes();matched=None
        for section in sections:
            if section[1]!=2:continue
            strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
            for offset in range(section[4],section[4]+section[5],section[9]):
                entry=struct.unpack_from('<IBBHQQ',kernel,offset)
                if names[entry[0]:].split(b'\0',1)[0]==f'user_{name}_elf'.encode():matched=entry
        embedded=None
        if matched:
            for i in range(header[10]):
                p=struct.unpack_from('<IIQQQQQQ',kernel,header[5]+i*header[9])
                if p[0]==1 and matched[4]>=p[3] and matched[4]-p[3]+len(data)<=p[5]:
                    offset=p[2]+matched[4]-p[3];embedded=kernel[offset:offset+len(data)]
        if embedded!=data:raise SystemExit('Sparse adapter fixture differs from tested kernel: '+name)
        adapter_programs[name]={'path':path,'sha256':sha(data),'bytes':len(data),'embedded_verified':True}
    proof['adapter_programs']=adapter_programs
destination=ROOT/('docs/browser-evaluation/native-javascriptcore-sparse-adapter-20261031.json' if args.sparse_adapter else
    'docs/browser-evaluation/native-javascriptcore-desktop-20261030.json' if args.desktop else
    'docs/browser-evaluation/native-javascriptcore-suite-20261030.json' if args.extended
    else 'docs/browser-evaluation/native-javascriptcore-20261030.json')
destination.write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({'proof':str(destination),'pass_groups':len(groups),'engine_executed':True}))
