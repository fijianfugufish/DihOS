"""Package the tested local WebCore window diagnostic; never mounts a disk."""
from pathlib import Path
import argparse,hashlib,json,shutil,datetime,zipfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--baseline',default='build/dihos-webcore-resource-types-20261031');p.add_argument('--validation',default='build/native-window-validation');p.add_argument('--package',default='build/dihos-native-window-20261101');p.add_argument('--proof',default='docs/browser-evaluation/native-window-20261101.json');a=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def under(path):
 p=(ROOT/path).resolve()
 if not p.is_relative_to(ROOT/'build'):raise SystemExit('Use an isolated build directory')
 return p
base,validation,package=map(under,[a.baseline,a.validation,a.package]);project=validation/'source'
proof_path=(ROOT/a.proof).resolve();proof=json.loads(proof_path.read_text())
if not proof['qemu_validated'] or not proof['desktop_adapter_built']:raise SystemExit('Validate the native window first')
for rel,digest in proof['source_hashes'].items():
 if sha(ROOT/rel)!=digest:raise SystemExit('Tested source changed: '+rel)
for rel,digest in proof['proof_files'].items():
 if sha(ROOT/rel)!=digest:raise SystemExit('Test evidence changed: '+rel)
if sha(project/'OS/aa64/KERNEL.ELF')!=proof['desktop_kernel_sha256']:raise SystemExit('Isolated kernel changed')
elf=ROOT/'build/webcore-headless-page/page.elf'
if sha(elf)!=proof['page_elf_sha256']:raise SystemExit('Page ELF changed')
manifest=json.loads((base/'manifest.json').read_text(encoding='utf-8-sig'))
for entry in manifest['files']:
 file=(base/entry['path']).resolve()
 if not file.is_relative_to(base) or file.stat().st_size!=entry['size'] or sha(file).lower()!=entry['sha256'].lower():raise SystemExit('Baseline manifest mismatch')
if package.exists():raise SystemExit('Retain existing package; choose a fresh --package directory')
shutil.copytree(base,package)
changed={'EFI/BOOT/BOOTAA64.EFI':project/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':project/'build/STAGE2.EFI','OS/aa64/KERNEL.ELF':project/'OS/aa64/KERNEL.ELF'}
for rel,source in changed.items():shutil.copyfile(source,package/rel)
programs=package/'OS/System/Programs/UserRuntime';shutil.copyfile(elf,programs/'page.elf')
licenses=programs/'Licenses/NativePage';licenses.mkdir(parents=True)
for source in (validation/'licenses').iterdir():shutil.copyfile(source,licenses/source.name)
if len(list(licenses.iterdir()))<8:raise SystemExit('Stage native dependency licences first')
for source in (ROOT/'OS/System/Fonts/Web').glob('*OFL.txt'):shutil.copyfile(source,licenses/source.name)
paths=[ROOT/rel for rel in proof['source_hashes']]
paths += [ROOT/'tools/webcore_dihos/port-edits.json',ROOT/'tools/webcore_dihos/image-edits.json',ROOT/'tools/webcore_dihos/engine-edits.json',proof_path,ROOT/'docs/dihscover-native-window.md']
with zipfile.ZipFile(programs/'NATIVE-PAGE-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for source in sorted(set(paths)):archive.write(source,source.relative_to(ROOT).as_posix())
 archive.writestr('README.txt','Native ARM64 local WebCore Page diagnostic. Rebuild the pinned WebCore/JSC and native dependencies using the included recipes and recorded hashes. The prior JSC source bundle and licences remain alongside this overlay.\n')
(programs/'NATIVE-PAGE-SOURCE.JSON').write_text(json.dumps({'proof_sha256':sha(proof_path),'elf_sha256':sha(elf),'sources_zip_sha256':sha(programs/'NATIVE-PAGE-SOURCES.zip'),'desktop_kernel_sha256':proof['desktop_kernel_sha256']},indent=2)+'\n')
(programs/'NATIVE-PAGE-TEST.txt').write_text('proc:run-ui 0:/OS/System/Programs/UserRuntime/page.elf\nClick the blue box: heading becomes clicked in DihOS and box becomes green. Close the window; proc:status should show exited, code 0x78 and the native Page window success log. proc:stop cancels and destroys owned windows.\nLocal HTML diagnostic only: fixed 640x480 viewport, primary pointer/wheel path; no live navigation, keyboard editing, dynamic viewport resizing or website/media compatibility claim.\n')
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+sha(package/'OS/aa64/KERNEL.ELF')[:12].upper()
(package/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
for entry in manifest['files']:
 rel=entry['path']
 if rel not in changed and rel!='OS/System/BUILD.TXT' and sha(package/rel).lower()!=entry['sha256'].lower():raise SystemExit('Baseline resource changed: '+rel)
files=[{'path':p.relative_to(package).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(package.rglob('*')) if p.is_file() and p!=package/'manifest.json']
(package/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
result={'package':str(package),'build':build,'files':len(files),'bytes':sum(x['size'] for x in files),'baseline_resources_preserved':True,'shipping_browser_sha256':sha(package/'OS/System/Programs/Dihscover/Dihscover.sacx'),'vhd_updated':False,'hyperv_tested':False}
(validation/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
