"""Package the normal Dihscover app and its embedded renderer; no disk mount."""
from pathlib import Path
import hashlib,json,shutil,datetime,zipfile
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'build/dihos-native-controls-20261102'
VALIDATION=ROOT/'build/dihscover-integration-validation'
PACKAGE=ROOT/'build/dihscover-webcore-integrated-20261102'
PROOF=ROOT/'docs/browser-evaluation/dihscover-webcore-integrated-20261102.json'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def check(ok,message):
 if not ok:raise SystemExit(message)
proof=json.loads(PROOF.read_text())
check(proof['qemu_validated'] and proof['host_existing_ui_validated'] and not proof['manual_helper_launch_required'],'Incomplete integration proof')
for path,digest in {**proof['source_hashes'],**proof['proof_files']}.items():check(sha(ROOT/path)==digest,'Changed tested input: '+path)
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for entry in manifest['files']:
 path=(BASE/entry['path']).resolve();check(path.is_relative_to(BASE) and path.stat().st_size==entry['size'] and sha(path).upper()==entry['sha256'].upper(),'Baseline manifest mismatch')
check(not PACKAGE.exists(),'Keep previous packages; choose a new output directory')
shutil.copytree(BASE,PACKAGE)
source=VALIDATION/'source'
changed={'EFI/BOOT/BOOTAA64.EFI':source/'build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':source/'build/STAGE2.EFI','OS/aa64/KERNEL.ELF':source/'OS/aa64/KERNEL.ELF',
 'OS/System/Programs/Dihscover/Dihscover.sacx':ROOT/'build/sacx/Dihscover.sacx'}
for path,artifact in changed.items():shutil.copy2(artifact,PACKAGE/path)
app=PACKAGE/'OS/System/Programs/Dihscover'
shutil.copy2(ROOT/'build/sacx/Dihscover-WebCore.bin',app/'WebCore.bin')
shutil.copy2(ROOT/'build/sacx/Dihscover-engine.json',app/'ENGINE.json')
shutil.copytree(VALIDATION/'licenses',app/'Licenses/WebCore',dirs_exist_ok=True)
shutil.copy2(PROOF,app/'INTEGRATION.json')
shutil.copy2(ROOT/'docs/dihscover-webcore-integration.md',app/'WEBCORE.md')
shutil.copy2(VALIDATION/'KERNEL-SOURCES.zip',app/'KERNEL-SOURCES.zip')
with zipfile.ZipFile(app/'SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for path in sorted(proof['source_hashes']):archive.write(ROOT/path,path)
 archive.write(PROOF,PROOF.relative_to(ROOT).as_posix())
check(sha(app/'Dihscover.sacx')==proof['frontend_sha256'],'Frontend mismatch')
check(sha(app/'WebCore.bin')==proof['renderer_sha256'],'Renderer mismatch')
check(sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256'],'Kernel mismatch')
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
for entry in manifest['files']:
 path=entry['path']
 if path not in changed and path!='OS/System/BUILD.TXT':check(sha(PACKAGE/path).upper()==entry['sha256'].upper(),'Baseline resource changed: '+path)
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p!=PACKAGE/'manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
result={'package':str(PACKAGE),'build':build,'files':len(files),'bytes':sum(p['size'] for p in files),'normal_dihscover_updated':True,
 'existing_resources_preserved':True,'frontend_sha256':proof['frontend_sha256'],'renderer_sha256':proof['renderer_sha256'],'vhd_updated':False,'hyperv_tested':False}
(VALIDATION/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
