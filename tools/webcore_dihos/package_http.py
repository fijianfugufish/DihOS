"""Stage/verify the HTTP development image using the separately built kernel."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--verify',action='store_true');args=parser.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
validation=ROOT/'build/user-validation-http-20261031';project=validation/'source'
baseline=ROOT/'build/dihos-webcore-loop-20261031';package=ROOT/'build/dihos-user-http-20261031'
proof_path=ROOT/'docs/browser-evaluation/native-user-http-20261031.json';proof=json.loads(proof_path.read_text())
def check_manifest(directory):
    manifest=json.loads((directory/'manifest.json').read_text(encoding='utf-8-sig'))
    for entry in manifest['files']:
        path=(directory/entry['path']).resolve()
        if not path.is_relative_to(directory) or path.stat().st_size!=entry['size'] or sha(path).lower()!=entry['sha256'].lower():raise SystemExit('Manifest mismatch: '+entry['path'])
    return manifest
base_manifest=check_manifest(baseline)
if args.verify:
    manifest=check_manifest(package)
    changed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
    for entry in base_manifest['files']:
        if entry['path'] not in changed and sha(package/entry['path']).lower()!=entry['sha256'].lower():raise SystemExit('Baseline resource changed: '+entry['path'])
    if sha(package/'OS/aa64/KERNEL.ELF')!=proof['production_kernel_sha256']:raise SystemExit('Kernel differs from isolated build')
    programs=package/'OS/System/Programs/UserRuntime';recipe=json.loads((programs/'HTTP-SOURCE.JSON').read_text())
    if recipe['proof_sha256']!=sha(proof_path) or sha(programs/'HTTP-SOURCES.zip')!=recipe['source_zip_sha256']:raise SystemExit('HTTP provenance changed')
    for name,entry in proof['programs'].items():
        if sha(programs/(name+'.elf'))!=entry['sha256']:raise SystemExit('HTTP test ELF changed')
    if sha(validation/'qemu-http-final.log')!=proof['qemu_log_sha256']:raise SystemExit('Passing QEMU log changed')
    for path,digest in proof['source_hashes'].items():
        if sha(ROOT/path)!=digest:raise SystemExit('Tested source changed: '+path)
    result={'package':str(package),'build':manifest['build'],'files':len(manifest['files']),'qemu_groups':41,
            'production_kernel_sha256':proof['production_kernel_sha256'],'browser_preserved':True,
            'hyperv_guest_validated':False,'tls_peer_authentication':False,'live_network_validated':False}
    (validation/'http-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0)
if sha(project/'OS/aa64/KERNEL.ELF')!=proof['production_kernel_sha256']:raise SystemExit('Isolated kernel changed')
shutil.copytree(baseline/'OS/System',project/'OS/System',dirs_exist_ok=True)
for source,target in [('OS/System/Programs/Dihscover/Dihscover.sacx','build/sacx/Dihscover.sacx'),
                      ('OS/System/Programs/Image Viewer/image_viewer.sacx','build/sacx/image_viewer.sacx')]:
    destination=project/target;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline/source,destination)
programs=project/'OS/System/Programs/UserRuntime'
for name in proof['programs']:shutil.copy2(ROOT/f'build/user-http/{name}.elf',programs/(name+'.elf'))
source_zip=programs/'HTTP-SOURCES.zip';overlay=json.loads((validation/'source.json').read_text())
paths={p:project/p for p in overlay['files']}
for relative in proof['source_hashes']:paths[relative]=ROOT/relative
for path in (ROOT/'tools/webcore_dihos').glob('*'):
    if path.is_file():paths[path.relative_to(ROOT).as_posix()]=path
paths['docs/browser-evaluation/native-user-http-20261031.json']=proof_path
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as archive:
    for relative,path in sorted(paths.items()):archive.write(path,relative)
    archive.write(validation/'source.json','DIHOS-KERNEL-OVERLAY.json')
(programs/'HTTP-SOURCE.JSON').write_text(json.dumps({'proof_sha256':sha(proof_path),'source_zip_sha256':sha(source_zip),
    'production_kernel_sha256':proof['production_kernel_sha256'],'base_sources':'JSC-SOURCES.zip, then HTTP-SOURCES.zip overlay'},indent=2)+'\n')
(programs/'HTTP-TEST.txt').write_text('proc:run 0:/OS/System/Programs/UserRuntime/http.elf\nExpected denied-access success, exited 0x69.\nproc:run-net 0:/OS/System/Programs/UserRuntime/http.elf\nExpected live asynchronous GET success, exited 0x68.\nOptional https.elf uses HTTPS but this transport does NOT authenticate the TLS peer. No credentials.\nproc:status reports progress/failure; proc:stop cancels.\n')
shutil.copy2(ROOT/'build/user-runtime/qemu.log',validation/'qemu-http-final.log')
print(json.dumps({'project':str(project),'kernel_sha256':proof['production_kernel_sha256']}))
