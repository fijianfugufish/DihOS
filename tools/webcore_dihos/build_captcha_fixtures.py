"""Test-only incremental relink; production candidate objects are checked, not changed."""
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-captcha-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((OUT/'report.json').read_text());assert report['linked'] and sha(OUT/'page.elf')==report['elf_sha256']
for p,d in report['linked_inputs'].items():assert sha(Path(p))==d,p
step=next(s for s in report['steps'] if s['label']=='compile-resource-fixture');cmd=list(step['argv'])
def run(name,args):
 with (OUT/(name+'.log')).open('w') as f:r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
 assert not r.returncode,name
run('compile-resource-fixture',cmd)
link=list(next(s['argv'] for s in report['steps'] if s['label']=='fixture-link'));run('fixture-link',link)
inspect=ROOT/'build/dihscover-images-perf-validation/renderer/inspect'
run('fixture-loader',[inspect,OUT/'resource.elf'])
source=ROOT/'tools/webcore_dihos/webcore_resource_fixture.cpp';report['sources'][str(source)]=sha(source)
report['fixture_elf_sha256']=sha(OUT/'resource.elf')
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: WebCore GET/POST resource fixture relinked against checked production objects')
