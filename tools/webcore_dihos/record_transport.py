"""Seal transport regressions and the isolated desktop kernel without disk I/O."""
from pathlib import Path
import hashlib,json,zipfile,shutil
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dihscover-transport-validation'
DEST=ROOT/'docs/browser-evaluation/dihscover-transport-bursts-20261102.json'
PREVIOUS='docs/browser-evaluation/dihscover-response-deadline-20261102.json'
BASE=ROOT/'build/dihscover-response-deadline-20261102'
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
def check(ok,message):
 if not ok:raise SystemExit(message)
check(not DEST.exists(),'Keep previous proofs; choose a fresh milestone')
previous=json.loads((ROOT/PREVIOUS).read_text())
changed={'kernel/net/knet_usb.c','kernel/hyperv/hyperv_net.c','kernel/apps/sacx_runtime.cpp','tools/export_user_snapshot.py','tools/hyperv_protocol_tests.c','tools/hyperv_tls_tests.c'}
for path,digest in previous['source_hashes'].items():
 if path not in changed:check(sha(ROOT/path)==digest,'Inherited source changed: '+path)
for path,digest in previous['proof_files'].items():check(sha(ROOT/path)==digest,'Inherited evidence changed: '+path)
with zipfile.ZipFile(BASE/'OS/System/Programs/Dihscover/SOURCES.zip') as z:
 old=z.read('kernel/apps/sacx_runtime.cpp').decode().replace('\r\n','\n')
 before='slot->stream.failed ? slot->stream.error : rc == -4 ? "network request timed out" : rc == -2 ? "network transport busy" : knet_usb_last_fetch_error()[0] ? knet_usb_last_fetch_error() : "network request failed"'
 after='slot->stream.failed ? slot->stream.error : knet_usb_last_fetch_error()[0] ? knet_usb_last_fetch_error() : rc == -4 ? "network request timed out" : rc == -2 ? "network transport busy" : "network request failed"'
 check(old.count(before)==1 and old.replace(before,after)==(ROOT/'kernel/apps/sacx_runtime.cpp').read_text(),'Unexpected mixed runtime changes')
required={'hyperv-tests.log':['PASS: NetVSC receives 64-frame bursts in order','PASS: deployment script syntax'],
 'tls-tests.log':['PASS: HTTP overlap recovery ignores a future FIN','PASS: transport requires root anchors'],
 'tcp-receive.log':['PASS: contiguous TCP suffix'],
 'http-tests.log':['PASS: ARM64 transport compiles'],
 'certificates.log':['PASS: real BearSSL RSA/intermediate and ECDSA chains'],
 'burst-before.log':['Assertion failed: count==64 && full_rx==drops'],
 'kernel-build.log':['Built:','Copy to USB:']}
for name,markers in required.items():
 log=(OUT/name).read_text()
 for marker in markers:check(marker in log,'Missing regression: '+name+': '+marker)
 if name!='burst-before.log':check('Assertion failed:' not in log and 'FAIL:' not in log,'Regression failed: '+name)
snapshot=json.loads((OUT/'source.json').read_text())
for path,digest in snapshot['files'].items():check(sha(OUT/'source'/path)==digest,'Snapshot source changed: '+path)
check('kernel/include/net/tcp_receive.h' in snapshot['files'],'TCP helper missing from isolated snapshot')
kernel=OUT/'source/OS/aa64/KERNEL.ELF';check(kernel.is_file(),'Build isolated kernel first')
check(sha(ROOT/'OS/aa64/KERNEL.ELF')=='5ad373f3bab1d2346bdbf3ac096c128012be3842cd25fc1305bbc109ff01fc9d','Working kernel changed')
for path,digest in [('build/sacx/Dihscover.sacx',previous['frontend_sha256']),('build/sacx/Dihscover-WebCore.bin',previous['renderer_sha256'])]:check(sha(ROOT/path)==digest,'Browser artifact changed')
sources={path:sha(ROOT/path) for path in previous['source_hashes']}
for path,digest in snapshot['files'].items():
 if (ROOT/path).is_file() and sha(ROOT/path)==digest:sources[path]=digest
for path in changed|{'kernel/include/net/tcp_receive.h','tools/tcp_receive_tests.c','tools/test_hyperv.ps1','docs/dihscover-transport-bursts.md','tools/webcore_dihos/record_transport.py','tools/webcore_dihos/package_transport.py'}:sources[path]=sha(ROOT/path)
with zipfile.ZipFile(OUT/'KERNEL-SOURCES.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for path in snapshot['files']:archive.write(OUT/'source'/path,path)
 archive.writestr('SOURCE-MANIFEST.json',json.dumps(snapshot,indent=2)+'\n')
shutil.copytree(ROOT/'build/dihscover-response-validation/licenses',OUT/'licenses',dirs_exist_ok=True)
evidence={}
for path in OUT.rglob('*'):
 if path.is_file() and ('source' not in path.relative_to(OUT).parts) and path.suffix in ['.log','.json','.py','.zip','.txt','.LICENSE']:
  evidence[path.relative_to(ROOT).as_posix()]=sha(path)
# Inherit the exact sealed renderer/host evidence; no new guest pass is inferred.
evidence.update(previous['proof_files'])
proof={'date':'2026-11-02','scope':'Bounded NetVSC bursts, TCP overlap/FIN recovery and preserved kernel timeout diagnostics',
 'frontend_sha256':previous['frontend_sha256'],'renderer_sha256':previous['renderer_sha256'],'kernel_sha256':sha(kernel),
 'existing_frontend':previous['existing_frontend'],'normal_application':previous['normal_application'],'renderer':previous['renderer'],
 'qemu_validated':True,'qemu_groups':previous['qemu_groups'],'qemu_network_modeled':True,'qemu_inherited_from':PREVIOUS,
 'host_existing_ui_validated':True,'manual_helper_launch_required':False,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'upload_max_bytes':65536,'page_surface_max':[1920,1080],'tcp_receive_window_bytes':16384,'netvsc_frame_slots':128,
 'real_certificate_rejections_validated':True,'transport_tests_use_mock_tls':True,'old_queue_loss_reproduced':True,
 'source_hashes':sources,'proof_files':evidence,
 'limits':['Live Google Hyper-V retest required','No new out-of-order reassembly or transmit retry engine','School CA remains untrusted','Existing browser GET/cookie/media limitations remain']}
DEST.write_text(json.dumps(proof,indent=2)+'\n');print('Recorded transport proof; isolated kernel',proof['kernel_sha256'])
