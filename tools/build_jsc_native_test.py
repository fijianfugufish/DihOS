"""Compile and link the actual target C API fixture; retain real link failures.

Run in WSL after jsc_dihos_probe.py --build-target JavaScriptCore succeeds.
This creates no guest package, runs no target code and never substitutes a host
engine or Linux libraries. The production ELF validator checks loader fitness.
"""
from pathlib import Path
import argparse,hashlib,json,platform,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--report',type=Path,default=ROOT/'build/jsc-dihos/report.json')
parser.add_argument('--icu-report',type=Path,default=ROOT/'build/icu-dihos/stage-report.json')
parser.add_argument('--libm-report',type=Path)
parser.add_argument('--support-report',type=Path)
parser.add_argument('--stdio-report',type=Path)
parser.add_argument('--spin',action='store_true',help='build the API-free infinite-JavaScript cancellation fixture')
parser.add_argument('--event-loop',action='store_true',help='build native WebCore event-loop foundation fixture')
parser.add_argument('--resource-loop',action='store_true',help='build native event-loop HTTP resource client fixture')
args=parser.parse_args()
if args.resource_loop:args.event_loop=True
if args.spin and args.event_loop:parser.error('--spin and --event-loop are separate profiles')
if platform.system()!='Linux':raise SystemExit('Run in WSL; the executable target stays aarch64-none-elf')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
r=json.loads(args.report.read_text())
if r['target']!='aarch64-none-elf' or r['host_engine_reused'] or not r.get('engine_library_built'):
    raise SystemExit('The report must prove a successful native JavaScriptCore library build')
if sha(ROOT/'tools/jsc_dihos/port-edits.json')!=r['port_edits_sha256']:
    raise SystemExit('Pinned source edits changed; rebuild before linking')
inputs=dict(r['sdk_inputs'])
for group in ['native_vm_sources','native_random_sources','native_thread_sources','native_file_sources','native_platform_sources']:
    inputs.update(r[group])
for relative,digest in inputs.items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native input: '+relative)
icu=json.loads(args.icu_report.read_text())
if sha(args.icu_report)!=r['icu']['stage_report_sha256']:
    raise SystemExit('Changed ICU stage report')
libraries={Path(r[key]['path']):r[key]['sha256'] for key in ['runtime_archive','support_library','engine_library','bytecode_library','allocator_library']}
libraries.update({Path(path):digest for path,digest in r['extra_cxx_libraries'].items()})
for relative,digest in icu['files'].items():
    if relative.endswith('.a'):libraries[Path(icu['sysroot'])/relative]=digest
libm=None
if args.libm_report:
    libm=json.loads(args.libm_report.read_text())
    if not libm.get('compiled') or libm.get('target')!='aarch64-none-elf':
        raise SystemExit('Math dependency must be a successful native target archive build')
    if libm['builder_sha256']!=sha(ROOT/'tools/build_user_libm.py'):
        raise SystemExit('Math builder changed; rebuild before linking')
    if libm['archive_sha256']!=sha(ROOT/'build/musl-1.2.5.tar.gz'):
        raise SystemExit('Math source archive changed')
    if libm.get('c_support') and libm['native_c_support_sha256']!=sha(ROOT/'tools/jsc_dihos/CLibraryDihOS.cpp'):
        raise SystemExit('Native C support changed; rebuild its library')
    libraries[Path(libm['library']['path'])]=libm['library']['sha256']
support=None
if args.support_report:
    support=json.loads(args.support_report.read_text())
    if not support.get('compiled') or support.get('target')!='aarch64-none-elf':
        raise SystemExit('Additional engine support must be a successful native target archive build')
    if support['builder_sha256']!=sha(ROOT/'tools/build_user_jsc_support.py'):
        raise SystemExit('Additional engine support builder changed')
    for relative,digest in support['sdk_inputs'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Changed support SDK input: '+relative)
    libraries[Path(support['library']['path'])]=support['library']['sha256']
stdio=None
if args.stdio_report:
    stdio=json.loads(args.stdio_report.read_text())
    if not stdio.get('compiled') or stdio.get('target')!='aarch64-none-elf':
        raise SystemExit('Stdio dependency must be a successful native target archive build')
    if stdio['builder_sha256']!=sha(ROOT/'tools/build_user_stdio.py') or stdio['backend_sha256']!=sha(ROOT/'tools/jsc_dihos/StdioDihOS.cpp'):
        raise SystemExit('Native stdio source changed; rebuild its archive')
    if stdio['archive_sha256']!=sha(ROOT/'build/musl-1.2.5.tar.gz'):raise SystemExit('Changed stdio source archive')
    libraries[Path(stdio['library']['path'])]=stdio['library']['sha256']
for path,digest in libraries.items():
    if sha(path)!=digest:raise SystemExit('Changed target library: '+str(path))
    if path.read_bytes()[:8]!=b'!<arch>\n':raise SystemExit('Target libraries must be self-contained archives: '+str(path))
fixture=ROOT/'sdk/sacx/apps/jsc_probe/main.cpp'
if args.event_loop:fixture=ROOT/'tools/webcore_dihos/eventloop_fixture.cpp'
if args.resource_loop:fixture=ROOT/'tools/webcore_dihos/resource_fixture.cpp'
unit=next(u for u in r['translation_units'] if u['unit']=='embedding')
compile_step=next(s for s in r['steps'] if s['label']=='compile-embedding')
required={'JSGlobalContextCreate','JSGlobalContextRelease','JSEvaluateScript',
    'JSStringCreateWithUTF8CString','JSStringRelease','JSValueToNumber'}
if not unit['compiled']:
    raise SystemExit('The recorded target embedding probe failed compilation')
if not required.issubset(set(unit['undefined_symbols'])):
    raise SystemExit('The fixture must retain actual JSC C API calls')
recipe={'probe_report_sha256':sha(args.report),'fixture_sha256':sha(fixture),
    'fixture_profile':'resource' if args.resource_loop else ('eventloop' if args.event_loop else ('spin' if args.spin else 'suite')),
    'builder_sha256':sha(Path(__file__)),'libraries':{str(p):v for p,v in libraries.items()},
    'embedding_compile_template':compile_step,
    'linker_script_sha256':sha(ROOT/'tools/user_threads_program.ld'),
    'loader_source_sha256':sha(ROOT/'kernel/process/dihos_user_elf.c'),
    'loader_header_sha256':sha(ROOT/'kernel/include/process/dihos_user_elf.h')}
abort_source=ROOT/'tools/jsc_dihos/AbortDihOS.cpp'
recipe['abort_diagnostics_sha256']=sha(abort_source)
if libm:recipe['libm_report_sha256']=sha(args.libm_report)
if support:recipe['support_report_sha256']=sha(args.support_report)
if stdio:recipe['stdio_report_sha256']=sha(args.stdio_report)
if args.event_loop:
    recipe['event_loop_sources']={str(p.relative_to(ROOT)):sha(p) for p in
        [ROOT/'tools/webcore_dihos/DispatchQueue.cpp',ROOT/'tools/webcore_dihos/DispatchQueue.h',
         ROOT/'tools/webcore_dihos/port-edits.json']}
    if args.resource_loop:
        for relative in ['tools/webcore_dihos/ResourceLoad.cpp','tools/webcore_dihos/ResourceLoad.h','sdk/sacx/include/sacx_user_network.h','sdk/sacx/include/sacx_user_http_request.h']:
            recipe['event_loop_sources'][relative]=sha(ROOT/relative)
    # This fixture passes concrete WTF objects across the archive boundary.
    # Match CMake's Release ABI (not the C-API-only diagnostic probe's debug
    # header layout). CheckedPtr RELEASE_ASSERT checks remain enabled.
    recipe['event_loop_abi']={'NDEBUG':True,'libcxx_hardening':'extensive'}
    source_root=Path(next(u for u in r['translation_units'] if u['unit']=='allocator')['source']).parents[3]
    edits=json.loads((ROOT/'tools/webcore_dihos/port-edits.json').read_text())
    recipe['event_loop_upstream_inputs']={}
    for relative,edit in edits.items():
        if sha(source_root/relative)!=edit['before_sha256']:raise SystemExit('Pinned event-loop header changed: '+relative)
        recipe['event_loop_upstream_inputs'][relative]=sha(source_root/relative)
    audit=json.loads((ROOT/'build/webcore-dihos/platform-audit.json').read_text())
    runloop_source=source_root/'Source/WTF/wtf/generic/RunLoopGeneric.cpp'
    if sha(runloop_source)!=audit['source_hashes']['Source/WTF/wtf/generic/RunLoopGeneric.cpp']['sha256']:
        raise SystemExit('Generic run-loop source differs from pinned archive')
    recipe['event_loop_upstream_inputs']['Source/WTF/wtf/generic/RunLoopGeneric.cpp']=sha(runloop_source)
key=hashlib.sha256(json.dumps(recipe,sort_keys=True).encode()).hexdigest()[:12]
out=ROOT/'build/user-jsc-native'/key;out.mkdir(parents=True,exist_ok=True)
result={**recipe,'target':r['target'],'linked':False,'loader_accepted':False,
    'qemu_validated':False,'guest_validated':False,'output':str(out),'steps':[]}
def save():
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    (out.parent/('resource-report.json' if args.resource_loop else 'eventloop-report.json' if args.event_loop else 'spin-report.json' if args.spin else 'report.json')).write_text(json.dumps(result,indent=2)+'\n')
def run(label,command):
    log=out/(label+'.log')
    with log.open('w') as stream:
        code=subprocess.run([str(x) for x in command],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT).returncode
    result['steps'].append({'label':label,'argv':[str(x) for x in command],
        'exit_code':code,'log':str(log),'log_sha256':sha(log)})
    save();return code,log.read_text(errors='replace')
elf=out/'jsc.elf'
obj=out/'embedding.o'
embedding_command=list(compile_step['argv'])
embedding_command[embedding_command.index('-c')+1]=str(fixture)
if args.event_loop:
    overlay=out/'patched-headers'
    for relative,edit in edits.items():
        original=(source_root/relative).read_text()
        if original.count(edit['match'])!=1:raise SystemExit('Event-loop patch no longer applies')
        destination=overlay/Path(relative).relative_to('Source/WTF')
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(original.replace(edit['match'],edit['replacement']))
    embedding_command[1:1]=['-DNDEBUG','-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE',
                             '-I'+str(overlay),
                             '-I'+str(ROOT/'tools/webcore_dihos')]
if args.spin:embedding_command.insert(1,'-DDIHOS_JSC_SPIN_TEST=1')
if args.resource_loop:
    # Append-only header was added after the sealed engine's SDK stage. Stage
    # only this header so its base includes still resolve to the one verified
    # SDK tree (mixing copied pragma-once headers duplicates enum definitions).
    network_sdk=out/'network-sdk';network_sdk.mkdir(exist_ok=True)
    (network_sdk/'sacx_user_network.h').write_bytes((ROOT/'sdk/sacx/include/sacx_user_network.h').read_bytes())
    embedding_command.insert(1,'-I'+str(network_sdk))
embedding_command[embedding_command.index('-o')+1]=str(obj)
code,_=run('compile-embedding',embedding_command)
if code:raise SystemExit('Native embedding compilation failed')
code,imports=run('embedding-imports',['aarch64-linux-gnu-nm','-u',obj])
actual_imports=[line.split()[-1] for line in imports.splitlines() if line.strip()]
if code or not required.issubset(set(actual_imports)):
    raise SystemExit('Compiled fixture must retain actual JSC C API calls')
result['embedding_object']={'path':str(obj),'sha256':sha(obj),'undefined_symbols':actual_imports}
runtime_step=next(step for step in r['steps'] if step['label']=='runtime-runtime')
abort_object=out/'abort.o'
code,_=run('abort-diagnostics',[runtime_step['argv'][0],*runtime_step['argv'][1:runtime_step['argv'].index('-O2')],
    '-O2','-ffunction-sections','-fdata-sections','-c',abort_source,'-o',abort_object])
if code:raise SystemExit('Native abort diagnostics compilation failed')
extra_objects=[]
if args.event_loop:
    adapter=out/'dispatch-queue.o';command=list(embedding_command)
    command[command.index('-c')+1]=str(ROOT/'tools/webcore_dihos/DispatchQueue.cpp')
    command[command.index('-o')+1]=str(adapter)
    code,_=run('compile-dispatch-queue',command)
    if code:raise SystemExit('Native event queue compilation failed')
    extra_objects.append(adapter)
    if args.resource_loop:
        resource=out/'resource-load.o';command=list(embedding_command)
        command[command.index('-c')+1]=str(ROOT/'tools/webcore_dihos/ResourceLoad.cpp')
        command[command.index('-o')+1]=str(resource)
        code,_=run('compile-resource-load',command)
        if code:raise SystemExit('Native resource client compilation failed')
        extra_objects.append(resource)
    # Replace only the generic backend object for this separate experiment.
    # The sealed baseline archives and the shipping JSC binary remain intact.
    generic=out/'RunLoopGeneric.o';command=list(embedding_command)
    command[command.index('-c')+1]=str(runloop_source)
    command[command.index('-o')+1]=str(generic)
    code,_=run('compile-generic-runloop',command)
    if code:raise SystemExit('Patched generic run-loop compilation failed')
    extra_objects.append(generic)
code,text=run('link',['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic','--gc-sections','--error-limit=0',
    '--undefined=dihos_user_thread_finalize','--undefined=dihos_user_thread_keys_finalize',
    '--wrap=abort','-z','max-page-size=4096','-T',ROOT/'tools/user_threads_program.ld',obj,abort_object,*extra_objects,
    '--start-group',*libraries.keys(),'--end-group','-o',elf])
if code:
    result['missing_symbols']=sorted(set(re.findall(r'undefined symbol: (.+)',text)))
    save();raise SystemExit('Native engine link failed; see '+str(out/'link.log'))
code,text=run('undefined',['aarch64-linux-gnu-nm','-u',elf])
if code or text.strip():raise SystemExit('Native executable has unresolved imports')
result.update(linked=True,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size)
code,metadata=run('elf-metadata',['aarch64-linux-gnu-readelf','-W','-h','-l','-d',elf])
if code:raise SystemExit('Linked executable metadata inspection failed')
loads=[]
for line in metadata.splitlines():
    fields=line.split()
    if fields and fields[0]=='LOAD':
        loads.append({'virtual_address':int(fields[2],16),'file_bytes':int(fields[4],16),'memory_bytes':int(fields[5],16),
            'writable':'W' in ''.join(fields[6:-1]),'executable':'E' in ''.join(fields[6:-1])})
if not loads:raise SystemExit('Linked executable has no load segments')
result['measured_layout']={'loads':loads,'image_span':
    ((max(p['virtual_address']+p['memory_bytes'] for p in loads)+4095)&~4095)-min(p['virtual_address']&~4095 for p in loads)}
code,symbols=run('startup-symbols',['aarch64-linux-gnu-nm','-S','--defined-only',elf])
if code:raise SystemExit('Native startup symbol inspection failed')
startup={}
for line in symbols.splitlines():
    fields=line.split()
    if len(fields)==4 and fields[3] in ('g_config','os_script_config_storage'):
        address,size=int(fields[0],16),int(fields[1],16)
        if not any(p['writable'] and not p['executable'] and address>=p['virtual_address'] and size<=p['virtual_address']+p['memory_bytes']-address for p in loads):
            raise SystemExit('Mutable WebKit startup table is not in writable, non-executable data: '+fields[3])
        startup[fields[3]]={'address':address,'bytes':size,'writable_nonexecutable':True}
if len(startup)!=2:raise SystemExit('Expected native WebKit startup tables are missing')
result['startup_tables']=startup
inspector=out/'inspect-elf'
code,_=run('inspector-build',['clang-22','-Ikernel/include','tools/inspect_user_elf.c',
    'kernel/process/dihos_user_elf.c','-o',inspector])
if code:raise SystemExit('Host loader inspector build failed')
code,text=run('loader-check',[inspector,elf])
if text.strip().startswith('{'):result['loader']=json.loads(text)
result['loader_accepted']=code==0
for relative,digest in inputs.items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Input changed while linking: '+relative)
for path,digest in libraries.items():
    if sha(path)!=digest:raise SystemExit('Library changed while linking: '+str(path))
if sha(ROOT/'kernel/process/dihos_user_elf.c')!=recipe['loader_source_sha256'] or sha(ROOT/'kernel/include/process/dihos_user_elf.h')!=recipe['loader_header_sha256']:
    raise SystemExit('Production loader changed during executable validation')
if sha(fixture)!=recipe['fixture_sha256'] or sha(abort_source)!=recipe['abort_diagnostics_sha256']:
    raise SystemExit('Embedding or abort diagnostics changed during executable validation')
if args.event_loop:
    for relative,digest in recipe['event_loop_sources'].items():
        if sha(ROOT/relative)!=digest:raise SystemExit('Event-loop source changed during build: '+relative)
save();print(json.dumps({'report':str(out/'report.json'),'linked':True,'loader_accepted':code==0}))
if code:raise SystemExit('Linked executable is outside the current loader contract; it was not deployed')
