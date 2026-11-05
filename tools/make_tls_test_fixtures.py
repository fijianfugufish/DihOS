"""Generate test-only chains; private keys stay in ignored build storage."""
from pathlib import Path
import datetime,hashlib,json,subprocess,re
root=Path(__file__).resolve().parents[1];out=root/'tools/fixtures/tls';out.mkdir(parents=True,exist_ok=True)
work=root/'build/tls-test-keys';work.mkdir(exist_ok=True)
def run(*args):subprocess.run(['openssl',*map(str,args)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def key(name,ec=False):
    path=work/(name+'.key')
    if ec:run('genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:secp384r1','-out',path)
    else:run('genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',path)
    return path
def ca(name,ec=False):
    k=key(name,ec);pem=work/(name+'.pem')
    run('req','-new','-x509','-key',k,'-sha256','-days','3650','-subj','/CN=DihOS TEST ONLY '+name,
        '-addext','basicConstraints=critical,CA:TRUE,pathlen:2','-addext','keyUsage=critical,keyCertSign,cRLSign','-out',pem)
    return k,pem
rk,rp=ca('rsa-root');ek,ep=ca('ec-root',True)
def issue(name,k,issuer_key,issuer_pem,intermediate=False):
    csr=work/(name+'.csr');pem=work/(name+'.pem');ext=work/(name+'.ext')
    run('req','-new','-key',k,'-subj','/CN='+('DihOS TEST intermediate' if intermediate else 'fixture.dihos.test'),'-out',csr)
    ext.write_text('basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n' if intermediate else
        'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:fixture.dihos.test\n')
    run('x509','-req','-in',csr,'-CA',issuer_pem,'-CAkey',issuer_key,'-CAcreateserial','-days','365','-sha256','-extfile',ext,'-out',pem)
    return pem
ik=key('intermediate');ip=issue('intermediate',ik,rk,rp,True)
lp=issue('rsa-leaf',key('rsa-leaf'),ik,ip);elp=issue('ec-leaf',key('ec-leaf',True),ek,ep)
files={}
for name,pem in [('rsa-root',rp),('ec-root',ep),('intermediate',ip),('rsa-leaf',lp),('ec-leaf',elp)]:
    target=out/(name+'.der');run('x509','-in',pem,'-outform','DER','-out',target)
    files[target.name]=hashlib.sha256(target.read_bytes()).hexdigest()
dates=subprocess.check_output(['openssl','x509','-in',str(lp),'-noout','-dates'],text=True).splitlines()
values=[int(datetime.datetime.strptime(line.split('=',1)[1],'%b %d %H:%M:%S %Y %Z').replace(tzinfo=datetime.timezone.utc).timestamp()) for line in dates]
(work/'roots.pem').write_bytes(rp.read_bytes()+ep.read_bytes())
generator=Path.home()/'.cache/dihos-tls-host/brssl'
code=subprocess.check_output([generator,'ta','-q',work/'roots.pem'],text=True)
code=re.sub(r'\bTA',r'FIXTURE_TA',code);(out/'test-roots.inc').write_text('/* TEST ONLY. Never include in the production trust store. */\n'+code)
files['test-roots.inc']=hashlib.sha256((out/'test-roots.inc').read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps({'purpose':'Test-only certificate chains; not trusted by production',
 'valid_time':values[0]+86400,'expired_time':values[1]+86400,'not_yet_valid_time':values[0]-86400,'files':files},indent=2)+'\n')
print('Generated test-only RSA intermediate and EC chains; private keys retained only under build/')
