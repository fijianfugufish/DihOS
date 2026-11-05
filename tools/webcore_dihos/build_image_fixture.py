"""Rebuild only test instrumentation against the exact checked shipping objects."""
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-images-perf-validation/renderer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((OUT/'report.json').read_text());assert report['linked'] and report['elf_sha256']==sha(OUT/'page.elf')
for p,d in report['linked_inputs'].items():assert sha(Path(p))==d,p
client=json.loads((ROOT/'build/webcore-headless-page/compile-report.json').read_text())
command=list(client['argv']);command[1:1]=['-DDIHOS_IMAGE_TEST_FIXTURE=1','-I'+str(OUT/'include'),'-I'+str(OUT/'include/sdk'),'-I'+str(OUT/'platform')]
source=ROOT/'tools/webcore_dihos/headless_page_fixture.cpp';target=OUT/'image-headless.o'
command[command.index('-c')+1]=str(source);command[command.index('-o')+1]=str(target)
def run(name,argv):
 with (OUT/(name+'.log')).open('w') as f:r=subprocess.run(list(map(str,argv)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
 assert not r.returncode,name
run('compile-image-headless',command)
link=list(next(s['argv'] for s in report['steps'] if s['label']=='link'))
link=[str(target) if a==str(OUT/'headless.o') else a for a in link];link[link.index('-o')+1]=str(OUT/'images-test.elf')
run('images-test-link',link);run('images-test-loader',[OUT/'inspect',OUT/'images-test.elf'])
report['image_test_elf_sha256']=sha(OUT/'images-test.elf')
report['image_test_sources']={str(p.relative_to(ROOT)):sha(p) for p in [source,ROOT/'tools/webcore_dihos/image_decoder_fixture.h',ROOT/'tools/browser_fixtures/images/fixtures.inc',Path(__file__)]}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: image decoder ownership/validation fixture linked against verified shipping objects')
