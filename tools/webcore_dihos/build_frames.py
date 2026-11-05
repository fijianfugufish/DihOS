"""Add native child frames to the verified image renderer, without modifying caches."""
from pathlib import Path
import hashlib,json,subprocess,shutil,struct
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-frames-validation/renderer';OUT.mkdir(parents=True,exist_ok=True)
BASE=ROOT/'build/dihscover-images-perf-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=json.loads((BASE/'report.json').read_text());assert base['linked'] and base['elf_sha256']==sha(BASE/'page.elf')
for p,d in base['linked_inputs'].items():assert sha(Path(p))==d,p
client=json.loads((ROOT/'build/webcore-headless-page/compile-report.json').read_text())
engine=json.loads((ROOT/'build/webcore-engine-openssl/report.json').read_text())
original=Path(engine['build'])/'WebCore/PrivateHeaders/WebCore'
shadow=OUT/'headers/WebCore';shadow.mkdir(parents=True,exist_ok=True)
for p in original.iterdir():
 target=shadow/p.name
 if target.is_symlink() and target.readlink()==p:continue
 if p.name=='EmptyFrameLoaderClient.h':
  text=p.read_text();needle='RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) final;'
  assert text.count(needle)==1
  patched=text.replace(needle,'RefPtr<LocalFrame> createFrame(const AtomString&, HTMLFrameOwnerElement&) override;')
  if not target.exists() or target.read_text()!=patched:target.write_text(patched)
 else:
  if target.exists() or target.is_symlink():target.unlink()
  target.symlink_to(p,target_is_directory=p.is_dir())
report={'linked':False,'baseline_sha256':sha(BASE/'report.json'),'sources':{},'linked_inputs':{},'steps':[],
 'frame_client_header':{'original_sha256':sha(original/'EmptyFrameLoaderClient.h'),'patched_sha256':sha(shadow/'EmptyFrameLoaderClient.h')}}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name,argv):
 with (OUT/(name+'.log')).open('w') as f:r=subprocess.run(list(map(str,argv)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
 report['steps'].append({'label':name,'argv':list(map(str,argv)),'exit_code':r.returncode});save()
 if r.returncode:raise SystemExit(name+' failed; see '+str(OUT/(name+'.log')))
template=list(client['argv']);template[1:1]=['-I'+str(shadow.parent),'-I'+str(shadow),'-I'+str(BASE/'include'),'-I'+str(BASE/'include/sdk'),'-I'+str(BASE/'platform')]
replacements={}
for path in [ROOT/'tools/webcore_dihos/headless_page_fixture.cpp',*map(Path,client['support_sources'])]:
 obj=OUT/(path.stem+'.o');cmd=list(template);cmd[cmd.index('-c')+1]=str(path);cmd[cmd.index('-o')+1]=str(obj)
 run('compile-'+path.stem,cmd);replacements[str(BASE/('headless.o' if path.name=='headless_page_fixture.cpp' else path.stem+'.o'))]=str(obj)
 report['sources'][str(path)]=sha(path)
link=list(next(s['argv'] for s in base['steps'] if s['label']=='link'));link=[replacements.get(a,a) for a in link];link[link.index('-o')+1]=str(OUT/'page.elf')
report['linked_inputs']={a:sha(Path(a)) for a in link if a.endswith(('.o','.a','.ld'))}
run('link',link);shutil.copy2(OUT/'page.elf',OUT/'page.symbols.elf')
save()
subprocess.run(['python3',str(ROOT/'tools/webcore_dihos/finish_frames.py')],cwd=ROOT,check=True)
