"""Seal the explicitly authorized personal CA package; no VM or Windows import."""
from pathlib import Path
import hashlib,json,shutil,zipfile,datetime
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-school-ca-validation'
BASE=ROOT/'build/dihscover-modern-tls-20261102'
PACKAGE=ROOT/'build/dihscover-google-school-ca-20261103'
PREVIOUS=ROOT/'docs/browser-evaluation/dihscover-modern-tls-20261102.json'
DEST=ROOT/'docs/browser-evaluation/dihscover-school-ca-20261103.json'
HOST=ROOT/'build/kernel-tls-school-ca'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def check(ok,why):
    if not ok:raise SystemExit(why)
check(not DEST.exists() and not PACKAGE.exists(),'Retain old proof/package; use a new milestone')
previous=json.loads(PREVIOUS.read_text())
allowed={'tools/export_user_snapshot.py','kernel/net/knet_tls.c','tools/build_kernel_tls.py','tools/test_kernel_tls.py','tools/probe_kernel_tls.py','tools/kernel_tls_socket_probe.c',
 'sdk/sacx/apps/dihscover/address.h','sdk/sacx/apps/dihscover/build_webcore.ps1','tools/browser_controls_tests.cpp','tools/browser_existing_ui_tests.cpp'}
for name,digest in previous['source_hashes'].items():
    if name not in allowed:check(sha(ROOT/name)==digest,'Inherited browser input changed: '+name)
for name,digest in previous['proof_files'].items():check(sha(ROOT/name)==digest,'Inherited evidence changed: '+name)
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
meta=json.loads((OUT/'qe-ca.json').read_text())
check(meta['der_sha256']=='32fee31ac315341a1455d3c7f038b2fa4fc3992c1c5642c8558747a9298bab2b' and meta['additional_ca_count']==1,'Wrong selected certificate')
check(sha(OUT/'qe-ca.h')==meta['header_sha256'],'CA configuration changed')
snapshot=json.loads((OUT/'source.json').read_text())
for name,digest in snapshot['files'].items():check(sha(OUT/'source'/name)==digest,'Snapshot changed: '+name)
check(snapshot['files']['kernel/include/net/dihos_tls_extra_ca.h']==meta['header_sha256'],'Personal snapshot does not contain the authorized CA')
check('DIHOS_TLS_EXTRA_CA_COUNT 0u' in (ROOT/'kernel/include/net/dihos_tls_extra_ca.h').read_text(),'Public default trusts extra CA')
report=json.loads((HOST/'report.json').read_text());check(report['compiled'] and report['host'] and report['extra_ca']['sha256']==meta['header_sha256'],'Wrong tested TLS profile')
for name,digest in report['inputs'].items():check(sha(ROOT/name)==digest,'Tested TLS source changed: '+name)
shutil.copy2(HOST/'report.json',OUT/'host-profile.json')
for name in ['validation.log','html.duckduckgo.com.log','www.google.com.log','example.com.log','www.netsurf-browser.org.log']:
    shutil.copy2(HOST/name,OUT/('tls-'+name))
required={'kernel-build.log':['Built:','Copy to USB:'],
 'root-signature.log':[': OK'],'duckduckgo-chain.log':['Verify return code: 0 (ok)'],
 'extra-ca-tests.log':['wrong fingerprint, private-key input','without leaks','default build still rejects intercepted DuckDuckGo'],
 'tls-validation.log':['validates RSA/ECDSA','100 cancelled handshakes, 16 OOM positions'],
 'tls-html.duckduckgo.com.log':['authenticated=1 verify=0x0 error=0','status=403'],
 'frontend-build.log':['Built SACX app:'],
 'controls-tests.log':['PASS: shared URLs/search','PASS: browser controls'],
 'ui-tests.log':['PASS: Dihscover existing frontend, isolated renderer bridge']}
for name,markers in required.items():
    log=(OUT/name).read_text()
    for marker in markers:check(marker in log,'Missing test: '+name+': '+marker)
    check('FAIL:' not in log and 'Traceback' not in log,'Failed test: '+name)
for host in ['www.google.com','example.com','www.netsurf-browser.org']:
    log=(OUT/('tls-'+host+'.log')).read_text()
    status_ok='complete=1 status=200' in log or (host=='www.google.com' and 'complete=1 status=302' in log)
    check('authenticated=1 verify=0x0 error=0' in log and status_ok,'Public HTTPS regression: '+host)
kernel=OUT/'source/OS/aa64/KERNEL.ELF';check(kernel.read_bytes()[18:20]==b'\xb7\x00','Not an ARM64 ELF')
manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8-sig'))
for item in manifest['files']:
    path=(BASE/item['path']).resolve();check(path.is_relative_to(BASE) and path.stat().st_size==item['size'] and sha(path).upper()==item['sha256'],'Baseline package mismatch')
sources={name:sha(ROOT/name) for name in previous['source_hashes']}
for name,digest in snapshot['files'].items():
    if (ROOT/name).is_file() and sha(ROOT/name)==digest:sources[name]=digest
for name in ['kernel/include/net/dihos_tls_extra_ca.h','tools/prepare_tls_extra_ca.py','tools/test_tls_extra_ca.py','tools/webcore_dihos/package_extra_ca.py','docs/dihscover-extra-ca.md']:sources[name]=sha(ROOT/name)
frontend=OUT/'frontend'
front_meta=json.loads((frontend/'Dihscover-engine.json').read_text(encoding='utf-8-sig'))
check(sha(frontend/'Dihscover.sacx')==front_meta['frontend_sha256'],'Frontend hash mismatch')
check(front_meta['engine_sha256']==previous['renderer_sha256'],'Renderer changed')
for name,digest in front_meta['sources'].items():
    check(sha(ROOT/name)==digest,'Frontend build input changed: '+name);sources[name]=digest
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name in snapshot['files']:archive.write(OUT/'source'/name,name)
    archive.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence=dict(previous['proof_files'])
for p in OUT.iterdir():
    if p.is_file() and p.suffix in ['.log','.json','.zip','.txt'] and p.name!='export.log':evidence[p.relative_to(ROOT).as_posix()]=sha(p)
proof={'date':'2026-11-03','scope':'Google address-bar searches and explicit personal CA configuration; public roots unchanged',
 'frontend_sha256':front_meta['frontend_sha256'],'renderer_sha256':previous['renderer_sha256'],'kernel_sha256':sha(kernel),
 'extra_ca':meta,'trust_scope':'All HTTPS hostnames through shared DihOS client; hostname/signature/date validation retained',
 'default_build_additional_ca_count':0,'public_ca_bundle_changed':False,'windows_trust_changed':False,'vhd_updated':False,
 'duckduckgo_tls_authentication_validated':True,'duckduckgo_access_enabled':False,'duckduckgo_http_status':403,
 'duckduckgo_filter_reason':'Content of type DuckDuckGo ( Domain/URL filtering ) blocked.',
 'filtering_bypassed':False,'incomplete_tls_response_accepted':False,
 'search_engine':'Google','renderer_unchanged':True,'frontend_ui_tests_repeated':True,'browser_validation_inherited_from':PREVIOUS.relative_to(ROOT).as_posix(),
 'hyperv_guest_validated':False,'physical_hardware_validated':False,'source_hashes':sources,'proof_files':evidence,
 'limits':['Live guest retest pending','Personal CA permits school HTTPS inspection','School blocks DuckDuckGo independently of TLS trust','No new cookie/media support']}
DEST.write_text(json.dumps(proof,indent=2)+'\n')
shutil.copytree(BASE,PACKAGE)
changed={'EFI/BOOT/BOOTAA64.EFI':OUT/'source/build/BOOTAA64.EFI','OS/aa64/STAGE2.EFI':OUT/'source/build/STAGE2.EFI','OS/aa64/KERNEL.ELF':kernel,
 'OS/System/Programs/Dihscover/Dihscover.sacx':frontend/'Dihscover.sacx','OS/System/Programs/Dihscover/ENGINE.json':frontend/'Dihscover-engine.json'}
for name,src in changed.items():shutil.copy2(src,PACKAGE/name)
app_path='OS/System/Programs/Dihscover';app=PACKAGE/app_path
shutil.copy2(BASE/app_path/'SOURCES.zip',app/'RENDERER-BASELINE-SOURCES.zip')
shutil.copy2(DEST,app/'INTEGRATION.json');shutil.copy2(ROOT/'docs/dihscover-extra-ca.md',app/'WEBCORE.md');shutil.copy2(OUT/'KERNEL-SOURCES.zip',app/'KERNEL-SOURCES.zip')
(app/'EXTRA-CA.json').write_text(json.dumps({'der_sha256':meta['der_sha256'],'subject':'QE_Root_Cert / Queen Ethelburgas College','all_https_hostnames':True,'explicit_personal_build':True,'public_defaults_changed':False,'revoke_by':'Deploy the public-root-only kernel/package'},indent=2)+'\n')
with zipfile.ZipFile(app/'SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(sources):archive.write(ROOT/name,name)
    archive.write(DEST,DEST.relative_to(ROOT).as_posix())
build=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+proof['kernel_sha256'][:12].upper()
(PACKAGE/'OS/System/BUILD.TXT').write_text('DihOS ARM64 Hyper-V build '+build+'\nSystem file hashes: 0:/manifest.json\n',encoding='ascii')
mutable=set(changed)|{'OS/System/BUILD.TXT'}|{app_path+'/'+n for n in ['INTEGRATION.json','WEBCORE.md','KERNEL-SOURCES.zip','SOURCES.zip']}
for item in manifest['files']:
    if item['path'] not in mutable:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Baseline resource changed')
check(sha(app/'Dihscover.sacx')==proof['frontend_sha256'] and sha(app/'WebCore.bin')==proof['renderer_sha256'],'Browser binaries changed')
files=[{'path':p.relative_to(PACKAGE).as_posix(),'size':p.stat().st_size,'sha256':sha(p).upper()} for p in sorted(PACKAGE.rglob('*')) if p.is_file() and p!=PACKAGE/'manifest.json']
(PACKAGE/'manifest.json').write_text(json.dumps({'version':1,'build':build,'files':files},indent=2)+'\n',encoding='utf-8-sig')
for item in files:check(sha(PACKAGE/item['path']).upper()==item['sha256'],'Packaged hash mismatch')
result={'package':str(PACKAGE),'build':build,'files':len(files),'vhd_updated':False,'windows_trust_changed':False,'search_engine':'Google','duckduckgo_http_status':403,'duckduckgo_access_enabled':False}
(OUT/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
