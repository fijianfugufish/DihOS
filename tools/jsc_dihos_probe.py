"""Freestanding JSC cross-build/dependency inventory, run in ARM64 WSL.

No Linux capability config, ICU library, generated header or engine object is
borrowed for the target. Expected configure/compile failures are evidence, not
engine success. This never updates the guest or changes the shipping browser.
"""
from pathlib import Path
import argparse,hashlib,json,os,platform,re,shlex,shutil,signal,subprocess,tarfile,tempfile,time

ROOT=Path(__file__).resolve().parents[1]
SCRIPT_SHA=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
VERSION='2.54.2'
SHA='f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56'
parser=argparse.ArgumentParser()
parser.add_argument('--cache',type=Path,default=Path.home()/'.cache/dihos-jsc-target')
parser.add_argument('--inventory',action='store_true',help='complete diagnostic probes even when upstream configure fails')
parser.add_argument('--icu',type=Path,help='verified ICU stage-report.json from the native dependency build')
parser.add_argument('--build-timeout',type=int,default=600,
    help='whole-build time budget in seconds; 0 configures and checks source units only')
parser.add_argument('--build-target',choices=('WTF','JavaScriptCore','jsc'),default='jsc',
    help='validate the native support library independently, or build the engine shell')
parser.add_argument('--reuse-build-dir',type=Path,
    help='reuse a verified same-source cache directory with fresh CMake capability checks')
parser.add_argument('--reuse-header-stage',type=Path,
    help='refresh the same-source build\'s staged headers in place, checking every byte')
args=parser.parse_args()
if args.build_timeout<0:parser.error('--build-timeout must be nonnegative')
if platform.system()!='Linux':raise SystemExit('Run in WSL; target remains aarch64-none-elf')
cache=args.cache.resolve();cache.mkdir(parents=True,exist_ok=True)
if (cache/'sysroot/usr/lib/libicuuc.a').exists() and not args.icu:
    raise SystemExit('This sysroot contains ICU; supply its verified --icu stage-report.json')
out=ROOT/'build/jsc-dihos';out.mkdir(parents=True,exist_ok=True)
archive=ROOT/f'build/browser-engine-arm64/wpewebkit-{VERSION}.tar.xz'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def seal_members(name,members,provenance):
    if not members or any(not p.is_file() or not p.is_relative_to(build_dir.resolve()) for p in members):
        raise SystemExit('Archive contains objects outside its verified build directory')
    sealed_dir=cache/'sealed-archives';sealed_dir.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=sealed_dir) as temporary:
        regular=Path(temporary)/name
        # Quick append preserves distinct objects with identical basenames.
        subprocess.run(['aarch64-linux-gnu-ar','qcs',regular,*members],check=True)
        if regular.read_bytes()[:8]!=b'!<arch>\n':raise SystemExit('Archive sealing failed')
        digest=sha(regular);sealed=sealed_dir/(digest+'-'+name)
        if sealed.is_symlink():raise SystemExit('Symlink in sealed archive cache')
        if not sealed.exists():shutil.copyfile(regular,sealed)
        if sha(sealed)!=digest:raise SystemExit('Modified sealed archive')
    return {**provenance,'members':len(members),'path':str(sealed),
        'sha256':digest,'bytes':sealed.stat().st_size}
if sha(archive)!=SHA:raise SystemExit('Pinned WebKit archive mismatch')
source=cache/f'wpewebkit-{VERSION}'
if not source.exists():
    with tarfile.open(archive,'r:xz') as tar:
        for member in tar.getmembers():
            if not member.name.startswith(f'wpewebkit-{VERSION}/'):raise SystemExit('Unexpected source archive entry')
        tar.extractall(cache,filter='data')
    (source/'.dihos-source-sha256').write_text(SHA)
if (source/'.dihos-source-sha256').read_text()!=SHA:raise SystemExit('Wrong source provenance')
# Validate actual source bytes against the archive, not just a marker.
source_hashes={}
with tarfile.open(archive,'r:xz') as tar:
    for member in tar:
        if member.isfile():
            path=cache/member.name
            if path.is_symlink() or hashlib.sha256(tar.extractfile(member).read()).hexdigest()!=sha(path):
                raise SystemExit('Modified pinned source: '+member.name)
            source_hashes[str(path.relative_to(source))]=sha(path)

report={'format':1,'target':'aarch64-none-elf','source_version':VERSION,'archive_sha256':SHA,
        'engine_built':False,'guest_validated':False,'host_engine_reused':False,
        'scope':'Cross configure and target-source compilation inventory','steps':[]}
report['build_target']=args.build_target
report['support_library_built']=False
report['engine_library_built']=False
report['native_file_sources']={str(path.relative_to(ROOT)):sha(path) for path in
    [ROOT/'tools/jsc_dihos/FileSystemDihOS.cpp']}
report['native_file_access']='disabled in the script-only renderer profile; operations fail'
report['native_vm_sources']={str(path.relative_to(ROOT)):sha(path) for path in
    [ROOT/'tools/jsc_dihos/bmalloc-vm.h',ROOT/'tools/jsc_dihos/OSAllocatorDihOS.cpp']}
report['native_thread_sources']={str(path.relative_to(ROOT)):sha(path) for path in
    [ROOT/'tools/jsc_dihos/ThreadingPrimitivesDihOS.cpp',ROOT/'tools/jsc_dihos/ThreadingDihOS.cpp',ROOT/'tools/jsc_dihos/CurrentTimeDihOS.cpp']}
report['native_random_sources']={str(path.relative_to(ROOT)):sha(path) for path in
    [ROOT/'tools/jsc_dihos/RandomDihOS.cpp',ROOT/'tools/jsc_dihos/BmallocRandomDihOS.cpp',ROOT/'tools/jsc_dihos/RandomDeviceDihOS.cpp']}
report['native_platform_sources']={str(path.relative_to(ROOT)):sha(path) for path in
    [ROOT/'tools/jsc_dihos/PlatformDihOS.cpp',ROOT/'tools/jsc_dihos/ResourcesDihOS.cpp']}
icu_stage=None;extra_cxx_libraries=[]
if args.icu:
    icu_stage=json.loads(args.icu.read_text())
    if sha(ROOT/'tools/jsc_dihos/icu-platform.h')!=icu_stage['profile_sha256']:raise SystemExit('ICU profile changed')
    for relative,digest in icu_stage['files'].items():
        if sha(Path(icu_stage['sysroot'])/relative)!=digest:raise SystemExit('Modified staged ICU input: '+relative)
    rtti=json.loads((ROOT/'build/user-rtti/report.json').read_text())
    rtti_library=ROOT/'build/user-rtti/libdihos-rtti.a'
    if sha(rtti_library)!=rtti['library_sha256']:raise SystemExit('RTTI archive mismatch')
    cxx_library=ROOT/'build/user-programs/libdihos-libcxx.a'
    extra_cxx_libraries=[rtti_library,cxx_library]
    report['icu']={'version':icu_stage['version'],'stage_report_sha256':sha(args.icu),'sysroot':icu_stage['sysroot'],
                   'final_link_validated':False}
    report['extra_cxx_libraries']={str(p):sha(p) for p in extra_cxx_libraries}
edits_path=ROOT/'tools/jsc_dihos/port-edits.json'
edits=json.loads(edits_path.read_text());edits_sha=sha(edits_path);port_source=cache/('port-source-'+edits_sha[:12]+'-v2')
if not port_source.exists():
    shutil.copytree(source,port_source,symlinks=True)
    (port_source/'.dihos-copy-complete').write_text(SHA)
if (port_source/'.dihos-copy-complete').read_text()!=SHA:raise SystemExit('Incomplete port source copy; choose a fresh --cache')
changes={};port_texts={}
for edit_name,edit in edits.items():
    relative=edit.get('path',edit_name)
    original=port_texts.get(relative)
    if original is None:original=(source/relative).read_text()
    if original.count(edit['match'])!=1:raise SystemExit('Pinned port edit no longer applies: '+relative)
    changed=original.replace(edit['match'],edit['replacement'])
    port_texts[relative]=changed
for relative,changed in port_texts.items():
    destination=port_source/relative
    if destination.read_text()!=changed:destination.write_text(changed)
    changes[relative]={'before':sha(source/relative),'after':sha(destination)}
for relative,expected in source_hashes.items():
    if relative in changes:expected=changes[relative]['after']
    if sha(port_source/relative)!=expected:raise SystemExit('Unexpected port source change: '+relative)
source=port_source;report['port_edits']=changes;report['port_edits_sha256']=edits_sha
def save():
    (out/'report.json').write_text(json.dumps(report,indent=2))
def run(label,argv,timeout=90):
    log=out/(label+'.log');start=time.monotonic()
    with log.open('w') as stream:
        process=subprocess.Popen([str(a) for a in argv],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:code=process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
            code=124
    text=log.read_text(errors='replace')
    item={'label':label,'argv':[str(a) for a in argv],'exit_code':code,'seconds':round(time.monotonic()-start,3),
          'log':str(log),'log_sha256':sha(log)}
    report['steps'].append(item);save();print(json.dumps({'step':label,'exit_code':code}),flush=True)
    return code,text

compiler='clang++-22';cc='clang-22'
report['compiler']=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]
resource=Path(subprocess.check_output([cc,'-print-resource-dir'],text=True).strip())/'include'
bundle=json.loads(subprocess.check_output(['python3',str(ROOT/'tools/prepare_user_libcxx.py')],text=True))
report['libcxx']=bundle
# WSL access to thousands of OneDrive-backed headers dominates compilation.
# Stage byte-verified headers on its native filesystem; target ABI/configuration
# is unchanged, and both the source and staged bytes are rechecked below.
header_roots={
    'libcxx':Path(bundle['bundle'])/'libcxx/include',
    'sdk':ROOT/'sdk/sacx/include',
    'runtime':ROOT/'sdk/sacx/user_runtime/include',
    'c-overlay':ROOT/'sdk/sacx/user_runtime/libcxx/c',
    'c-native':ROOT/'sdk/sacx/freestanding/include',
}
header_inputs={}
for name,directory in header_roots.items():
    for path in directory.rglob('*'):
        if path.is_file():header_inputs[name+'/'+str(path.relative_to(directory))]=(path,sha(path))
header_manifest={name:item[1] for name,item in header_inputs.items()}
header_paths_digest=hashlib.sha256('\n'.join(sorted(header_inputs)).encode()).hexdigest()
header_digest=hashlib.sha256(json.dumps(header_manifest,sort_keys=True).encode()).hexdigest()
header_stage=cache/('headers-'+header_digest[:16]);header_stage.mkdir(exist_ok=True)
if args.reuse_header_stage:
    if not args.reuse_build_dir:raise SystemExit('Header reuse requires an explicit same-source build')
    prior=args.reuse_build_dir.resolve();candidate=args.reuse_header_stage.resolve()
    if prior.parent!=cache or not prior.name.startswith('build-') or candidate.parent!=cache or not candidate.name.startswith('headers-'):
        raise SystemExit('Header reuse is limited to native cache directories')
    prior_config=(prior/'CMakeCache.txt').read_text()
    if 'CMAKE_HOME_DIRECTORY:INTERNAL='+str(source)+'\n' not in prior_config or str(candidate/'sdk') not in prior_config:
        raise SystemExit('Header stage does not belong to this verified source/build')
    header_stage=candidate
for relative,(original,digest) in header_inputs.items():
    destination=header_stage/relative
    if destination.is_symlink() or not destination.resolve().is_relative_to(header_stage.resolve()):
        raise SystemExit('Invalid staged header target: '+relative)
    if not destination.exists() or (args.reuse_header_stage and sha(destination)!=digest):
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,destination)
    if sha(destination)!=digest:raise SystemExit('Modified staged target header: '+relative)
if {str(p.relative_to(header_stage)) for p in header_stage.rglob('*') if p.is_file()}!=set(header_inputs):
    raise SystemExit('Unexpected staged target header')
report['staged_headers']={'path':str(header_stage),'tree_sha256':header_digest,'files':len(header_inputs),
    'refreshed_for_incremental_build':bool(args.reuse_header_stage)}
report['staged_headers']['paths_sha256']=header_paths_digest
config=cache/'libcxx-config'
if run('libcxx-configure',['cmake','-S',ROOT/'sdk/sacx/user_runtime/libcxx','-B',config,
                        '-DLIBCXX_INCLUDE='+str(header_stage/'libcxx'),'-DDIHOS_SYNCHRONIZATION=ON'])[0]:
    raise SystemExit('Target header configuration failed')
base=['--target=aarch64-none-elf','-D__DIHOS__=1','-DDIHOS_HEADER_PATHS_REVISION=0x'+header_paths_digest[:16]+'ULL','-I'+str(ROOT/'tools/jsc_dihos'),'-march=armv8-a','-ffreestanding','-fno-builtin','-fno-stack-protector',
      '-fpie','-fno-exceptions','-fno-rtti','-ftls-model=local-exec','-fno-emulated-tls',
      '-nostdinc','-isystem',str(resource),'-I'+str(header_stage/'sdk'),'-I'+str(header_stage/'runtime')]
cxx=base[:base.index('-isystem')]+base[base.index('-isystem')+2:]+['-std=c++23','-nostdinc++','-D_LIBCPP_AVAILABILITY_MINIMUM_HEADER_VERSION=22',
          '-isystem',str(config),'-isystem',str(header_stage/'libcxx'),'-isystem',str(resource),
          '-idirafter',str(header_stage/'c-overlay'),'-idirafter',str(header_stage/'c-native')]
if icu_stage:
    if Path(icu_stage['sysroot']).resolve()!=(cache/'sysroot').resolve():raise SystemExit('ICU sysroot differs from this target cache')
    profile_flags=['-include',str(ROOT/'tools/jsc_dihos/icu-platform.h'),'-isystem',str(Path(icu_stage['sysroot'])/'usr/include')]
    base+=profile_flags;cxx+=profile_flags
sdk_dirs=[ROOT/'sdk/sacx/user_runtime',ROOT/'sdk/sacx/freestanding/include',ROOT/'sdk/sacx/include']
sdk_inputs={str(p.relative_to(ROOT)):sha(p) for directory in sdk_dirs for p in directory.rglob('*') if p.is_file()}
empty=cache/'empty.cpp';empty.write_text('')
_,macros=run('target-macros',[compiler,*cxx,'-dM','-E',empty])
assert '#define __aarch64__ 1' in macros and '#define __ELF__ 1' in macros
assert not re.search(r'^#define (__linux__|__unix__|_WIN32)\b',macros,re.M)
report['target_macros_verified']=True
runtime_dir=cache/'runtime';runtime_dir.mkdir(exist_ok=True)
runtime_sources=['sdk/sacx/user_runtime/runtime.cpp','sdk/sacx/user_runtime/fenv.cpp','sdk/sacx/user_runtime/time.cpp','sdk/sacx/user_runtime/virtual_memory.cpp','sdk/sacx/user_runtime/thread_lifetime.cpp',
 'sdk/sacx/user_runtime/thread_keys.cpp','sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp',
 'sdk/sacx/user_runtime/libcxx/platform.cpp','sdk/sacx/user_runtime/libcxx/c_support.cpp',
 'sdk/sacx/user_runtime/libcxx/synchronization.cpp','sdk/sacx/user_runtime/libcxx/threading.cpp']
objects=[]
for relative in runtime_sources:
    path=ROOT/relative;obj=runtime_dir/(path.name+'.o')
    code,_=run('runtime-'+path.stem,[compiler,*cxx,'-I'+str(ROOT/'third_party/tlsf'),'-O2','-c',path,'-o',obj])
    if code:raise SystemExit('Target runtime compilation failed: '+relative)
    objects.append(obj)
obj=runtime_dir/'tlsf.o'
if run('runtime-tlsf',[cc,*base,'-idirafter',ROOT/'sdk/sacx/user_runtime/libcxx/c','-idirafter',ROOT/'sdk/sacx/freestanding/include',
                      '-DDIHOS_USER_TLSF','-O2','-c',ROOT/'third_party/tlsf/tlsf.c','-o',obj])[0]:raise SystemExit('Target allocator compilation failed')
objects.append(obj);runtime_archive=runtime_dir/'libdihos-runtime.a'
if run('runtime-archive',['aarch64-linux-gnu-ar','rcs',runtime_archive,*objects])[0]:raise SystemExit('Target runtime archive failed')
report['runtime_archive']={'path':str(runtime_archive),'sha256':sha(runtime_archive)}
link_script=cache/'user_program.ld'
link_script.write_text((ROOT/'tools/user_threads_program.ld').read_text()+'\nPROVIDE(main = 0);\nPROVIDE(_Z4mainv = 0);\nPROVIDE(_Z4mainiPPc = 0);\n')
smoke=cache/'thread-smoke.cpp'
smoke.write_text('#include <thread>\n#include "sacx_user_api.h"\nextern "C" int dihos_user_main(const sacx_api*){std::thread thread([]{});thread.join();return 0;}\n')
link_flags='-nostdlib --ld-path=/usr/bin/ld.lld-22 -Wl,-pie,--no-dynamic-linker,-Bsymbolic,-z,max-page-size=4096,-T,'+str(link_script)
# CMake capability programs define main; the SDK starts dihos_user_main.
# This tool-local weak adapter supports actual link checks without a Linux CRT.
adapter=ROOT/'tools/jsc_dihos/feature-main.cpp';adapter_object=cache/'feature-main.o'
report['feature_main_sha256']=sha(adapter)
if run('runtime-feature-adapter',[compiler,*cxx,'-O2','-ffunction-sections','-c',adapter,'-o',adapter_object])[0]:raise SystemExit('Target feature-test adapter failed')
link_flags+=' -Wl,--gc-sections,--undefined=dihos_user_thread_finalize,--undefined=dihos_user_thread_keys_finalize'
# Validate C and both ordinary freestanding C++ main spellings. The negative
# case must retain its main and fail on the requested dependency, even with GC.
bridge={}
for name,text,language,expected in [
    ('c','int main(void){return 98;}','c',98),
    ('cpp0','int main(){return 99;}','cpp',99),
    ('cpp2','int main(int argc,char** argv){return argc==0 && argv && !argv[0] ? 100 : 162;}','cpp',100),
    ('missing','extern "C" int dihos_missing_feature_dependency();int main(){return dihos_missing_feature_dependency();}','cpp',None)]:
    src=cache/('feature-'+name+'.'+language);src.write_text(text+'\n')
    obj=cache/('feature-'+name+'.o');elf=out/('feature-main-'+name+'.elf')
    compile_flags=cxx if language=='cpp' else base+['-idirafter',str(ROOT/'sdk/sacx/freestanding/include')]
    driver=compiler if language=='cpp' else cc
    if run('feature-main-compile-'+name,[driver,*compile_flags,'-O2','-ffunction-sections','-c',src,'-o',obj])[0]:
        raise SystemExit('Native main bridge fixture compilation failed')
    code,log=run('feature-main-link-'+name,[compiler,'--target=aarch64-none-elf',*shlex.split(link_flags),obj,adapter_object,runtime_archive,'-o',elf])
    if expected is None:
        if not code or 'dihos_missing_feature_dependency' not in log:
            raise SystemExit('C++ main did not retain its required dependency')
        bridge['required_dependency_rejected']=True
    else:
        if code:raise SystemExit('Native feature main fixture link failed')
        rc,imports=run('feature-main-imports-'+name,['aarch64-linux-gnu-nm','-u',elf])
        if rc or imports.strip():raise SystemExit('Unexpected native main bridge import: '+imports)
        bridge[name]={'sha256':sha(elf),'bytes':elf.stat().st_size,'exit_status':expected,'guest_validated':False}
report['feature_main_bridge']=bridge

toolchain=cache/'toolchain.cmake';sysroot=cache/'sysroot';sysroot.mkdir(exist_ok=True)
module=cache/'cmake/Platform';module.mkdir(parents=True,exist_ok=True)
(module/'DihOS.cmake').write_text('include(Platform/Generic)\nset(CMAKE_EXECUTABLE_SUFFIX ".elf")\nset(CMAKE_DL_LIBS "")\n')
# Static-library compiler ABI checks really compile for the target. No compiler
# works/Threads/ICU result is forced and no target test is executed on Linux.
toolchain.write_text('\n'.join([
    'set(CMAKE_SYSTEM_NAME DihOS)','set(CMAKE_SYSTEM_PROCESSOR aarch64)',
    'list(PREPEND CMAKE_MODULE_PATH '+json.dumps(str(cache/'cmake'))+')',
    'set(CMAKE_C_COMPILER clang-22)','set(CMAKE_CXX_COMPILER clang++-22)',
    'set(CMAKE_AR aarch64-linux-gnu-ar)','set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)',
    'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)',
    'set(CMAKE_C_STANDARD_LIBRARIES '+json.dumps(str(adapter_object)+' '+str(runtime_archive))+')',
    'set(CMAKE_CXX_STANDARD_LIBRARIES '+json.dumps(str(adapter_object)+' -Wl,--start-group '+
       ' '.join(shlex.quote(str(p)) for p in [runtime_archive,*extra_cxx_libraries])+' -Wl,--end-group')+')',
    'set(CMAKE_C_FLAGS_INIT '+json.dumps(shlex.join(base+['-idirafter',str(ROOT/'sdk/sacx/freestanding/include')]))+')',
    'set(CMAKE_CXX_FLAGS_INIT '+json.dumps(shlex.join(cxx))+')',
    'set(CMAKE_FIND_ROOT_PATH '+json.dumps(str(sysroot))+')',
    'set(CMAKE_SYSTEM_PREFIX_PATH /usr)',
    'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)','set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)',
    'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)','set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)','']))
features=['-DPORT=JSCOnly','-DCMAKE_BUILD_TYPE=Release','-DENABLE_STATIC_JSC=ON','-DUSE_HEADER_MAPS=OFF',
          '-DENABLE_JIT=OFF','-DENABLE_DFG_JIT=OFF','-DENABLE_FTL_JIT=OFF','-DENABLE_C_LOOP=ON',
          '-DENABLE_WEBASSEMBLY=OFF','-DENABLE_API_TESTS=OFF','-DENABLE_SAMPLING_PROFILER=OFF',
          '-DENABLE_REMOTE_INSPECTOR=OFF','-DUSE_LIBBACKTRACE=OFF','-DUSE_SYSTEM_MALLOC=ON',
          '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
if icu_stage:features.append('-DICU_ROOT='+str(Path(icu_stage['sysroot'])/'usr'))
dependency_key=json.dumps(report.get('extra_cxx_libraries',{}),sort_keys=True)+(sha(args.icu) if args.icu else '')
build_dir=cache/('build-'+hashlib.sha256((sha(toolchain)+edits_sha+sha(runtime_archive)+dependency_key).encode()).hexdigest()[:12])
fresh=[]
if args.reuse_build_dir:
    candidate=args.reuse_build_dir.resolve()
    if candidate.parent!=cache or not candidate.name.startswith('build-'):
        raise SystemExit('Reuse requires a direct native build directory inside the selected cache')
    cache_file=candidate/'CMakeCache.txt'
    if not cache_file.is_file():raise SystemExit('Reuse requires an existing configured build')
    home=next((line.split('=',1)[1] for line in cache_file.read_text().splitlines()
        if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')),None)
    if home!=str(source):raise SystemExit('Refusing to reuse a different source/profile tree')
    build_dir=candidate;fresh=['--fresh']
report['fresh_reconfiguration']=bool(fresh)
code,text=run('engine-configure',['cmake',*fresh,'-S',source,'-B',build_dir,'-G','Ninja',
                               '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),
                               '-DDIHOS_SDK_ROOT='+str(ROOT),'-DDIHOS_RUNTIME_ARCHIVE='+str(runtime_archive),
                               '-DDIHOS_THREAD_SMOKE='+str(smoke),'-DCMAKE_EXE_LINKER_FLAGS='+link_flags,*features],180)
if (build_dir/'dihos-thread-link.log').exists():
    shutil.copy2(build_dir/'dihos-thread-link.log',out/'runtime-thread-link.log')
if (build_dir/'dihos-thread-link.elf').exists():
    linked=build_dir/'dihos-thread-link.elf'
    code_symbols,symbols=run('runtime-thread-imports',['aarch64-linux-gnu-nm','-u',linked])
    if code_symbols or symbols.strip():raise SystemExit('Native thread link proof has unresolved imports')
    report['runtime_thread_link']={'path':str(linked),'sha256':sha(linked),'undefined_symbols':[]}
report['configure_succeeded']=code==0
if not code and args.build_timeout:
    # Keep going through independent units so one platform error does not hide
    # the remaining dependency inventory. Ninja is the configured generator.
    targets=[args.build_target]
    if args.build_target in ('JavaScriptCore','jsc'):targets.append('JavaScriptCoreJIT')
    code,text=run('engine-build',['cmake','--build',build_dir,'--target',*targets,'--parallel','2','--','-k','0'],args.build_timeout)
    report['engine_built']=code==0 and args.build_target=='jsc'
    report['support_library_built']=code==0
    report['engine_library_built']=code==0 and args.build_target in ('JavaScriptCore','jsc')
    if not code:
        for label,name in [('support_library','libWTF.a'),('engine_library','libJavaScriptCore.a'),('allocator_library','libbmalloc.a')]:
            artifact=build_dir/'lib'/name
            if artifact.is_file() and (label!='engine_library' or report['engine_library_built']):
                provenance={'cmake_archive':str(artifact),'cmake_archive_sha256':sha(artifact)}
                if artifact.read_bytes()[:8]==b'!<thin>\n':
                    names=subprocess.check_output(['aarch64-linux-gnu-ar','t',artifact],text=True).splitlines()
                    members=[(Path(name) if Path(name).is_absolute() else artifact.parent/name).resolve() for name in names]
                    report[label]=seal_members(name,members,{**provenance,'thin_archive_sealed':True})
                    continue
                elif artifact.read_bytes()[:8]!=b'!<arch>\n':raise SystemExit('Invalid native archive')
                report[label]={**provenance,'path':str(artifact),'sha256':sha(artifact),'bytes':artifact.stat().st_size}
        if report['engine_library_built']:
            members=[]
            for entry in json.loads((build_dir/'compile_commands.json').read_text()):
                command=shlex.split(entry['command'])
                if '-o' not in command:continue
                output=Path(command[command.index('-o')+1])
                if 'JavaScriptCoreJIT.dir' not in output.parts or output.suffix not in ('.o','.obj'):continue
                members.append((Path(entry['directory'])/output).resolve())
            report['bytecode_library']=seal_members('libJavaScriptCoreBytecode.a',members,
                {'cmake_object_target':'JavaScriptCoreJIT','jit_enabled':False})
report['engine_build_skipped']=not args.build_timeout

forward=cache/'include/JavaScriptCore';forward.mkdir(parents=True,exist_ok=True)
headers={}
for path in (source/'Source/JavaScriptCore').rglob('*.h'):headers.setdefault(path.name,[]).append(path)
report['ambiguous_forwarding_headers']={name:[str(p.relative_to(source)) for p in paths] for name,paths in headers.items() if len(paths)>1}
for name,paths in headers.items():
    if len(paths)==1:(forward/name).write_text('#include '+json.dumps(str(paths[0]))+'\n')
include=['-I'+str(build_dir),'-I'+str(build_dir/'JavaScriptCore/PrivateHeaders'),
         '-I'+str(build_dir/'JavaScriptCore/Headers'),'-I'+str(build_dir/'JavaScriptCore/DerivedSources'),
         '-I'+str(build_dir/'WTF/Headers'),'-I'+str(build_dir/'bmalloc/Headers'),
         '-I'+str(build_dir/'bmalloc/PrivateHeaders'),'-I'+str(cache/'include'),'-I'+str(source/'Source/WTF'),'-I'+str(source/'Source/bmalloc'),
         '-I'+str(source/'Source/JavaScriptCore')]
for directory in (source/'Source/JavaScriptCore').iterdir():
    if directory.is_dir():include.append('-I'+str(directory))
# Reuse include paths from the actual target build, including nested generated
# and wasm/temporal headers. A failed whole build does not make its correctly
# generated, target-configured headers into host headers.
commands=json.loads((build_dir/'compile_commands.json').read_text())
extractor=next(item for item in commands if item['file'].endswith('/LLIntSettingsExtractor.cpp'))
extractor_args=shlex.split(extractor['command'])
include=[arg for arg in extractor_args if arg.startswith('-I')]+include
report['probe_target_command_sha256']=hashlib.sha256(extractor['command'].encode()).hexdigest()
object_dir=cache/'objects';object_dir.mkdir(exist_ok=True)
settings=['-DBUILDING_JSCONLY__=1','-DENABLE_JIT=0','-DENABLE_DFG_JIT=0','-DENABLE_FTL_JIT=0',
          '-DENABLE_WEBASSEMBLY=0','-DENABLE_C_LOOP=1','-DUSE_SYSTEM_MALLOC=1',
          '-DENABLE_SAMPLING_PROFILER=0','-DENABLE_REMOTE_INSPECTOR=0','-DUSE_LIBBACKTRACE=0']
translation_units=[('embedding',ROOT/'sdk/sacx/apps/jsc_probe/main.cpp'),
    ('native-vm',ROOT/'tools/jsc_dihos/OSAllocatorDihOS.cpp'),
    ('native-random',ROOT/'tools/jsc_dihos/RandomDihOS.cpp'),
    ('native-bmalloc-random',ROOT/'tools/jsc_dihos/BmallocRandomDihOS.cpp'),
    ('native-random-device',ROOT/'tools/jsc_dihos/RandomDeviceDihOS.cpp'),
    ('native-platform',ROOT/'tools/jsc_dihos/PlatformDihOS.cpp'),
    ('native-resources',ROOT/'tools/jsc_dihos/ResourcesDihOS.cpp'),
    ('native-thread-primitives',ROOT/'tools/jsc_dihos/ThreadingPrimitivesDihOS.cpp'),
    ('native-threads',ROOT/'tools/jsc_dihos/ThreadingDihOS.cpp'),('native-time',ROOT/'tools/jsc_dihos/CurrentTimeDihOS.cpp'),('native-file-denial',ROOT/'tools/jsc_dihos/FileSystemDihOS.cpp'),
    ('allocator',source/'Source/WTF/wtf/FastMalloc.cpp'),('threads',source/'Source/WTF/wtf/Threading.cpp'),
    ('upstream-time-replaced',source/'Source/WTF/wtf/CurrentTime.cpp'),('strings',source/'Source/WTF/wtf/text/StringImpl.cpp'),('date-math',source/'Source/WTF/wtf/DateMath.cpp'),
    ('integer-formatting',source/'Source/WTF/wtf/Int128.cpp'),('lock-backoff',source/'Source/WTF/wtf/Lock.cpp'),
    ('utf16-diagnostics',source/'Source/WTF/wtf/PrintStream.cpp'),
    ('api',source/'Source/JavaScriptCore/API/JSBase.cpp'),('global-object',source/'Source/JavaScriptCore/runtime/JSGlobalObject.cpp'),
    ('interpreter',source/'Source/JavaScriptCore/interpreter/Interpreter.cpp'),('gc',source/'Source/JavaScriptCore/heap/Heap.cpp')]
results=[]
for name,path in translation_units:
    if not path.is_file():raise SystemExit('Missing pinned translation unit: '+str(path))
    obj=object_dir/(name+'.o')
    target_command=next((entry for entry in commands if Path(entry['file']).resolve()==path.resolve()),None)
    origin='independent target source probe'
    flags=[*cxx,*include,*settings]
    if name.startswith('native-') and target_command:
        # Check native adapters with the actual configured target flags. Extra
        # probe defines must not conceal a missing config.h in a real build.
        argv=shlex.split(target_command['command']);flags=[];index=1
        while index<len(argv):
            arg=argv[index]
            if arg in ('-o','-c','-MF','-MT','-MQ'):index+=2;continue
            if arg not in ('-MD','-MMD'):flags.append(arg)
            index+=1
        origin='actual CMake target command'
    code,text=run('compile-'+name,[compiler,*flags,'-O1','-ferror-limit=6','-fno-color-diagnostics','-c',path,'-o',obj],120)
    errors=[line for line in text.splitlines() if 'error:' in line or 'fatal error:' in line]
    item={'unit':name,'source':str(path),'source_sha256':sha(path),'compiled':code==0,'errors':errors,'flags_origin':origin}
    if not code:
        item['object_sha256']=sha(obj)
        _,undefined=run('imports-'+name,['aarch64-linux-gnu-nm','-u',obj])
        item['undefined_symbols']=[line.split()[-1] for line in undefined.splitlines() if line.strip()]
    results.append(item)
report['translation_units']=results
report['probe_sha256']=SCRIPT_SHA
report['sdk_inputs']=sdk_inputs
current_inputs={str(p.relative_to(ROOT)):sha(p) for directory in sdk_dirs for p in directory.rglob('*') if p.is_file()}
if current_inputs!=sdk_inputs or sha(edits_path)!=edits_sha:raise SystemExit('Inputs changed during probe; rerun before using its report')
for relative,(original,digest) in header_inputs.items():
    if sha(original)!=digest or sha(header_stage/relative)!=digest:
        raise SystemExit('Target header changed during probe: '+relative)
for relative,digest in report['native_vm_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Native VM adapter changed during probe')
for relative,digest in report['native_file_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Native file profile changed during probe')
for relative,digest in report['native_thread_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Native thread adapter changed during probe')
for relative,digest in report['native_random_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Native random adapter changed during probe')
for relative,digest in report['native_platform_sources'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Native platform adapter changed during probe')
if icu_stage:
    for relative,digest in icu_stage['files'].items():
        if sha(Path(icu_stage['sysroot'])/relative)!=digest:raise SystemExit('ICU changed during probe')
    for path,digest in report['extra_cxx_libraries'].items():
        if sha(Path(path))!=digest:raise SystemExit('C++ dependency changed during probe')
save()
print(json.dumps({'report':str(out/'report.json'),'engine_built':report['engine_built'],
                  'compiled_units':[i['unit'] for i in results if i['compiled']]}),flush=True)
if not args.inventory and not report['engine_built']:raise SystemExit(1)
