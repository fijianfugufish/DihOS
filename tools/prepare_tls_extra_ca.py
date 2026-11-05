"""Generate an explicit, fingerprint-pinned personal TLS build configuration.

Does not install into Windows or alter the public CA bundle. Certificate type
and signature validation must also pass the native TLS tests/live chain check.
"""
from pathlib import Path
import argparse,base64,hashlib,json,re,ssl
p=argparse.ArgumentParser()
p.add_argument('certificate',type=Path)
p.add_argument('--sha256',required=True,help='Expected SHA-256 of the DER certificate, not the PEM text')
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
data=a.certificate.read_bytes()
if len(data)>65536 or b'PRIVATE KEY' in data:raise SystemExit('Expected one public CA certificate')
if b'-----BEGIN' in data:
    text=data.decode('ascii').strip()
    if not re.fullmatch(r'-----BEGIN CERTIFICATE-----\s+[A-Za-z0-9+/=\s]+-----END CERTIFICATE-----',text):raise SystemExit('Expected exactly one PEM certificate')
    der=ssl.PEM_cert_to_DER_cert(text)
else:der=data
digest=hashlib.sha256(der).hexdigest()
expected=a.sha256.lower().replace(':','')
if not re.fullmatch('[0-9a-f]{64}',expected) or digest!=expected:raise SystemExit('Certificate fingerprint mismatch')
pem='-----BEGIN CERTIFICATE-----\n'+'\n'.join(re.findall('.{1,64}',base64.b64encode(der).decode()))+'\n-----END CERTIFICATE-----\n'
header='/* Explicit personal build CA; public defaults remain unchanged. */\n#pragma once\n#define DIHOS_TLS_EXTRA_CA_COUNT 1u\n#define DIHOS_TLS_EXTRA_CA_SHA256 "'+digest+'"\n#define DIHOS_TLS_EXTRA_CA_PEM '+json.dumps(pem)+'\n'
if a.output.exists():raise SystemExit('Retain existing CA configuration; choose a fresh output')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(header)
metadata={'der_sha256':digest,'pem_sha256':hashlib.sha256(pem.encode()).hexdigest(),'header_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'additional_ca_count':1,'windows_trust_changed':False}
a.output.with_suffix('.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata))
