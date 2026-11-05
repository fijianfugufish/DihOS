"""Seal the native Frame navigation regression and isolated desktop build."""
from pathlib import Path
import hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/native-controls-validation'
destination=ROOT/'docs/browser-evaluation/native-controls-20261102.json'
if destination.exists():raise SystemExit('Retain the existing proof; choose a new milestone name')
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def check(value,message):
    if not value:raise SystemExit(message)
page_path=ROOT/'build/webcore-headless-page/report.json';page=json.loads(page_path.read_text())
check(page.get('https'),'Compile the HTTPS preview')
check(page.get('linked') and page.get('loader_accepted') and page.get('navigation'),'Build the navigation ELF first')
for relative,key in [('tools/webcore_dihos/build_page_fixture.py','builder_sha256'),
 ('build/webcore-engine-openssl/report.json','engine_report_sha256'),
 ('build/webcore-headless-page/compile-report.json','client_compile_report_sha256')]:
    check(sha(ROOT/relative)==page[key],'Stale Page build input: '+relative)
check(sha(ROOT/'build/webcore-headless-page/page.elf')==page['elf_sha256'],'Page ELF changed')
client=json.loads((ROOT/'build/webcore-headless-page/compile-report.json').read_text())
for name,digest in client['navigation_inputs'].items():check(sha(ROOT/name)==digest,'Changed navigation input: '+name)
qemu=ROOT/'build/user-runtime/qemu.log';log=qemu.read_text()
check('PASS: native WebCore Frame navigation, redirects, external CSS/JavaScript, links, Stop on stalled load, Reload, pixels and cancellation cleanup (modeled HTTP)' in log,'Navigation regression incomplete')
check('PASS: native browser typed address, DOM keyboard pixels, form edits/cancellation, Back and Forward history (modeled HTTP)' in log,'Browser controls regression incomplete')
check('PASS: sparse quota/slot bounds' in log and 'FAIL:' not in log and 'FATAL' not in log,'Guest regression failed')
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working-tree kernel changed')
snapshot=json.loads((out/'source.json').read_text())
for name,digest in snapshot['files'].items():check(sha(out/'source'/name)==digest,'Changed isolated source: '+name)
check((out/'source/OS/aa64/KERNEL.ELF').is_file(),'Complete isolated desktop build first')
shutil.copy2(qemu,out/'qemu-final.log');shutil.copy2(ROOT/'build/user-loader-web-test.log',out/'host-loader.log')
check('PASS: one-ms loader budget' in (out/'host-loader.log').read_text(),'Host loader checks missing')
reports=['build/webcore-headless-page/report.json','build/webcore-headless-page/compile-report.json',
 'build/webcore-engine-openssl/report.json','build/webcore-engine-openssl/compile-report.json',
 'build/user-runtime/base-programs.json']
for name in reports:
    path=out/'reports'/Path(name).parent.name/Path(name).name
    path.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,path)
sources={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'tools/webcore_dihos').rglob('*')
    if p.is_file() and p.suffix in ['.h','.cpp','.c','.py','.json','.ld','.ps1']}
for name in ['kernel/process/dihos_user_loader.c','kernel/include/process/dihos_user_loader.h',
 'kernel/shell/dihos_user_commands.inc','kernel/apps/dihos_user_window_desktop.cpp',
 'kernel/process/dihos_user_window.c','kernel/include/process/dihos_user_window.h',
 'kernel/apps/sacx_user_network.inc','kernel/include/apps/sacx_network_internal.h',
 'kernel/include/process/dihos_user_network.h','kernel/process/dihos_user_sacx.c',
 'sdk/sacx/include/sacx_user_window.h','sdk/sacx/include/sacx_user_http_request.h',
 'kernel/include/net/http_request_headers.h','tools/export_user_snapshot.py',
 'tools/embed_user_programs.py','tools/test_user_runtime_qemu.ps1','tools/user_runtime_qemu_tests.c',
 'tools/user_loader_tests.c','docs/dihscover-native-https.md','docs/dihscover-native-controls.md',
 'sdk/sacx/apps/dihscover/address.h','sdk/sacx/apps/dihscover/netsurf_backend.cpp',
 'sdk/sacx/include/sacx_keys.h','sdk/sacx/include/sacx_window_keys.h',
 'tools/browser_controls_tests.cpp','tools/user_window_desktop_tests.cpp','tools/user_window_tests.c','tools/test_browser_controls.ps1','tools/user_platform_program.cpp']:
    sources[name]=sha(ROOT/name)

# Real cryptographic verification is separate from the modeled guest transport.
trust=ROOT/'third_party/ca-certificates';lock=json.loads((trust/'roots.lock.json').read_text())
check(sha(trust/'cacert-2026-10-20.pem')==lock['sha256'],'Changed CA snapshot')
check(sha(ROOT/'kernel/net/tls_roots.inc')==lock['generated_sha256'],'Changed generated anchors')
crypto=ROOT/'build/tls-trust-tests';crypto_report=json.loads((crypto/'report.json').read_text())
check(crypto_report['certificates_verified'] and crypto_report['live_interoperability'],'Run real certificate and TLS checks')
for name,digest in crypto_report['sources'].items():check(sha(ROOT/name)==digest,'Changed crypto test input: '+name)
for host in ['example.com','www.netsurf-browser.org','en.wikipedia.org','github.com']:
    check(('PASS: '+host+' authenticated TLS') in (crypto/('live-'+host+'.log')).read_text(),'Missing live TLS check')
for host in ['expired.badssl.com','wrong.host.badssl.com','untrusted-root.badssl.com']:
    check(('PASS: '+host+' certificate rejected') in (crypto/('reject-'+host+'.log')).read_text(),'Missing certificate rejection')
evidence_dir=out/'tls-evidence';evidence_dir.mkdir(exist_ok=True)
for path in crypto.iterdir():
    if path.suffix in ['.log','.json']:shutil.copy2(path,evidence_dir/path.name)
for name in ['build/tls-transport-test.log','build/tls-trust-host.log']:
    shutil.copy2(ROOT/name,evidence_dir/Path(name).name)
check('requires root anchors' in (evidence_dir/'tls-transport-test.log').read_text(),'Transport gating check missing')
check('PASS: ARM64 transport compiles' in (evidence_dir/'tls-trust-host.log').read_text(),'Broker check missing')
for name in ['kernel/net/knet_usb.c','kernel/include/net/knet_usb.h','kernel/include/net/tls_verify.h',
 'kernel/net/tls_roots.inc','kernel/apps/sacx_runtime.cpp','kernel/include/system/entropy.h','kernel/system/entropy.c',
 'tools/fetch_tls_roots.py','tools/generate_tls_roots.py','tools/make_tls_test_fixtures.py',
 'tools/test_tls_trust.py','tools/tls_certificate_tests.c','tools/tls_live_probe.c',
 'tools/hyperv_tls_tests.c','tools/hyperv_dhcp_tests.c','tools/sacx_network_service_tests.cpp']:
    sources[name]=sha(ROOT/name)
for directory in [trust,ROOT/'tools/fixtures/tls']:
    for path in directory.iterdir():
        if path.is_file():sources[path.relative_to(ROOT).as_posix()]=sha(path)
check(b'DihOS TEST ONLY rsa-root' not in (out/'source/OS/aa64/KERNEL.ELF').read_bytes(),'Test CA leaked into production kernel')

check('PASS: browser controls and focused owned-window keyboard delivery' in (out/'host-controls.log').read_text(),'Run controls host checks')
licenses=out/'licenses';shutil.copytree(ROOT/'build/native-window-load-validation/licenses',licenses,dirs_exist_ok=True)
shutil.copy2(trust/'LICENSE-MPL-2.0.txt',licenses/'Mozilla-Roots-MPL-2.0.txt')
with zipfile.ZipFile(out/'KERNEL-NAVIGATION-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name in snapshot['files']:archive.write(out/'source'/name,name)
    archive.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
evidence={str(p.relative_to(ROOT)):sha(p) for p in [out/'qemu-final.log',out/'qemu-run.log',out/'qemu-clock-sampling-failure.log',out/'qemu-history-policy-failure.log',out/'host-loader.log',out/'host-controls.log',out/'legacy-compile.log',out/'kernel-build.log',out/'source.json',out/'KERNEL-NAVIGATION-SOURCES.zip']}
for directory in [licenses,out/'reports',evidence_dir,out/'host']:
    evidence.update({str(p.relative_to(ROOT)):sha(p) for p in directory.rglob('*') if p.is_file()})
proof={'date':'2026-11-02','scope':'Native ARM64 WebCore Dihscover controls and focused keyboard delivery',
 'qemu_validated':True,'qemu_transport_modeled':True,'qemu_groups':sum(s.startswith('PASS:') for s in log.splitlines()),
 'navigation_integrated':True,'desktop_adapter_built':True,'hyperv_guest_validated':False,
 'physical_hardware_validated':False,'shipping_browser_changed':False,'tls_peer_authentication_supported':True,
 'source_hashes':sources,'proof_files':evidence,'page_elf_sha256':page['elf_sha256'],
 'page_elf_bytes':page['elf_bytes'],'desktop_kernel_sha256':sha(out/'source/OS/aa64/KERNEL.ELF'),
 'roots':lock['certificates'],'roots_sha256':lock['sha256'],'tls_live_host_probes':True,'guest_tls_transport_modeled':True,
 'tests':['real RSA and ECDSA certificate verification','invalid certificate rejection','root/entropy/Finished gating','redirect through WebCore policy','external CSS','external JavaScript changes painted width',
          'typed address commits another document','DOM key event changes pixels','input value changes and preventDefault blocks insertion','Back and Forward history','clicked link commits another document','Stop cancels a stalled main request','Reload fetches again',
          'close while HTTP pending','all process/window/network pages reclaimed'],
 'limits':['TLS 1.2; invalid certificates rejected','GET only','64 KiB response bound','US-layout address/form editing; clipboard, IME and undo pending',
           'fixed viewport','bitmap codecs and media unavailable','cookies disabled; host-only site grouping',
           'full-frame upload and 250 ms diagnostic refresh','live Hyper-V test pending']}
destination.write_text(json.dumps(proof,indent=2)+'\n')
print('Recorded native controls:',proof['qemu_groups'],'guest groups; Hyper-V live test pending')
