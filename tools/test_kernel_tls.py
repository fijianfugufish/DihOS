"""Run real cryptographic/cleanup checks against the freestanding host profile."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/kernel-tls-host');a=p.parse_args()
out=a.output.resolve()
report=json.loads((out/'report.json').read_text())
assert report['compiled'] and report['host']
if report.get('extra_ca'):assert hashlib.sha256(Path(report['extra_ca']['header']).read_bytes()).hexdigest()==report['extra_ca']['sha256']
for name,digest in report['inputs'].items():
    assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest, name
fixtures=ROOT/'tools/fixtures/tls'
manifest=json.loads((fixtures/'manifest.json').read_text())
vectors='/* Test-only certificate vectors; never production trust anchors. */\n'
for name,key in [('valid_time','VALID_TIME'),('expired_time','EXPIRED_TIME'),('not_yet_valid_time','FUTURE_TIME')]:
    vectors+='#define FIXTURE_'+key+' '+str(manifest[name])+'ull\n'
for name in ['rsa-root','ec-root','rsa-leaf','ec-leaf','intermediate']:
    data=(fixtures/(name+'.der')).read_bytes()
    assert hashlib.sha256(data).hexdigest()==manifest['files'][name+'.der']
    vectors+='static const unsigned char FIXTURE_'+name.upper().replace('-','_')+'[] = {'+','.join(str(b) for b in data)+'};\n'
(out/'tls_validation_fixtures.inc').write_text(vectors)
for name,digest in report['objects'].items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest, name
args=[report['compiler'],*report['flags'],'-I'+str(out),str(ROOT/'tools/kernel_tls_validation_tests.c'),*report['objects'], '-o',str(out/'validation-tests')]
with (out/'validation-link.log').open('w') as log:
    subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
with (out/'validation.log').open('w') as log:
    result=subprocess.run([str(out/'validation-tests')],stdout=log,stderr=subprocess.STDOUT,timeout=60)
print((out/'validation.log').read_text())
raise SystemExit(result.returncode)
