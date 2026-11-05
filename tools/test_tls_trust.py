"""Run real certificate verification and optional live TLS interoperability."""
from pathlib import Path
import argparse,hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--live',action='store_true');a=p.parse_args()
out=root/'build/tls-trust-tests';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
bundle=root/'third_party/ca-certificates';lock=json.loads((bundle/'roots.lock.json').read_text())
for path,digest in [(bundle/'cacert-2026-10-20.pem',lock['sha256']),(bundle/'LICENSE-MPL-2.0.txt',lock['license_sha256']),
                    (root/'kernel/net/tls_roots.inc',lock['generated_sha256'])]:
    if sha(path)!=digest:raise SystemExit('Changed trust input: '+str(path))
fixtures=root/'tools/fixtures/tls';f=json.loads((fixtures/'manifest.json').read_text())
for name,digest in f['files'].items():
    if sha(fixtures/name)!=digest:raise SystemExit('Changed test certificate: '+name)
library=Path.home()/'.cache/dihos-tls-host/libbearssl.a'
def run(name,args):
    with (out/(name+'.log')).open('w') as log:code=subprocess.call(list(map(str,args)),cwd=root,stdout=log,stderr=subprocess.STDOUT)
    print((out/(name+'.log')).read_text()[-2000:])
    if code:raise SystemExit(name+' failed: '+str(code))
run('compile-certificate',['clang-22','-std=c11','-O2','-Ikernel/include','-Ithird_party/BearSSL/inc','tools/tls_certificate_tests.c',library,'-o',out/'certificates'])
run('certificates',[out/'certificates',fixtures,f['valid_time'],f['expired_time'],f['not_yet_valid_time']])
if a.live:
    run('compile-live',['clang-22','-std=c11','-O2','-Ikernel/include','-Ithird_party/BearSSL/inc','tools/tls_live_probe.c',library,'-o',out/'live'])
    for host in ['example.com','www.netsurf-browser.org','en.wikipedia.org','github.com']:
        run('live-'+host,[out/'live',host])
    for host in ['expired.badssl.com','wrong.host.badssl.com','untrusted-root.badssl.com']:
        run('reject-'+host,[out/'live',host,'--reject'])
result={'certificates_verified':True,'live_interoperability':a.live,'roots':lock['certificates'],
        'library_sha256':sha(library),'sources':{name:sha(root/name) for name in ['tools/tls_certificate_tests.c','tools/tls_live_probe.c','kernel/include/net/tls_verify.h','kernel/net/tls_roots.inc']}}
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
