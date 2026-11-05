"""Verify vendored TLS provenance and deterministic production CA conversion."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
vendor=root/'third_party/mbedtls';lock=json.loads((vendor/'source.lock.json').read_text())
for name,digest in lock['files'].items():
    if sha(vendor/name)!=digest:raise SystemExit('Changed vendor input: '+name)
bundle=root/'third_party/ca-certificates';ca=json.loads((bundle/'roots.lock.json').read_text())
pem=bundle/'cacert-2026-10-20.pem'
assert sha(pem)==ca['sha256'] and sha(bundle/'LICENSE-MPL-2.0.txt')==ca['license_sha256']
assert pem.read_text().count('-----BEGIN CERTIFICATE-----')==ca['certificates']==118
text='/* Pinned Mozilla/curl roots, MPL-2.0; generated from the existing CA bundle. */\nstatic const unsigned char dihos_tls_ca_pem[] =\n'+''.join(json.dumps(line)+'\n' for line in pem.read_text().splitlines(keepends=True))+';\n#define DIHOS_TLS_CA_COUNT '+str(ca['certificates'])+'u\n'
assert (root/'kernel/net/tls_roots_pem.inc').read_text()==text
print('PASS: pinned Mbed TLS '+lock['version']+', '+str(len(lock['files']))+' source/license hashes and unchanged 118-root Mozilla bundle')
