"""Fetch an explicit Mozilla/curl CA snapshot using host-verified HTTPS."""
from pathlib import Path
import hashlib,json,urllib.request
root=Path(__file__).resolve().parents[1]
out=root/'third_party/ca-certificates';out.mkdir(exist_ok=True)
url='https://curl.se/ca/cacert-2026-10-20.pem'
data=urllib.request.urlopen(url,timeout=60).read()
published=urllib.request.urlopen(url+'.sha256',timeout=60).read().decode().split()[0].lower()
digest=hashlib.sha256(data).hexdigest()
if digest!=published:raise SystemExit('CA bundle does not match published SHA-256')
count=data.count(b'-----BEGIN CERTIFICATE-----')
if count!=118:raise SystemExit('Unexpected CA snapshot count')
(out/'cacert-2026-10-20.pem').write_bytes(data)
(out/'LICENSE-MPL-2.0.txt').write_bytes(urllib.request.urlopen('https://www.mozilla.org/media/MPL/2.0/index.txt',timeout=60).read())
lock={'url':url,'sha256':digest,'certificates':count,'upstream':'Mozilla CA store converted by curl',
      'license':'MPL-2.0','license_sha256':hashlib.sha256((out/'LICENSE-MPL-2.0.txt').read_bytes()).hexdigest(),
      'note':'The PEM conversion does not retain additional Mozilla policy constraints; see curl CA Extract documentation.'}
(out/'roots.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
print(json.dumps(lock))
