"""Clone the sealed kernel sources and overlay only the response allowance."""
from pathlib import Path
import argparse,hashlib,io,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--large',action='store_true');p.add_argument('--budget',action='store_true');p.add_argument('--pool-slots',action='store_true');p.add_argument('--vm-tables',action='store_true');p.add_argument('--damage',action='store_true');p.add_argument('--pressure',action='store_true');args=p.parse_args()
args.damage|=args.pressure
args.vm_tables|=args.damage
args.pool_slots|=args.vm_tables
args.budget|=args.pool_slots
args.large|=args.budget
OUT=ROOT/('build/youtube-budget-kernel/src' if args.budget else 'build/youtube-memory-kernel/src' if args.large else 'build/youtube-kernel/src')
if args.pool_slots:OUT=ROOT/'build/youtube-pool-slots-kernel/src'
if args.vm_tables:OUT=ROOT/'build/youtube-vm-tables-kernel/src'
archive=ROOT/('build/dihscover-youtube-memory-20261104/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip' if args.budget else 'build/dihscover-youtube-progress-20261104/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip' if args.large else 'build/dihscover-mse-player-20261104/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip')
if args.pool_slots:archive=ROOT/'build/dihscover-indexeddb-20261104/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip'
if args.vm_tables:archive=ROOT/'build/dihscover-css-size-20261105/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip'
if args.damage:
    OUT=ROOT/'build/youtube-damage-kernel/src'
    archive=ROOT/'build/dihscover-vm-tables-20261105/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip'
if args.pressure:
    OUT=ROOT/'build/youtube-pressure-kernel/src'
    archive=ROOT/'build/dihscover-damage-20261105/OS/System/Programs/Dihscover/KERNEL-SOURCES.zip'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert not (OUT/'OS/aa64/KERNEL.ELF').exists(),'Never overwrite a built kernel snapshot'
with zipfile.ZipFile(archive) as z:
    meta=json.loads(z.read('SOURCE-MANIFEST.json'))
    if 'src/boot.c' not in meta['files']:
        base=subprocess.check_output(['git','archive','--format=zip',meta['base']],cwd=ROOT)
        with zipfile.ZipFile(io.BytesIO(base)) as original:
            for item in original.infolist():
                target=(OUT/item.filename).resolve();assert target.is_relative_to(OUT.resolve())
                if not item.is_dir():
                    data=original.read(item);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    for name,digest in meta['files'].items():
        target=(OUT/name).resolve();assert target.is_relative_to(OUT.resolve())
        data=z.read(name);assert hashlib.sha256(data).hexdigest()==digest,name
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
overlays=['kernel/include/process/dihos_user_memory.h','sdk/sacx/include/sacx_user_api.h'] if args.large else ['kernel/apps/sacx_user_network.inc','sdk/sacx/include/sacx_http_limits.h']
if args.budget:overlays=['kernel/process/dihos_user_image.inc','kernel/process/dihos_user_loader.c','kernel/include/process/dihos_user_runtime.h','kernel/process/dihos_user_runtime.c']
if args.pool_slots:overlays=['kernel/include/process/dihos_user_memory.h','kernel/process/dihos_user_sparse.inc']
if args.vm_tables:overlays=['kernel/include/memory/aarch64_user_vm.h']
if args.damage:overlays=['kernel/process/dihos_user_window.c','kernel/process/dihos_user_sacx.c','sdk/sacx/include/sacx_user_window.h']
if args.pressure:overlays=['kernel/process/dihos_user_image.inc']
for name in overlays:
    target=OUT/name;target.write_bytes((ROOT/name).read_bytes());meta['files'][name]=sha(target)
if args.large and not args.budget:
    # Earlier kernel-only archives omitted the bootloader's textual includes.
    # Retain the exact src tree used to build the previous matched boot files.
    for folder in ['src','include','kernel','mesart','sdk','third_party/BearSSL','third_party/mbedtls','third_party/simplewebp','third_party/tlsf']:
        boot=ROOT/'build/youtube-kernel/src'/folder
        for source in boot.rglob('*'):
            if not source.is_file():continue
            name=folder+'/'+source.relative_to(boot).as_posix()
            if name in overlays:continue
            if name in meta['files']:assert sha(source)==meta['files'][name],name
            target=OUT/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes())
            meta['files'][name]=sha(target)
meta.update(path=str(OUT),parent_archive_sha256=sha(archive),response_limit_bytes=16*1024*1024)
if args.large:meta['memory_region_limit_bytes']=64*1024*1024
if args.budget:meta['browser_memory_limit_bytes']=512*1024*1024
if args.pressure:meta['browser_memory_limit_bytes']=768*1024*1024
if args.pool_slots:meta.update(heap_region_slots=32,sparse_region_slots=16)
if args.vm_tables:meta['private_page_table_limit']=512
if args.large:
    script=OUT/'build.ps1';text=script.read_text()
    assert text.count('[switch]$CrashMap')==1 and text.count('# ---- IMAGE EDITOR SACX')==1
    if '[switch]$KernelOnly' not in text:
        text=text.replace('[switch]$CrashMap','[switch]$KernelOnly,\n  [switch]$CrashMap')
        text=text.replace('# ---- IMAGE EDITOR SACX','if ($KernelOnly) { return }\n\n# ---- IMAGE EDITOR SACX')
        script.write_text(text)
    meta['files']['build.ps1']=sha(script)
meta['files']['src/boot.c']=sha(OUT/'src/boot.c')
(OUT.parent/'source.json').write_text(json.dumps(meta,indent=2)+'\n')
assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
print('Prepared isolated response-limit kernel; other kernel sources retained from sealed package')
