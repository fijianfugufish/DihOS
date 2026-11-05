"""Stage/verify an event-loop test image, preserving every baseline system file."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',default='build/dihos-user-vm-adapter-20261031')
parser.add_argument('--validation',default='build/webcore-loop-validation-20261031')
parser.add_argument('--package',default='build/dihos-webcore-loop-20261031')
parser.add_argument('--verify',action='store_true');args=parser.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def under_build(relative):
    path=(ROOT/relative).resolve()
    if not path.is_relative_to(ROOT/'build'):raise SystemExit('Use build/ paths')
    return path
baseline=under_build(args.baseline);validation=under_build(args.validation);package=under_build(args.package)
def verify_manifest(directory):
    manifest=json.loads((directory/'manifest.json').read_text(encoding='utf-8-sig'))
    for entry in manifest['files']:
        path=(directory/entry['path']).resolve()
        if not path.is_relative_to(directory) or '/Logs/' in entry['path']:raise SystemExit('Invalid manifest path')
        if path.stat().st_size!=entry['size'] or sha(path).lower()!=entry['sha256'].lower():raise SystemExit('Manifest mismatch: '+entry['path'])
    return manifest
base_manifest=verify_manifest(baseline)
proof_path=ROOT/'docs/browser-evaluation/native-webcore-eventloop-20261031.json'
proof=json.loads(proof_path.read_text())
if not proof['qemu_validated'] or len(proof['pass_groups'])!=40:raise SystemExit('Record passing exact-binary event-loop evidence first')
if args.verify:
    manifest=verify_manifest(package)
    for entry in base_manifest['files']:
        if entry['path']=='OS/System/BUILD.TXT':continue # Deliberately regenerated package identity.
        if sha(package/entry['path']).lower()!=entry['sha256'].lower():raise SystemExit('Baseline system file changed: '+entry['path'])
    if manifest['build'] not in (package/'OS/System/BUILD.TXT').read_text():raise SystemExit('Build identity differs from manifest')
    programs=package/'OS/System/Programs/UserRuntime'
    recipe=json.loads((programs/'WEBCORE-LOOP-SOURCE.JSON').read_text())
    if sha(programs/'eventloop.elf')!=proof['embedded_programs']['webcore_loop']['sha256'] or recipe['proof_sha256']!=sha(proof_path):raise SystemExit('Event-loop proof changed')
    if sha(programs/'WEBCORE-LOOP-SOURCES.zip')!=recipe['source_zip_sha256']:raise SystemExit('Event-loop sources changed')
    if sha(programs/'JSC-SOURCES.zip')!=recipe['base_source_zip_sha256']:raise SystemExit('Base engine sources changed')
    if sha(validation/'qemu-final.log')!=proof['qemu_log_sha256']:raise SystemExit('Saved execution log changed')
    for relative,digest in proof['source_hashes'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Tested source changed: '+relative)
    result={'package':str(package),'build':manifest['build'],'files':len(manifest['files']),
            'baseline_files_preserved':len(base_manifest['files'])-1,'build_metadata_regenerated':True,'kernel_unchanged':True,
            'qemu_groups':40,'eventloop_sha256':sha(programs/'eventloop.elf'),
            'hyperv_guest_validated':False,'webcore_dom_compiled':False,'browser_integrated':False}
    (validation/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0)
project=validation/'source'
if project.exists():raise SystemExit('Retain the existing staging tree and choose another --validation path')
project.mkdir(parents=True)
for directory in ['OS/System','OS/Fonts']:
    if (baseline/directory).exists():shutil.copytree(baseline/directory,project/directory)
for source,destination in [('EFI/BOOT/BOOTAA64.EFI','build/BOOTAA64.EFI'),('OS/aa64/STAGE2.EFI','build/STAGE2.EFI'),
                           ('OS/aa64/KERNEL.ELF','OS/aa64/KERNEL.ELF'),
                           ('OS/System/Programs/Image Viewer/image_viewer.sacx','build/sacx/image_viewer.sacx'),
                           ('OS/System/Programs/Dihscover/Dihscover.sacx','build/sacx/Dihscover.sacx')]:
    target=project/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline/source,target)
licenses=baseline/'OS/System/Programs/Dihscover/Licenses'
for source,target in [('FreeType.txt','third_party/freetype/docs/FTL.TXT'),('HarfBuzz.txt','third_party/harfbuzz/COPYING'),('TLSF.txt','third_party/tlsf/tlsf.h')]:
    path=project/target;path.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(licenses/source,path)
for source in licenses.glob('*.txt'):
    target=project/'third_party/netsurf-all-3.11'/source.stem/'COPYING';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
programs=project/'OS/System/Programs/UserRuntime'
r=json.loads((ROOT/'build/user-jsc-native/eventloop-report.json').read_text());key=Path(r['output']).name
elf=ROOT/f'build/user-jsc-native/{key}/jsc.elf'
if sha(elf)!=proof['embedded_programs']['webcore_loop']['sha256']:raise SystemExit('ELF differs from recorded run')
shutil.copy2(elf,programs/'eventloop.elf')
sources=programs/'WEBCORE-LOOP-SOURCES.zip'
paths=set((ROOT/'tools/webcore_dihos').glob('*'))
paths.update(ROOT/p for p in ['tools/build_jsc_native_test.py','tools/embed_user_programs.py',
    'tools/test_user_runtime_qemu.ps1','tools/user_runtime_qemu_tests.c',
    'build/user-jsc-native/eventloop-report.json','build/webcore-dihos/platform-audit.json',
    'docs/browser-evaluation/native-webcore-eventloop-20261031.json'])
with zipfile.ZipFile(sources,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(paths):
        if path.is_file():archive.write(path,path.relative_to(ROOT).as_posix())
    archive.writestr('WEBCORE-LOOP-README.txt','Extract JSC-SOURCES.zip first, then this overlay. It supplies the matching event-loop experiment and recipes; no DOM/browser port is claimed.\n')
recipe={'elf_sha256':sha(elf),'proof_sha256':sha(proof_path),'source_zip_sha256':sha(sources),
        'base_source_zip_sha256':sha(programs/'JSC-SOURCES.zip'),'baseline_manifest_sha256':sha(baseline/'manifest.json')}
(programs/'WEBCORE-LOOP-SOURCE.JSON').write_text(json.dumps(recipe,indent=2)+'\n')
(programs/'WEBCORE-LOOP-TEST.txt').write_text('proc:run 0:/OS/System/Programs/UserRuntime/eventloop.elf\nproc:status\nExpected exited, status 0x67, SACX WebCore event loop success log and zero remaining heap.\nStandalone event-loop/JSC test only; Dihscover and the kernel are retained from the previous image.\n')
shutil.copy2(ROOT/'build/user-runtime/qemu.log',validation/'qemu-final.log')
(validation/'baseline.json').write_text(json.dumps({'package':args.baseline,'manifest_sha256':sha(baseline/'manifest.json')},indent=2)+'\n')
print(json.dumps({'project':str(project),'eventloop_sha256':sha(elf),'baseline_files_preserved':len(base_manifest['files'])}))
