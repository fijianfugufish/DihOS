"""Record, stage and verify the native event-loop resource-client milestone."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();modes=parser.add_mutually_exclusive_group(required=True)
for mode in ['record','stage','verify']:modes.add_argument('--'+mode,action='store_true')
args=parser.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
baseline=ROOT/'build/dihos-user-http-20261031'
validation=ROOT/'build/webcore-resource-validation-20261031';project=validation/'source'
package=ROOT/'build/dihos-webcore-resource-20261031'
proof_path=ROOT/'docs/browser-evaluation/native-resource-client-20261031.json'
def manifest(directory):
    result=json.loads((directory/'manifest.json').read_text(encoding='utf-8-sig'))
    for entry in result['files']:
        path=(directory/entry['path']).resolve()
        if not path.is_relative_to(directory) or path.stat().st_size!=entry['size'] or sha(path).lower()!=entry['sha256'].lower():raise SystemExit('Manifest mismatch: '+entry['path'])
    return result
base=manifest(baseline)
report_path=ROOT/'build/user-jsc-native/resource-report.json';report=json.loads(report_path.read_text())
key=Path(report['output']).name
if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Invalid artifact directory')
elf=ROOT/f'build/user-jsc-native/{key}/jsc.elf'
if not report['linked'] or not report['loader_accepted'] or sha(elf)!=report['elf_sha256']:raise SystemExit('Changed/unvalidated resource ELF')
if args.record:
    log_path=ROOT/'build/user-runtime/qemu.log';log=log_path.read_text(encoding='utf-8-sig')
    groups=[line for line in log.splitlines() if line.startswith('PASS:')]
    if len(groups)!=42 or any(x in log for x in ['FAIL:','FATAL','JSC abort']):raise SystemExit('Full 42-group run required')
    checks=['headers, chunks, completion and JavaScript promise ordering',
            'cancellation, reentrant captures and destruction cleanup',
            'deadlines, transport/protocol errors, TLS rejection and queue backpressure']
    for check in checks:
        if 'Resource client: '+check+' OK' not in log:raise SystemExit('Missing resource check: '+check)
    if not any(x.startswith('PASS: native RunLoop HTTP resource client') for x in groups):raise SystemExit('No native resource-client pass')
    kernel_path=ROOT/'build/user-runtime/user-runtime.elf';kernel=kernel_path.read_bytes()
    h=struct.unpack_from('<16sHHIQQQIHHHHHH',kernel)
    if h[0][:6]!=b'\x7fELF\x02\x01' or h[2]!=183:raise SystemExit('Expected native ARM64 ELF64')
    sections=[struct.unpack_from('<IIQQQQIIQQ',kernel,h[6]+i*h[11]) for i in range(h[12])];symbol=None
    for section in sections:
        if section[1]!=2:continue
        strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
        for offset in range(section[4],section[4]+section[5],section[9]):
            item=struct.unpack_from('<IBBHQQ',kernel,offset)
            if names[item[0]:].split(b'\0',1)[0]==b'user_resource_loop_elf':symbol=item
    data=elf.read_bytes();actual=None
    if symbol:
        for i in range(h[10]):
            p=struct.unpack_from('<IIQQQQQQ',kernel,h[5]+i*h[9])
            if p[0]==1 and symbol[4]>=p[3] and symbol[4]-p[3]+len(data)<=p[5]:
                at=p[2]+symbol[4]-p[3];actual=kernel[at:at+len(data)]
    if actual!=data:raise SystemExit('Resource ELF differs from tested kernel bytes')
    sources=dict(report['event_loop_sources'])
    sources['tools/webcore_dihos/resource_fixture.cpp']=report['fixture_sha256']
    sources['tools/build_jsc_native_test.py']=report['builder_sha256']
    for path,digest in sources.items():
        if sha(ROOT/path)!=digest:raise SystemExit('Changed resource source: '+path)
    for path in ['tools/user_runtime_qemu_tests.c','tools/test_user_runtime_qemu.ps1','tools/embed_user_programs.py']:
        sources[path]=sha(ROOT/path)
    http_proof=json.loads((ROOT/'docs/browser-evaluation/native-user-http-20261031.json').read_text())
    for path,digest in http_proof['source_hashes'].items():
        if path.startswith('kernel/'):
            if sha(ROOT/path)!=digest:raise SystemExit('Kernel HTTP implementation changed: '+path)
            sources[path]=digest
    kernel_sha=sha(baseline/'OS/aa64/KERNEL.ELF')
    if kernel_sha!=http_proof['production_kernel_sha256']:raise SystemExit('Baseline kernel changed')
    proof={'date':'2026-10-31','scope':'Native RunLoop HTTP resource client with owner-thread callbacks',
        'qemu_validated':True,'hyperv_guest_validated':False,'physical_hardware_validated':False,
        'webcore_dom_compiled':False,'browser_integrated':False,'websites_validated':False,
        'qemu_transport_modeled':True,'live_guest_resource_callbacks_validated':False,
        'default_rejects_unverified_tls':True,'diagnostic_fixture_allows_unverified_tls':True,
        'tls_peer_authentication_supported':False,'success_exit_status':106,'denied_exit_status':108,
        'read_chunk_bytes':4096,'queue_capacity':32,'queue_turn_budget':8,'execution_frame_ms':16,
        'cancellation':'Owner-thread, silent, suppresses callbacks not yet begun; running callback capture survives cancellation.',
        'body_lifetime':'Borrowed until the data callback returns; retaining clients must copy.',
        'transport_limit':'Existing GET-only, four requests/process, 64 KiB cumulative body limit and one network transport owner.',
        'checks':checks,'pass_groups':groups,'source_hashes':sources,'elf_sha256':sha(elf),
        'elf_bytes':len(data),'embedded_elf_verified':True,'loader':report['loader'],
        'build_report_sha256':sha(report_path),'qemu_kernel_sha256':sha(kernel_path),'qemu_log_sha256':sha(log_path),
        'production_kernel_sha256':kernel_sha,'kernel_reused':True,'baseline_manifest_sha256':sha(baseline/'manifest.json')}
    proof_path.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({'proof':str(proof_path),'groups':42,'exit_status':106}));raise SystemExit(0)
proof=json.loads(proof_path.read_text())
if sha(report_path)!=proof['build_report_sha256'] or sha(elf)!=proof['elf_sha256']:raise SystemExit('Recorded resource build changed')
for path,digest in proof['source_hashes'].items():
    if sha(ROOT/path)!=digest:raise SystemExit('Recorded source changed: '+path)
if args.verify:
    current=manifest(package)
    for entry in base['files']:
        if entry['path']!='OS/System/BUILD.TXT' and sha(package/entry['path']).lower()!=entry['sha256'].lower():raise SystemExit('Baseline system file changed: '+entry['path'])
    programs=package/'OS/System/Programs/UserRuntime';recipe=json.loads((programs/'RESOURCE-SOURCE.JSON').read_text())
    if sha(programs/'resource.elf')!=proof['elf_sha256'] or recipe['proof_sha256']!=sha(proof_path):raise SystemExit('Resource proof differs')
    if sha(programs/'RESOURCE-SOURCES.zip')!=recipe['source_zip_sha256']:raise SystemExit('Source overlay differs')
    if sha(validation/'qemu-final.log')!=proof['qemu_log_sha256']:raise SystemExit('Passing log differs')
    result={'package':str(package),'build':current['build'],'files':len(current['files']),
            'qemu_groups':42,'kernel_reused':True,'baseline_files_preserved':len(base['files'])-1,
            'resource_sha256':proof['elf_sha256'],'guest_validated':False,'browser_integrated':False}
    (validation/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0)
if project.exists():raise SystemExit('Retain the existing source staging tree')
project.mkdir(parents=True)
shutil.copytree(baseline/'OS/System',project/'OS/System')
for source,destination in [('EFI/BOOT/BOOTAA64.EFI','build/BOOTAA64.EFI'),('OS/aa64/STAGE2.EFI','build/STAGE2.EFI'),
        ('OS/aa64/KERNEL.ELF','OS/aa64/KERNEL.ELF'),
        ('OS/System/Programs/Image Viewer/image_viewer.sacx','build/sacx/image_viewer.sacx'),
        ('OS/System/Programs/Dihscover/Dihscover.sacx','build/sacx/Dihscover.sacx')]:
    target=project/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline/source,target)
licenses=baseline/'OS/System/Programs/Dihscover/Licenses'
for name,destination in [('FreeType.txt','third_party/freetype/docs/FTL.TXT'),('HarfBuzz.txt','third_party/harfbuzz/COPYING'),('TLSF.txt','third_party/tlsf/tlsf.h')]:
    target=project/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(licenses/name,target)
for path in licenses.glob('*.txt'):
    target=project/'third_party/netsurf-all-3.11'/path.stem/'COPYING';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
programs=project/'OS/System/Programs/UserRuntime';shutil.copy2(elf,programs/'resource.elf')
paths={ROOT/path for path in proof['source_hashes']}
paths.update(p for p in (ROOT/'tools/webcore_dihos').glob('*') if p.is_file())
paths.update([report_path,proof_path])
source_zip=programs/'RESOURCE-SOURCES.zip'
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(paths):archive.write(path,path.relative_to(ROOT).as_posix())
    archive.writestr('RESOURCE-README.txt','Extract JSC-SOURCES.zip, then HTTP-SOURCES.zip, then this resource-client overlay. No DOM/browser integration is claimed.\n')
(programs/'RESOURCE-SOURCE.JSON').write_text(json.dumps({'elf_sha256':sha(elf),'proof_sha256':sha(proof_path),
    'source_zip_sha256':sha(source_zip),'build_report_sha256':sha(report_path)},indent=2)+'\n')
(programs/'RESOURCE-TEST.txt').write_text('proc:run-net 0:/OS/System/Programs/UserRuntime/resource.elf\nproc:status\nExpected exited, status 0x6a, resource-loop success message, zero remaining heap.\nOrdinary proc:run returns denied-access success 0x6c.\nDiagnostic fixture only: explicitly permits the existing unauthenticated TLS transport for its public example.com request. Production client default rejects unverified TLS.\n')
shutil.copy2(ROOT/'build/user-runtime/qemu.log',validation/'qemu-final.log')
print(json.dumps({'project':str(project),'resource_sha256':sha(elf),'kernel_reused':True}))
