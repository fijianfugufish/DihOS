"""Check opt-in CA generation and fail-closed native CA admission (WSL host)."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
base=root/'build/tls-extra-ca-tests';base.mkdir(exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=base))
profile=json.loads((root/'build/kernel-tls-school-ca/report.json').read_text())
assert profile['compiled'] and profile['host'] and profile['extra_ca']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name,digest in profile['inputs'].items():assert sha(root/name)==digest,name
for name,digest in profile['objects'].items():assert sha(Path(name))==digest,name
flags=list(profile['flags']);at=flags.index(profile['extra_ca']['header']);assert flags[at-1]=='-include';del flags[at-1:at+1]
generator=root/'tools/prepare_tls_extra_ca.py';certificate=root/'tools/fixtures/tls/rsa-leaf.der'
for name,data in [('bad-fingerprint',certificate.read_bytes()),('private-key',b'-----BEGIN PRIVATE KEY-----\nx\n-----END PRIVATE KEY-----\n'),('multiple', (root/'build/dihscover-school-ca-validation/QE_Root_Cert.crt').read_bytes()*2)]:
    path=out/(name+'.crt');path.write_bytes(data);header=out/(name+'.h')
    args=[sys.executable,str(generator),str(path),'--sha256','0'*64,'--output',str(header)]
    result=subprocess.run(args,capture_output=True,text=True);assert result.returncode and not header.exists(),name
print('PASS: wrong fingerprint, private-key input and multiple-certificate bundles rejected')
objects=[name for name in profile['objects'] if Path(name).name!='knet_tls.o']
driver=out/'reject-extra.c'
driver.write_text('#define main unused_validation_main\n#include "kernel_tls_validation_tests.c"\n#undef main\nextern void mbedtls_psa_crypto_free(void);\nint main(void){uint64_t baseline=0;for(unsigned i=0;i<16;++i){check(knet_tls_begin("www.google.com")<0,"reject invalid extra CA");check(!knet_tls_authenticated(),"no authentication");knet_tls_end();if(!i)baseline=pages;else check(pages==baseline,"repeat failed CA leak");}mbedtls_psa_crypto_free();check(!pages && !knet_tls_memory_pages(),"partial root cache cleanup");printf("PASS: non-CA or malformed extra certificate rejects initialization without leaks\\n");return 0;}\n')
for name,data in [('leaf',certificate.read_bytes()),('malformed',b'\x30\x82\xff\xff\x00')]:
    path=out/(name+'.der');path.write_bytes(data);header=out/(name+'.h')
    subprocess.run([sys.executable,str(generator),str(path),'--sha256',sha(path),'--output',str(header)],check=True,capture_output=True)
    obj=out/(name+'.o');binary=out/(name+'-test')
    variant=[*flags,'-include',str(header)]
    subprocess.run([profile['compiler'],*variant,'-c',str(root/'kernel/net/knet_tls.c'),'-o',str(obj)],check=True)
    subprocess.run([profile['compiler'],*variant,'-I'+str(root/'tools'),'-I'+str(root/'build/kernel-tls-school-ca'),str(driver),str(obj),*objects,'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
# A default build has no additional CA, even though a personal build exists.
obj=out/'default.o';binary=out/'default-socket-probe'
subprocess.run([profile['compiler'],*flags,'-c',str(root/'kernel/net/knet_tls.c'),'-o',str(obj)],check=True)
subprocess.run([profile['compiler'],'-O2','-I'+str(root/'kernel/include'),str(root/'tools/kernel_tls_socket_probe.c'),str(root/'kernel/net/http_stream.c'),str(obj),*objects,'-o',str(binary)],check=True)
result=subprocess.run([str(binary),'html.duckduckgo.com'],capture_output=True,text=True,timeout=50)
(out/'default-rejection.log').write_text(result.stdout+result.stderr)
assert result.returncode and 'authenticated=0 verify=0x8' in result.stdout and 'request_bytes=0' in result.stdout
print('PASS: default build still rejects intercepted DuckDuckGo before sending HTTP; additional CA remains opt-in')
