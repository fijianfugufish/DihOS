"""Independent read-only validation of the sealed images/performance package."""
from pathlib import Path
import json,hashlib,zipfile
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-images-perf-validation'
PACKAGE=ROOT/'build/dihscover-images-perf-20261103';BASE=ROOT/'build/dihscover-loading-fix-20261103'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
proof=json.loads((ROOT/'docs/browser-evaluation/dihscover-images-perf-20261103.json').read_text())
manifest=json.loads((PACKAGE/'manifest.json').read_text(encoding='utf-8-sig'))
paths=set()
for item in manifest['files']:
 p=(PACKAGE/item['path']).resolve();assert p.is_relative_to(PACKAGE) and p.is_file()
 assert item['path'] not in paths;paths.add(item['path'])
 assert p.stat().st_size==item['size'] and sha(p).upper()==item['sha256'],item['path']
assert paths=={p.relative_to(PACKAGE).as_posix() for p in PACKAGE.rglob('*') if p.is_file() and p.name!='manifest.json'}
app=PACKAGE/'OS/System/Programs/Dihscover'
assert sha(app/'Dihscover.sacx')==proof['frontend_sha256']
assert sha(app/'WebCore.bin')==proof['renderer_sha256']
assert sha(PACKAGE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
assert json.loads((app/'INTEGRATION.json').read_text())==proof
for n,d in proof['source_hashes'].items():assert sha(ROOT/n)==d,n
for n,d in proof['proof_files'].items():assert sha(ROOT/n)==d,n
with zipfile.ZipFile(app/'SOURCES.zip') as z:
 for n,d in proof['source_hashes'].items():assert hashlib.sha256(z.read(n)).hexdigest()==d,n
with zipfile.ZipFile(app/'KERNEL-SOURCES.zip') as z:
 snapshot=json.loads(z.read('SOURCE-MANIFEST.json'))
 for n,d in snapshot['files'].items():assert hashlib.sha256(z.read(n)).hexdigest()==d,n
 assert snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==proof['extra_ca']['header_sha256']
old=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
allowed={'EFI/BOOT/BOOTAA64.EFI','OS/aa64/STAGE2.EFI','OS/aa64/KERNEL.ELF','OS/System/BUILD.TXT'}
allowed|={'OS/System/Programs/Dihscover/'+n for n in ['Dihscover.sacx','WebCore.bin','ENGINE.json','INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip','RENDERER-BASELINE-SOURCES.zip']}
for item in old['files']:
 if item['path'] not in allowed:assert sha(PACKAGE/item['path']).upper()==item['sha256'],item['path']
assert 'Copyright (c) 2017 Sean Barrett' in (app/'LICENCES/stb_image.txt').read_text()
assert 'Copyright (c) 2010 Google Inc., 2023 Miku AuahDark' in (app/'LICENCES/simplewebp.txt').read_text()
qemu=json.loads((OUT/'qemu-page/report.json').read_text());renderer=json.loads((OUT/'renderer/report.json').read_text())
assert qemu['image_test_sha256']==renderer['image_test_elf_sha256']==sha(OUT/'renderer/images-test.elf')
assert qemu['kernel_sha256']==sha(OUT/'qemu-page/kernel.elf')
for n,d in renderer['image_test_sources'].items():assert sha(ROOT/n)==d,n
assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
print('PASS: all manifest files, inherited assets/licences, exact frontend/renderer/kernel, archived sources, personal CA, ARM64 test hashes and unchanged working kernel')
