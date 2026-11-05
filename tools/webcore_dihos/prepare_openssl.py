"""Fetch/verify the pinned WebCrypto dependency; does not build a native port."""
from pathlib import Path
import argparse,hashlib,json,tarfile,urllib.request
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--extract',action='store_true');args=parser.parse_args()
version='3.5.9'
pin='603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'
name='openssl-'+version+'.tar.gz'
url='https://github.com/openssl/openssl/releases/download/openssl-'+version+'/'+name
out=ROOT/'build/webcore-openssl';out.mkdir(exist_ok=True)
archive=ROOT/'build/browser-engine-arm64'/name;archive.parent.mkdir(exist_ok=True)
if not archive.exists():
    # Interrupted downloads never become the accepted archive.
    temporary=archive.with_suffix('.download')
    with urllib.request.urlopen(url,timeout=60) as response,temporary.open('wb') as stream:
        while chunk:=response.read(1024*1024):stream.write(chunk)
    if hashlib.sha256(temporary.read_bytes()).hexdigest()!=pin:raise SystemExit('Publisher checksum mismatch')
    temporary.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=pin:raise SystemExit('Pinned OpenSSL source changed')
report={'version':version,'archive':str(archive),'archive_sha256':pin,'source_url':url,
        'publisher_url':'https://openssl-library.org/source/','checksum_url':url+'.sha256',
        'builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'source_verified':True,'native_library_built':False,'webcore_crypto_linked':False,
        'dom_executed':False,'page_rendered':False,'shipping_browser_changed':False}
if args.extract:
    # Extract only into a new directory. Do not treat an existing source tree
    # as pristine merely because it has the right directory name.
    target=out/('source-'+pin[:12])
    if target.exists():raise SystemExit('Extraction target exists; use the verified archive for a fresh port build')
    target.mkdir()
    with tarfile.open(archive) as stream:stream.extractall(target,filter='data')
    report['extracted_source']=str(target/('openssl-'+version))
(out/'source-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('OpenSSL source verified; native threading/entropy/runtime port still required',flush=True)
