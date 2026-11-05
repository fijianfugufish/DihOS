"""Record the checked EL0 HTTP boundary; modeled transport is not a live NIC test."""
from pathlib import Path
import hashlib,json,struct,subprocess
ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
validation=ROOT/'build/user-validation-http-20261031';project=validation/'source'
log_path=ROOT/'build/user-runtime/qemu.log';log=log_path.read_text(encoding='utf-8-sig')
groups=[line for line in log.splitlines() if line.startswith('PASS:')]
if len(groups)!=41 or 'modeled transport' not in log or any(x in log for x in ['FAIL:','FATAL','JSC abort']):raise SystemExit('Full 41-group run required')
kernel=(ROOT/'build/user-runtime/user-runtime.elf').read_bytes();h=struct.unpack_from('<16sHHIQQQIHHHHHH',kernel)
sections=[struct.unpack_from('<IIQQQQIIQQ',kernel,h[6]+i*h[11]) for i in range(h[12])];symbol=None
for section in sections:
    if section[1]!=2:continue
    strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
    for offset in range(section[4],section[4]+section[5],section[9]):
        entry=struct.unpack_from('<IBBHQQ',kernel,offset)
        if names[entry[0]:].split(b'\0',1)[0]==b'user_http_elf':symbol=entry
elf=ROOT/'build/user-http/http.elf';data=elf.read_bytes();actual=None
if symbol:
    for i in range(h[10]):
        p=struct.unpack_from('<IIQQQQQQ',kernel,h[5]+i*h[9])
        if p[0]==1 and symbol[4]>=p[3] and symbol[4]-p[3]+len(data)<=p[5]:
            at=p[2]+symbol[4]-p[3];actual=kernel[at:at+len(data)]
if actual!=data:raise SystemExit('HTTP fixture differs from embedded tested ELF')
sources=['sdk/sacx/include/sacx_user_network.h','kernel/include/process/dihos_user_network.h',
 'kernel/include/process/dihos_user_sacx.h','kernel/process/dihos_user_sacx.c',
 'kernel/include/process/dihos_process.h','kernel/process/dihos_user_runtime.c',
 'kernel/process/dihos_user_image.inc','kernel/process/dihos_user_loader.c',
 'kernel/include/process/dihos_user_loader.h','kernel/include/process/dihos_user_runtime.h',
 'kernel/apps/sacx_runtime.cpp','kernel/apps/sacx_user_network.inc','kernel/apps/sacx_network_service.inc',
 'kernel/shell/dihos_user_commands.inc','tools/webcore_dihos/http_fixture.c',
 'tools/webcore_dihos/build_http.ps1','tools/user_sacx_tests.c','tools/sacx_network_service_tests.cpp',
 'tools/user_runtime_qemu_tests.c','tools/test_user_runtime_qemu.ps1','tools/embed_user_programs.py']
overlay=json.loads((validation/'source.json').read_text())
for relative,digest in overlay['files'].items():
    if sha(project/relative)!=digest:raise SystemExit('Isolated kernel source changed: '+relative)
    if relative in sources and sha(ROOT/relative)!=digest:raise SystemExit('Kernel source differs from tested boundary: '+relative)
programs={}
for name in ['http','https']:
    path=ROOT/f'build/user-http/{name}.elf'
    result=json.loads(subprocess.check_output([str(ROOT/'build/user-http/inspect.exe'),str(path)],text=True))
    if result['result']:raise SystemExit('Production ELF validator rejected HTTP fixture')
    programs[name]={'sha256':sha(path),'bytes':path.stat().st_size,'loader':result,'qemu_executed':name=='http'}
host=subprocess.check_output([str(ROOT/'build/browser-tests/network.exe')],text=True)
if 'native owned URL' not in host:raise SystemExit('Native broker host tests absent')
proof={'date':'2026-10-31','scope':'Checked SACX HTTP GET boundary and shared core-0 continuation',
 'qemu_validated':True,'hyperv_guest_validated':False,'physical_hardware_validated':False,
 'transport_modeled_in_qemu':True,'live_dns_tcp_tls_validated':False,'tls_peer_authentication':False,
 'browser_integrated':False,'webcore_dom_compiled':False,'abi_version':1,'operations':[27,28,29,30],
 'network_grant_bit':9,'default_elf_launch_grants_network':False,'grant_command':'proc:run-net',
 'maximum_global_requests':16,'maximum_process_requests':4,'response_capacity_bytes':65536,
 'read_limit_bytes':4096,'url_limit_bytes':767,'response_policy':'Fixed cumulative body cap; partial reads do not recycle capacity. Truncation is explicit failure, not successful EOF.',
 'memory_budget':'Separate bounded broker pool: at most 1 MiB payload globally / 256 KiB per process, plus fixed metadata; not charged to process heap.',
 'success_exit_status':104,'denied_exit_status':105,'programs':programs,'embedded_http_elf_verified':True,
 'qemu_kernel_sha256':sha(ROOT/'build/user-runtime/user-runtime.elf'),'qemu_log_sha256':sha(log_path),
 'production_kernel_sha256':sha(project/'OS/aa64/KERNEL.ELF'),'isolated_source_overlay_sha256':overlay['overlay_sha256'],
 'host_network_test':host.splitlines(),'source_hashes':{p:sha(ROOT/p) for p in sources},'pass_groups':groups,
 'harness_correction':'The inherited-EL0-map rejection test now alters an unused leaf rather than the executing kernel/vector leaf; captured ESR 0x8600000e confirmed the earlier harness instruction-permission fault.'}
out=ROOT/'docs/browser-evaluation/native-user-http-20261031.json';out.write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({'proof':str(out),'qemu_groups':len(groups),'live_network_validated':False}))
