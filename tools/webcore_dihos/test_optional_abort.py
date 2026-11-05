"""Exercise the actual hardened optional handler in ARM64 EL0, not just a mock."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--indexeddb-control',action='store_true');p.add_argument('--vm-control',action='store_true');p.add_argument('--old-tables',action='store_true');args=p.parse_args()
BASE=(ROOT/args.output).resolve();OUT=BASE/('vm-tables-control' if args.old_tables else 'vm-error-test' if args.vm_control else 'indexeddb-control' if args.indexeddb_control else 'abort-test');OUT.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
renderer=json.loads((BASE/'renderer/report.json').read_text());steps=[]
def run(label,command):
    with (OUT/(label+'.log')).open('w') as log:r=subprocess.run(list(map(str,command)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    steps.append({'label':label,'argv':list(map(str,command)),'exit_code':r.returncode})
    assert not r.returncode,label
run('host-compile',['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','tools/user_abort_trace_tests.cpp','-o',OUT/'host-test'])
run('host-run',[OUT/'host-test'])
if args.vm_control:
    run('vm-host-compile',['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','tools/vm_failure_report_tests.cpp','-o',OUT/'vm-host-test'])
    run('vm-host-run',[OUT/'vm-host-test'])
command=list(next(s['argv'] for s in renderer['steps'] if s['label']==('compile-video-page' if args.indexeddb_control else 'compile-headless')))
if args.indexeddb_control:command.remove('-DDIHOS_INDEXEDDB_PROVIDER=1')
clean=[];i=0
while i<len(command):
    if command[i] in ['-MF','-MT','-MQ']:i+=2;continue
    if command[i] in ['-MD','-MMD','-MP']:i+=1;continue
    clean.append(command[i]);i+=1
command=clean;command[1:1]=['-fno-omit-frame-pointer']
if not args.indexeddb_control:command[command.index('-c')+1]=str(ROOT/('tools/webcore_dihos/vm_error_fixture.cpp' if args.vm_control else 'tools/webcore_dihos/optional_abort_fixture.cpp'))
command[command.index('-o')+1]=str(OUT/'optional.o');run('compile',command)
link=list(next(s['argv'] for s in renderer['steps'] if s['label']==('link-video-page' if args.indexeddb_control else 'link-resource')))
suffix='/video-page-headless.o' if args.indexeddb_control else '/resource-fixture.o'
link=[str(OUT/'optional.o') if a.endswith(suffix) else a for a in link]
link[link.index('-o')+1]=str(OUT/'optional.elf');run('link',link)
# The full WebCore control contains debug/symbol sections beyond the loader's
# 128 MiB file ceiling. Preserve them for addr2line, load only the stripped ELF.
import shutil
shutil.copy2(OUT/'optional.elf',OUT/'optional.symbols.elf')
run('strip',['aarch64-linux-gnu-strip','--strip-all',OUT/'optional.elf'])
# Reuse the real kernel test machinery in a separate output; do not mutate old fixtures.
test=OUT/'native';(test/'renderer').mkdir(parents=True,exist_ok=True)
run('prepare-inputs',['python3',ROOT/'tools/webcore_dihos/prepare_large_test_inputs.py','--output',test])
test_report={**renderer,'video_page_sha256':sha(OUT/'optional.elf')}
shutil.copy2(OUT/'optional.elf',test/'renderer/video-page.elf')
(test/'renderer/report.json').write_text(json.dumps(test_report))
harness=(ROOT/'tools/user_runtime_qemu_tests.c').read_text()
assert harness.count('infos[0].exit_status!=124')==1
assert harness.count('infos[0].exit_status==124')==1
expected_exit=175 if args.vm_control else 151
harness=harness.replace('infos[0].exit_status!=124','infos[0].exit_status!='+str(expected_exit)).replace('infos[0].exit_status==124','infos[0].exit_status=='+str(expected_exit))
marker='        print("PASS: native WebCore HTML video, MP4/H.264 pixels, seek, timed muted playback, ended events and process cleanup\\n");'
assert marker in harness
harness=harness.replace(marker,'        require(text_contains(infos[0].last_log,"optional operator*") && text_contains(infos[0].last_log,"callers:"),"optional assertion and caller trace retained together");\n        print("Optional code base: ");hex(infos[0].code_va);print("\\n");\n        print("PASS: hardened optional caller diagnostics and process cleanup\\n");')
if args.vm_control:
    harness=harness.replace('text_contains(infos[0].last_log,"optional operator*") && text_contains(infos[0].last_log,"callers:")',
                            'text_contains(infos[0].last_log,"WTF VM decommit rc=-14") && text_contains(infos[0].last_log,"bytes=0x0000000000001000") && text_contains(infos[0].last_log,"limit=0x") && !text_contains(infos[0].last_log,"limit=0x0000000000000000")')
harness_path=test/'abort-harness.c';harness_path.write_text(harness)
template=(ROOT/'tools/webcore_dihos/media/test_page.py').read_text()
template=template.replace('ROOT=Path(__file__).resolve().parents[3]','ROOT=Path('+repr(str(ROOT))+')')
template=template.replace("BASE=ROOT/'build/dihscover-video-player'",'BASE=Path('+repr(str(test))+')')
template=template.replace("'tools/user_runtime_qemu_tests.c'",repr(harness_path.relative_to(ROOT).as_posix()))
template=template.replace('PASS: native WebCore HTML video','PASS: hardened optional caller diagnostics')
if args.old_tables:
    assert args.vm_control
    template=template.replace("'-DUSER_TEST_ARENA_PAGES=131072'","'-DUSER_TEST_ARENA_PAGES=131072','-DAARCH64_USER_VM_MAX_TABLES=192','-DUSER_EXPECT_OLD_TABLE_LIMIT=1'")
runner=test/'run.py';runner.write_text(template)
run('native',['python3',runner])
log=(test/'qemu/run.log').read_text()
trace=re.search(r'caller=0x([0-9a-f]{16})' if args.vm_control else r'callers: ([0-9a-f]{16})',log);base=re.search(r'Optional code base: \s*0x([0-9A-Fa-f]+)',log)
assert trace and base,log[-2000:]
offset=int(trace[1],16)-int(base[1],16)-4
run('symbolize',['aarch64-linux-gnu-addr2line','-Cfe',OUT/'optional.symbols.elf',hex(offset)])
expected='dihos_user_main' if args.vm_control else 'IDBConnectionProxy::IDBConnectionProxy' if args.indexeddb_control else 'checked_optional'
assert expected in (OUT/'symbolize.log').read_text(),(OUT/'symbolize.log').read_text()
files=['sdk/sacx/user_runtime/libcxx/platform.cpp','sdk/sacx/user_runtime/include/dihos_user_abort_trace.h','tools/user_abort_trace_tests.cpp','tools/webcore_dihos/optional_abort_fixture.cpp','tools/webcore_dihos/test_optional_abort.py']
files+=['tools/webcore_dihos/headless_page_fixture.cpp','tools/webcore_dihos/media/page_mse_fixture.h','tools/webcore_dihos/media/make_mse_fixture.py']
if args.vm_control:files+=['tools/webcore_dihos/vm_error_fixture.cpp','tools/vm_failure_report_tests.cpp','tools/jsc_dihos/OSAllocatorDihOS.cpp','tools/jsc_dihos/vm_failure_report.h']
if args.old_tables:assert 'PASS: 192-table control exhausts metadata before its byte quota' in log
(OUT/'report.json').write_text(json.dumps({'passed':True,'real_arm64_el0':True,'hardening_enabled':True,'indexeddb_empty_provider_control':args.indexeddb_control,'vm_error_control':args.vm_control,'old_table_control':args.old_tables,'expected_function':expected,'inputs':{n:sha(ROOT/n) for n in files},'steps':steps,'log_sha256':sha(test/'qemu/run.log'),'symbolization':(OUT/'symbolize.log').read_text()},indent=2)+'\n')
print('PASS: optional assertion preserved, bounded callers map to actual failing function; all process pages released')
