"""Compile the exact freestanding TLS profile for ARM64 or a host harness."""
from pathlib import Path
import argparse,subprocess,json,hashlib,concurrent.futures,platform
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--host',action='store_true');p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='clang-22');p.add_argument('--extra-ca-header',type=Path);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
vendor=ROOT/'third_party/mbedtls';profile=ROOT/'kernel/include/net/dihos_tls_build.h'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
lock=json.loads((vendor/'source.lock.json').read_text())
for name,digest in lock['files'].items():
 if sha(vendor/name)!=digest:raise SystemExit('Changed upstream TLS source: '+name)
flags=['-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-I'+str(vendor/'compat'),'-I'+str(vendor/'include'),'-I'+str(vendor/'library'),'-I'+str(ROOT/'kernel/include'),'-iquote',str(ROOT/'include'),'-include',str(profile)]
if not a.host:flags+=['--target=aarch64-none-elf','-march=armv8-a','-mstrict-align','-fPIE']
flags+=['-DNDEBUG']
extra_ca=None
if a.extra_ca_header:
 extra=a.extra_ca_header.resolve();extra_ca={'header':str(extra),'sha256':sha(extra)};flags+=['-include',str(extra)]
sources=sorted((vendor/'library').glob('*.c'))+[ROOT/'kernel/net/knet_tls.c',ROOT/'kernel/net/knet_tls_platform.c']
def compile(source):
 target=out/(source.stem+'.o');log=out/(source.stem+'.log')
 with log.open('w') as f:rc=subprocess.call([a.cc,*flags,'-c',str(source),'-o',str(target)],stdout=f,stderr=subprocess.STDOUT)
 return source,rc,target
failed=[];objects=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for source,rc,target in pool.map(compile,sources):
  if rc:failed.append(source.name)
  else:objects.append(target)
inputs={p.relative_to(ROOT).as_posix():sha(p) for directory in [vendor,ROOT/'kernel/include/net'] for p in directory.rglob('*') if p.is_file() and p.suffix in ['.h','.c','.json']}
for source in sources:inputs[source.relative_to(ROOT).as_posix()]=sha(source)
inputs['kernel/net/tls_roots_pem.inc']=sha(ROOT/'kernel/net/tls_roots_pem.inc')
report={'compiled':not failed,'host':a.host,'failed':failed,'objects':{str(o):sha(o) for o in objects},'inputs':inputs,'compiler':a.cc,'flags':flags,'extra_ca':extra_ca}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'compiled':not failed,'failed':failed,'objects':len(objects)}));raise SystemExit(bool(failed))
