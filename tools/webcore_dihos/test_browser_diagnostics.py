"""Sanitize the bounded renderer diagnostic snapshots on the host."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-youtube-bootstrap-validation/diagnostics';OUT.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
steps=[]
for name,args in [('compile',['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
    'tools/browser_diagnostics_tests.cpp','-o',str(OUT/'test')]),('run',[str(OUT/'test')])]:
    with (OUT/(name+'.log')).open('w') as log:result=subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    steps.append({'label':name,'argv':args,'exit_code':result.returncode});assert not result.returncode,name
files=['tools/browser_diagnostics_tests.cpp','tools/webcore_dihos/browser_diagnostics.h','tools/webcore_dihos/test_browser_diagnostics.py']
(OUT/'report.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],
    'inputs':{n:sha(ROOT/n) for n in files},'steps':steps},indent=2)+'\n')
print('PASS: bounded diagnostic capture, sanitization, error priority, saturation and reset')
