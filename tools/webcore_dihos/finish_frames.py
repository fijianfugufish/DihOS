from pathlib import Path
import json,hashlib,subprocess,struct,shutil
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-frames-validation/renderer';BASE=ROOT/'build/dihscover-images-perf-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((OUT/'report.json').read_text())
for p,d in report['linked_inputs'].items():assert sha(Path(p))==d,p
def run(name,argv):
 with (OUT/(name+'.log')).open('w') as f:r=subprocess.run(list(map(str,argv)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
 assert not r.returncode,name
 report['steps'].append({'label':name,'argv':list(map(str,argv)),'exit_code':r.returncode})
def segments(p):
 data=p.read_bytes();at=struct.unpack_from('<Q',data,32)[0];count=struct.unpack_from('<H',data,56)[0];result=[]
 for i in range(count):
  kind,flags,off,addr,physical,size,memory,alignment=struct.unpack_from('<IIQQQQQQ',data,at+i*56)
  if kind==1:result.append((flags,addr,size,memory,alignment,hashlib.sha256(data[off:off+size]).hexdigest()))
 return result
before=segments(OUT/'page.elf');assert before==segments(OUT/'page.symbols.elf')
strip=shutil.which('llvm-strip-22') or shutil.which('aarch64-linux-gnu-strip');assert strip
run('strip',[strip,'--strip-all',OUT/'page.elf']);assert segments(OUT/'page.elf')==before
run('loader',[BASE/'inspect',OUT/'page.elf']);run('imports',['aarch64-linux-gnu-nm','-u',OUT/'page.symbols.elf']);assert not (OUT/'imports.log').read_text().strip()
for p in (ROOT/'tools/webcore_dihos').glob('*.h'):report['sources'][str(p)]=sha(p)
report.update(linked=True,loader_accepted=True,embedded=True,elf_sha256=sha(OUT/'page.elf'),elf_bytes=(OUT/'page.elf').stat().st_size,fixture_elf_sha256=sha(BASE/'resource.elf'),upstream_archives_unchanged=True)
shutil.copy2(BASE/'resource.elf',OUT/'resource.elf')
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native iframe renderer linked and accepted; execution still requires tests')
