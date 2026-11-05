"""Record, stage and verify the actual native WebCore resource milestone."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('action',choices=['record','stage','verify']);args=parser.parse_args()
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
validation=ROOT/'build/webcore-resource-types-validation-20261031';project=validation/'source'
baseline=ROOT/'build/dihos-webcore-resource-20261031';package=ROOT/'build/dihos-webcore-resource-types-20261031'
proof_path=ROOT/'docs/browser-evaluation/native-webcore-resource-types-20261031.json'
bridge_path=ROOT/'build/webcore-resource-bridge/report.json';bridge=json.loads(bridge_path.read_text())
elf=ROOT/'build/webcore-resource-bridge/webcore-resource.elf'
def manifest(directory):
    data=json.loads((directory/'manifest.json').read_text(encoding='utf-8-sig'))
    for entry in data['files']:
        path=(directory/entry['path']).resolve()
        if not path.is_relative_to(directory.resolve()) or path.stat().st_size!=entry['size'] or sha(path).lower()!=entry['sha256'].lower():raise SystemExit('Bad packaged file: '+entry['path'])
    return data
if args.action=='record':
    log_path=ROOT/'build/user-runtime/qemu.log';log=log_path.read_text(encoding='utf-8-sig')
    groups=[line for line in log.splitlines() if line.startswith('PASS:')]
    if len(groups)!=43 or 'SACX WebCore resource: native request/response, header slices and async body OK' not in log or any(x in log for x in ['FAIL:','FATAL','JSC abort']):raise SystemExit('Complete passing 43-group run required')
    if not bridge['linked'] or not bridge['loader_accepted'] or sha(elf)!=bridge['elf_sha256']:raise SystemExit('Bridge executable changed')
    if bridge['fixture_sha256']!=sha(ROOT/'tools/webcore_dihos/webcore_resource_fixture.cpp') or bridge['builder_sha256']!=sha(ROOT/'tools/webcore_dihos/build_resource_bridge.py'):raise SystemExit('Bridge recipe changed')
    kernel_path=ROOT/'build/user-runtime/user-runtime.elf'
    # Verify exact embedded executables without reading the 500+ MiB harness
    # into memory. A matching log alone is insufficient evidence.
    programs={'webcore_resource':('user_webcore_resource_elf',elf),
              'http_manual':('user_http_elf',ROOT/'build/user-http/http.elf')}
    with kernel_path.open('rb') as stream:
        def read(at,size):stream.seek(at);return stream.read(size)
        h=struct.unpack('<16sHHIQQQIHHHHHH',read(0,64))
        sections=[struct.unpack('<IIQQQQIIQQ',read(h[6]+i*h[11],64)) for i in range(h[12])]
        segments=[struct.unpack('<IIQQQQQQ',read(h[5]+i*h[9],56)) for i in range(h[10])]
        symbols={}
        for section in sections:
            if section[1]!=2:continue
            strings=sections[section[6]];names=read(strings[4],strings[5])
            for offset in range(section[4],section[4]+section[5],section[9]):
                entry=struct.unpack('<IBBHQQ',read(offset,24));name=names[entry[0]:].split(b'\0',1)[0].decode(errors='replace')
                if name in [p[0] for p in programs.values()]:symbols[name]=entry[4]
        for name,(symbol,path) in programs.items():
            address=symbols.get(symbol);matched=False
            for segment in segments:
                if address is None or segment[0]!=1 or address<segment[3] or address-segment[3]+path.stat().st_size>segment[5]:continue
                at=segment[2]+address-segment[3]
                with path.open('rb') as binary:
                    while block:=binary.read(1024*1024):
                        if read(at,len(block))!=block:raise SystemExit('Embedded ELF differs: '+name)
                        at+=len(block)
                matched=True;break
            if not matched:raise SystemExit('Missing embedded ELF: '+name)
    snapshot=json.loads((validation/'source.json').read_text())
    for relative,digest in snapshot['files'].items():
        if sha(project/relative)!=digest:raise SystemExit('Snapshot source changed: '+relative)
        if relative!='kernel/shell/dihos_shell.c' and sha(ROOT/relative)!=digest:raise SystemExit('Kernel/runtime source changed after isolated build: '+relative)
    if sha(ROOT/'OS/aa64/KERNEL.ELF')!='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d':raise SystemExit('Working-tree kernel was changed')
    sources=dict(bridge['inputs'])
    for path in (ROOT/'tools/webcore_dihos').rglob('*'):
        if path.is_file() and path.suffix in ['.cpp','.c','.h','.py','.json','.ps1']:sources[path.relative_to(ROOT).as_posix()]=sha(path)
    for relative in ['tools/test_user_runtime_qemu.ps1','tools/user_runtime_qemu_tests.c','tools/embed_user_programs.py',
                     'tools/sacx_network_service_tests.cpp','tools/user_sacx_tests.c','tools/build_jsc_native_test.py',
                     'kernel/apps/sacx_user_network.inc','kernel/process/dihos_user_sacx.c','kernel/include/process/dihos_user_network.h']:
        sources[relative]=sha(ROOT/relative)
    for relative,digest in sources.items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed source: '+relative)
    shutil.copy2(log_path,validation/'qemu-final.log')
    proof={'date':'2026-10-31','scope':'Actual WebCore request/response/error values over owner-thread checked SACX streaming',
           'qemu_validated':True,'qemu_transport_modeled':True,'hyperv_guest_validated':False,'physical_hardware_validated':False,
           'dom_executed':False,'page_rendered':False,'browser_integrated':False,'tls_peer_authentication_supported':False,
           'default_rejects_unverified_tls':True,'manual_redirects':True,'header_read_max':4096,'header_total_max':16384,
           'unsupported_request_features_rejected':['methods other than GET','uploads','custom request headers','URL credentials'],
           'limits':['64 KiB cumulative body','No Frame/ResourceHandle hookup or graphics backend yet'],
           'programs':{name:{'sha256':sha(path),'bytes':path.stat().st_size,'embedded_verified':True} for name,(_,path) in programs.items()},
           'success_exit_status':111,'denied_exit_status':112,'pass_groups':groups,'source_hashes':sources,
           'bridge_report_sha256':sha(bridge_path),'qemu_log_sha256':sha(validation/'qemu-final.log'),
           'qemu_kernel_sha256':sha(kernel_path),'snapshot_source_sha256':sha(validation/'source.json'),
           'production_kernel_sha256':sha(project/'OS/aa64/KERNEL.ELF'),'working_tree_kernel_unchanged':True,
           'baseline_manifest_sha256':sha(baseline/'manifest.json')}
    proof_path.write_text(json.dumps(proof,indent=2)+'\n');print('Recorded 43 groups, exact embedded ELFs and isolated production kernel');raise SystemExit(0)
proof=json.loads(proof_path.read_text())
if sha(elf)!=proof['programs']['webcore_resource']['sha256'] or sha(bridge_path)!=proof['bridge_report_sha256']:raise SystemExit('Recorded bridge changed')
base=manifest(baseline)
if args.action=='verify':
    current=manifest(package)
    changed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
    for entry in base['files']:
        if entry['path'] not in changed and sha(package/entry['path']).lower()!=entry['sha256'].lower():raise SystemExit('Baseline resource changed: '+entry['path'])
    if sha(package/'OS/aa64/KERNEL.ELF')!=proof['production_kernel_sha256']:raise SystemExit('Wrong production kernel')
    programs=package/'OS/System/Programs/UserRuntime'
    for name,entry in proof['programs'].items():
        if sha(programs/(name+'.elf'))!=entry['sha256']:raise SystemExit('Wrong test ELF')
    recipe=json.loads((programs/'WEBCORE-RESOURCE-SOURCE.JSON').read_text())
    if recipe['proof_sha256']!=sha(proof_path) or recipe['source_zip_sha256']!=sha(programs/'WEBCORE-RESOURCE-SOURCES.zip'):raise SystemExit('Wrong source overlay')
    result={'build':current['build'],'files':len(current['files']),'qemu_groups':43,'guest_validated':False,
            'production_kernel_sha256':proof['production_kernel_sha256'],'browser_preserved':True}
    (validation/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0)
if (project/'OS/System/Programs/UserRuntime/WEBCORE-RESOURCE-SOURCE.JSON').exists():raise SystemExit('Staging already exists; retain it')
shutil.copytree(baseline/'OS/System',project/'OS/System',dirs_exist_ok=True)
for source,destination in [('OS/System/Programs/Image Viewer/image_viewer.sacx','build/sacx/image_viewer.sacx'),('OS/System/Programs/Dihscover/Dihscover.sacx','build/sacx/Dihscover.sacx')]:
    target=project/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline/source,target)
licenses=baseline/'OS/System/Programs/Dihscover/Licenses'
for name,destination in [('FreeType.txt','third_party/freetype/docs/FTL.TXT'),('HarfBuzz.txt','third_party/harfbuzz/COPYING'),('TLSF.txt','third_party/tlsf/tlsf.h')]:
    target=project/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(licenses/name,target)
for path in licenses.glob('*.txt'):
    target=project/'third_party/netsurf-all-3.11'/path.stem/'COPYING';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
programs=project/'OS/System/Programs/UserRuntime'
shutil.copy2(elf,programs/'webcore_resource.elf');shutil.copy2(ROOT/'build/user-http/http.elf',programs/'http_manual.elf')
paths={ROOT/path for path in proof['source_hashes']}
paths.update([bridge_path,proof_path,validation/'source.json',ROOT/'build/webcore-native/report.json',ROOT/'build/webcore-native/units/report.json',ROOT/'build/user-jsc-native/resource-report.json'])
source_zip=programs/'WEBCORE-RESOURCE-SOURCES.zip'
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(paths):archive.write(path,path.relative_to(ROOT).as_posix())
    archive.writestr('WEBCORE-RESOURCE-README.txt','Extract JSC-SOURCES.zip, HTTP-SOURCES.zip, RESOURCE-SOURCES.zip, then this overlay. The pinned WPE source archive is inside JSC-SOURCES.zip. This tests resource metadata/streaming, not DOM or painting.\n')
(programs/'WEBCORE-RESOURCE-SOURCE.JSON').write_text(json.dumps({'proof_sha256':sha(proof_path),'source_zip_sha256':sha(source_zip)},indent=2)+'\n')
(programs/'WEBCORE-RESOURCE-TEST.txt').write_text('proc:run-net 0:/OS/System/Programs/UserRuntime/webcore_resource.elf\nproc:status\nExpected exited, status 0x6f; ordinary proc:run reports denied-access success 0x70.\nUses real WebCore request/response/header parsing over HTTP. Unverified HTTPS is rejected. No DOM, page painting or browser integration yet.\nOptional syscall-only test: proc:run-net 0:/OS/System/Programs/UserRuntime/http_manual.elf; expected 0x68.\n')
print('Staged exact tested resource ELFs; baseline browser/resources preserved')
