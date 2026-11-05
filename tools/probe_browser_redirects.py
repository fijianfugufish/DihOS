"""Follow real HTTP redirects with the actual freestanding TLS/HTTP decoder."""
from pathlib import Path
import hashlib,json,subprocess,urllib.parse
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/dihscover-loading-fix-validation'
PROFILE=ROOT/'build/kernel-tls-loadfix'
profile=json.loads((PROFILE/'report.json').read_text());assert profile['compiled'] and profile['host']
for name,digest in profile['inputs'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
results=[]
for first in ['https://www.google.com/search?q=apples','https://en.wikipedia.org/']:
    url=first;hops=[]
    for index in range(9):
        parsed=urllib.parse.urlsplit(url);assert parsed.scheme=='https' and parsed.hostname and not parsed.username and parsed.port in [None,443] and len(url)<=8191
        path=parsed.path or '/'
        if parsed.query:path+='?'+parsed.query
        result=subprocess.run([str(PROFILE/'socket-probe'),parsed.hostname,'443',path],capture_output=True,text=True,timeout=50)
        output=result.stdout+result.stderr
        assert not result.returncode and 'authenticated=1 verify=0x0 error=0' in output and 'complete=1' in output,output
        status=int(output.split(' status=')[1].split()[0]);location=output.split('redirect: ',1)[1].splitlines()[0]
        hops.append({'url':url,'bytes':len(url),'status':status,'output':output})
        print(parsed.hostname,'URL bytes',len(url),'status',status,flush=True)
        if status not in [301,302,303,307,308]:break
        assert location;url=urllib.parse.urljoin(url,location)
    assert hops[-1]['status']==200,'Redirect did not reach a usable response'
    results.append({'start':first,'hops':hops,'complete':True})
(OUT/'live-redirects.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: Google search and Wikipedia redirect chains complete with authenticated responses and unmodified URLs')
