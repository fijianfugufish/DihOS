"""Record exact-binary native event-loop execution, without claiming WebCore DOM."""
from pathlib import Path
import hashlib,json,re,struct
ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report_path=ROOT/'build/user-jsc-native/eventloop-report.json'
report=json.loads(report_path.read_text());key=Path(report['output']).name
if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Invalid artifact directory')
elf=ROOT/f'build/user-jsc-native/{key}/jsc.elf'
if not report['linked'] or not report['loader_accepted'] or sha(elf)!=report['elf_sha256']:raise SystemExit('Changed/unvalidated ELF')
sources=dict(report['event_loop_sources'])
sources['tools/webcore_dihos/eventloop_fixture.cpp']=report['fixture_sha256']
sources['tools/build_jsc_native_test.py']=report['builder_sha256']
for relative,digest in sources.items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Test source changed: '+relative)
for relative in ['tools/user_runtime_qemu_tests.c','tools/test_user_runtime_qemu.ps1','tools/embed_user_programs.py']:
    sources[relative]=sha(ROOT/relative)
log_path=ROOT/'build/user-runtime/qemu.log';log=log_path.read_text(encoding='utf-8-sig')
groups=[line for line in log.splitlines() if line.startswith('PASS:')]
checks=['bounded FIFO, cancellation and turn fairness','cross-thread delivery, wakeups and capture disposal',
        'one-shot, repeating, stopped timers and idle waits',
        'native timer to protected JavaScript callback and promise checkpoint']
if len(groups)!=40 or any(word in log for word in ['FAIL:','FATAL','JSC abort']):raise SystemExit('Full 40-group run required')
if not any(line.startswith('PASS: native WebKit RunLoop, bounded dispatch') for line in groups):raise SystemExit('No native event-loop pass')
for check in checks:
    if 'WebCore loop: '+check+' OK' not in log:raise SystemExit('Missing fixture check: '+check)
kernel_path=ROOT/'build/user-runtime/user-runtime.elf';kernel=kernel_path.read_bytes()
h=struct.unpack_from('<16sHHIQQQIHHHHHH',kernel)
if h[0][:6]!=b'\x7fELF\x02\x01' or h[2]!=183:raise SystemExit('Native ARM64 ELF64 required')
sections=[struct.unpack_from('<IIQQQQIIQQ',kernel,h[6]+i*h[11]) for i in range(h[12])]
symbols={}
for section in sections:
    if section[1]!=2:continue
    strings=sections[section[6]];names=kernel[strings[4]:strings[4]+strings[5]]
    for offset in range(section[4],section[4]+section[5],section[9]):
        symbol=struct.unpack_from('<IBBHQQ',kernel,offset)
        symbols[names[symbol[0]:].split(b'\0',1)[0].decode(errors='replace')]=symbol
embedded={}
programs={'webcore_loop':elf}
for profile,symbol_name in [('report.json','jsc'),('spin-report.json','jsc_spin')]:
    baseline=json.loads((ROOT/'build/user-jsc-native'/profile).read_text())
    programs[symbol_name]=ROOT/'build/user-jsc-native'/Path(baseline['output']).name/'jsc.elf'
for name,path in programs.items():
    symbol=symbols.get('user_'+name+'_elf');data=path.read_bytes();actual=None
    if symbol:
        for i in range(h[10]):
            p=struct.unpack_from('<IIQQQQQQ',kernel,h[5]+i*h[9])
            if p[0]==1 and symbol[4]>=p[3] and symbol[4]-p[3]+len(data)<=p[5]:
                at=p[2]+symbol[4]-p[3];actual=kernel[at:at+len(data)]
    if actual!=data:raise SystemExit('Embedded program differs: '+name)
    embedded[name]={'sha256':sha(path),'bytes':len(data)}
proof={'date':'2026-10-31','scope':'Native generic WTF RunLoop and bounded DihOS dispatch queue',
       'qemu_validated':True,'hyperv_validated':False,'physical_hardware_validated':False,
       'webcore_dom_compiled':False,'websites_validated':False,'browser_integrated':False,
       'host_engine_reused':False,'kernel_changed':False,'exit_status':103,
       'capacity':32,'callbacks_per_turn':8,'queue_cancellation':'Pending work only; executing callbacks are not interrupted.',
       'bounded_execution':'Queue bounds work items per turn; individual callbacks require kernel preemption. No measured UI-latency guarantee.',
       'runloop':'Actual pinned generic WTF RunLoop over native SACX thread/condition services',
       'execution_frame_ms':16,'checks':checks,'pass_groups':groups,'embedded_programs':embedded,
       'build_report_sha256':sha(report_path),'kernel_sha256':sha(kernel_path),'qemu_log_sha256':sha(log_path),
       'source_hashes':sources,'libraries':report['libraries'],'abi_profile':report['event_loop_abi'],
       'platform_audit_sha256':sha(ROOT/'build/webcore-dihos/platform-audit.json')}
out=ROOT/'docs/browser-evaluation/native-webcore-eventloop-20261031.json'
out.write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({'proof':str(out),'groups':len(groups),'exit_status':103,'embedded_elf_verified':True}))
