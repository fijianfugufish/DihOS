"""Record a tested development checkpoint, explicitly not CAPTCHA acceptance."""
from pathlib import Path
import json,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/dihscover-captcha-validation'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
qemu=json.loads((OUT/'qemu-page/report.json').read_text());renderer=json.loads((OUT/'renderer/report.json').read_text())
assert qemu['passed'] and renderer['linked'] and renderer['loader_accepted']
assert renderer['elf_sha256']==sha(OUT/'renderer/page.elf')==qemu['image_test_sha256']
assert renderer['fixture_elf_sha256']==sha(OUT/'renderer/resource.elf')==qemu['fixture_sha256']
assert qemu['kernel_sha256']==sha(OUT/'qemu-page/kernel.elf')
for p,d in qemu['inputs'].items():assert sha(ROOT/p)==d,p
log=(OUT/'qemu-page/run.log').read_text()
for marker in ['WebCore POST: owned binary body','PASS: iframe JavaScript performs asynchronous POST','PASS: native iframes paint','PASS: twenty plain pointer movements','PASS: sparse quota/slot bounds']:assert marker in log,marker
assert 'FAIL:' not in log and 'FATAL' not in log
assert 'PASS: Dihscover existing frontend' in (OUT/'ui-tests.log').read_text()
assert 'PASS: appended GET/POST descriptor' in (ROOT/'build/dihscover-frames-validation/http-tests.log').read_text()
snapshot=json.loads((ROOT/'build/capt/source.json').read_text())
for p,d in snapshot['files'].items():assert sha(ROOT/'build/capt/src'/p)==d,p
assert 'Copy to USB:' in (OUT/'kernel-build.log').read_text()
assert sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d'
names=['sdk/sacx/include/sacx_user_http_request.h','kernel/apps/sacx_user_network.inc','kernel/process/dihos_user_sacx.c','kernel/net/knet_usb.c','tools/webcore_dihos/navigation_client.h','tools/webcore_dihos/ResourceLoad.h','tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/platform/WebCoreResourceLoad.cpp','tools/webcore_dihos/platform/ResourceHandleDihOS.cpp','tools/user_sacx_tests.c','tools/sacx_network_service_tests.cpp','tools/user_runtime_qemu_tests.c','tools/webcore_dihos/webcore_resource_fixture.cpp','docs/dihscover-captcha-compatibility.md']
names += [str(p.relative_to(ROOT)) for p in (ROOT/'tools/webcore_dihos').glob('*captcha*.py')]
names += ['tools/webcore_dihos/build_frames.py','tools/webcore_dihos/finish_frames.py','tools/webcore_dihos/test_frames.py']
proof={'date':'2026-11-03','scope':'Native iframe and memory-only POST compatibility foundations','google_captcha_supported':False,'cookies_implemented':False,'google_challenge_tested':False,'shipped':False,'vhd_updated':False,'hyperv_guest_validated':False,'qemu_network_modeled':True,'arm64_tests_passed':True,'renderer_sha256':renderer['elf_sha256'],'kernel_sha256':sha(ROOT/'build/capt/src/OS/aa64/KERNEL.ELF'),'qemu_groups':qemu['groups'],'source_hashes':{p:sha(ROOT/p) for p in names},'evidence_hashes':{str(p.relative_to(ROOT)):sha(p) for p in [OUT/'qemu-page/report.json',OUT/'qemu-page/run.log',OUT/'renderer/report.json',OUT/'ui-tests.log',OUT/'kernel-build.log',ROOT/'build/capt/source.json',ROOT/'build/dihscover-frames-validation/http-tests.log']},'remaining':['Bounded session-cookie storage with credential/origin/Secure/HttpOnly/SameSite/PSL checks','Package the complete candidate with its matched kernel','Human completion of a real Google challenge; investigate additional missing APIs if encountered']}
dest=ROOT/'docs/browser-evaluation/dihscover-captcha-foundations-20261103.json';assert not dest.exists()
dest.write_text(json.dumps(proof,indent=2)+'\n')
print('PASS: ARM64 iframe/POST checkpoint, matched isolated kernel, source/evidence hashes; Google CAPTCHA remains unverified and unshipped')
