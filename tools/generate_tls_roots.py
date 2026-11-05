"""Convert the pinned public roots with BearSSL's own X.509 decoder."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--brssl',required=True);a=p.parse_args()
directory=root/'third_party/ca-certificates';lock=json.loads((directory/'roots.lock.json').read_text())
pem=directory/'cacert-2026-10-20.pem'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if sha(pem)!=lock['sha256'] or sha(directory/'LICENSE-MPL-2.0.txt')!=lock['license_sha256']:raise SystemExit('Pinned root inputs changed')
data=subprocess.check_output([a.brssl,'ta','-q',str(pem)],text=True)
if f'TAs[{lock["certificates"]}]' not in data or data.count('BR_X509_TA_CA')!=lock['certificates']:raise SystemExit('Root conversion count/CA policy mismatch')
data=data.replace('TAs_NUM','DIHOS_TLS_ROOTS_COUNT').replace('TAs[','DIHOS_TLS_ROOTS[')
data=re.sub(r'\bTA(\d+)_',r'DIHOS_TLS_ANCHOR_\1_',data)
prefix='/* Generated from Mozilla/curl CA data (MPL-2.0). See third_party/ca-certificates. */\n'
prefix+=f'#define DIHOS_TLS_ROOTS_SHA256 "{lock["sha256"]}"\n#define DIHOS_TLS_ROOTS_DATE "2026-10-20"\n'
target=root/'kernel/net/tls_roots.inc';target.write_text(prefix+data)
lock['generated_sha256']=sha(target);(directory/'roots.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
print('Generated',lock['certificates'],'compiled trust anchors:',lock['generated_sha256'])
