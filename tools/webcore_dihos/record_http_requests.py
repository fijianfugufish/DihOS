"""Seal request-boundary evidence without claiming live Page navigation."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--validation',default='build/native-http-request-validation')
args=parser.parse_args();out=ROOT/args.validation
destination=ROOT/'docs/browser-evaluation/native-http-requests-20261101.json'
if destination.exists():raise SystemExit('Retain existing proof; use a new milestone name')
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def check(ok,message):
    if not ok:raise SystemExit(message)
qemu=ROOT/'build/user-runtime/qemu.log';log=qemu.read_text()
for marker in ['WebCore request headers: owned fields, framing rejection and response deferral OK',
               'PASS: actual WebCore request/response/error metadata',
               'PASS: native WebCore window syscall frames',
               'PASS: sparse quota/slot bounds']:
    check(marker in log,'Missing guest check: '+marker)
check('FAIL:' not in log and 'FATAL' not in log,'Guest run failed')
host=out/'host.log'
check('PASS: ARM64 transport compiles' in host.read_text(),'Host checks incomplete')
check('PASS: bounded request fields' in (out/'sanitizer.log').read_text(),'Sanitizers incomplete')
snapshot=json.loads((out/'source.json').read_text())
for name,digest in snapshot['files'].items():
    check(sha(out/'source'/name)==digest,'Changed isolated source: '+name)
for name in ['build/BOOTAA64.EFI','build/STAGE2.EFI','OS/aa64/KERNEL.ELF']:
    check((out/'source'/name).is_file(),'Missing isolated kernel build: '+name)
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working-tree kernel changed')
sources={}
names=['sdk/sacx/include/sacx_user_http_request.h','kernel/include/net/http_request_headers.h',
 'kernel/include/apps/sacx_network_internal.h','kernel/include/net/knet_usb.h','kernel/net/knet_usb.c',
 'kernel/include/process/dihos_user_network.h','kernel/process/dihos_user_sacx.c','kernel/apps/sacx_user_network.inc',
 'kernel/apps/sacx_runtime.cpp','tools/export_user_snapshot.py','tools/test_user_http_headers.ps1',
 'tools/http_request_headers_tests.c','tools/sacx_network_service_tests.cpp','tools/user_sacx_tests.c',
 'tools/user_runtime_qemu_tests.c','tools/test_user_runtime_qemu.ps1','tools/embed_user_programs.py',
 'tools/build_jsc_native_test.py','tools/webcore_dihos/ResourceLoad.h','tools/webcore_dihos/ResourceLoad.cpp',
 'tools/webcore_dihos/resource_fixture.cpp','tools/webcore_dihos/platform/WebCoreResourceLoad.h',
 'tools/webcore_dihos/platform/WebCoreResourceLoad.cpp','tools/webcore_dihos/webcore_resource_fixture.cpp',
 'tools/webcore_dihos/http_fixture.c','tools/webcore_dihos/build_http.ps1','tools/webcore_dihos/http_fixture.ld',
 'tools/webcore_dihos/configure_native.py','tools/webcore_dihos/build_resource_bridge.py',
 'tools/webcore_dihos/record_http_requests.py','docs/dihscover-native-http-requests.md']
for name in names:sources[name]=sha(ROOT/name)
reports=['build/user-jsc-native/resource-report.json','build/webcore-resource-bridge/report.json',
 'build/webcore-native/report.json','build/webcore-native/units/report.json',
 'build/webcore-engine-openssl/report.json','build/webcore-engine-openssl/compile-report.json',
 'build/webcore-headless-page/report.json','build/webcore-headless-page/compile-report.json',
 'build/user-runtime/base-programs.json']
resource=json.loads((ROOT/reports[1]).read_text())
check(resource.get('linked') and resource.get('loader_accepted'),'Resource ELF incomplete')
check(resource['fixture_sha256']==sha(ROOT/'tools/webcore_dihos/webcore_resource_fixture.cpp'),'Stale resource fixture')
check(resource['configure_report_sha256']==sha(ROOT/'build/webcore-native/report.json'),'Stale resource configuration')
check(resource['base_report_sha256']==sha(ROOT/'build/user-jsc-native/resource-report.json'),'Stale resource base')
for name,digest in resource['inputs'].items():check(sha(ROOT/name)==digest,'Stale resource input: '+name)
shutil.copy2(qemu,out/'qemu-final.log')
for relative in reports:
    target=out/'reports'/Path(relative).parent.name/Path(relative).name
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/relative,target)
evidence={str(p.relative_to(ROOT)):sha(p) for p in [out/'qemu-final.log',out/'qemu-run.log',host,out/'sanitizer.log',out/'build.log',out/'source.json']}
evidence.update({str(p.relative_to(ROOT)):sha(p) for p in (out/'reports').rglob('*.json')})
bundle=out/'sources.zip'
with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as archive:
    for name in sources:archive.write(ROOT/name,name)
    archive.writestr('SOURCE-HASHES.json',json.dumps(sources,indent=2)+'\n')
evidence[str(bundle.relative_to(ROOT))]=sha(bundle)
proof={'date':'2026-11-01','scope':'Native WebCore request fields, checked SACX GET descriptor and response deferral',
 'qemu_validated':True,'qemu_transport_modeled':True,'qemu_groups':sum(line.startswith('PASS:') for line in log.splitlines()),
 'hyperv_new_transport_validated':False,'live_page_navigation_integrated':False,'shipping_browser_changed':False,
 'tls_peer_authentication_supported':False,'default_rejects_unverified_tls':True,'request_headers_max':2048,
 'response_body_limit':65536,'syscall':38,'source_hashes':sources,'evidence_hashes':evidence,
 'isolated_kernel_sha256':sha(out/'source/OS/aa64/KERNEL.ELF'),
 'remaining':['native Frame/ResourceHandle integration','authenticated HTTPS','larger streaming responses','live site validation']}
destination.write_text(json.dumps(proof,indent=2)+'\n')
print('Recorded native request boundary:',proof['qemu_groups'],'guest test groups; no live Page navigation claim')
