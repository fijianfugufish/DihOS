"""Pinned musl formatting/parsing with real native, closed-profile streams.

Uses no musl syscall implementation. The sole private-header edit removes an
unused syscall-header include before compiling the portable stdio algorithms.
"""
import hashlib,json,platform,subprocess,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
if platform.system()!='Linux':raise SystemExit('Run in WSL; target stays aarch64-none-elf')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
archive=ROOT/'build/musl-1.2.5.tar.gz'
digest='a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'
if sha(archive)!=digest:raise SystemExit('Wrong musl source archive')
source=Path.home()/'.cache/dihos-libm-1.2.5/source'
with tarfile.open(archive) as tar:
    for member in tar:
        if not member.isfile():continue
        path=source/Path(member.name).relative_to('musl-1.2.5')
        if path.is_symlink() or not path.resolve().is_relative_to(source.resolve()):raise SystemExit('Linked source path')
        if path.read_bytes()!=tar.extractfile(member).read():raise SystemExit('Modified musl source: '+member.name)
out=ROOT/'build/user-stdio';out.mkdir(exist_ok=True)
generated=Path.home()/'.cache/dihos-libm-1.2.5/generated'
expected=subprocess.check_output(['sed','-f',source/'tools/mkalltypes.sed',source/'arch/aarch64/bits/alltypes.h.in',source/'include/alltypes.h.in'])
if (generated/'bits/alltypes.h').read_bytes()!=expected:raise SystemExit('Modified generated type header')
original=source/'src/internal/stdio_impl.h';data=original.read_text()
match='#include "syscall.h"'
if data.count(match)!=1:raise SystemExit('Unexpected upstream stdio header')
header=out/'native-stdio_impl.h'
header.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+
    data.replace(match,'/* Native subset: no syscall header or syscall implementation. */')+
    '\n#include <errno.h>\n#ifdef __cplusplus\n}\n#endif\n')
resource=subprocess.check_output(['clang-22','-print-resource-dir'],text=True).strip()
flags=['--target=aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin',
    '-fno-stack-protector','-fPIC','-ftls-model=local-exec','-fno-emulated-tls',
    '-ffunction-sections','-fdata-sections','-O2','-ffp-contract=off','-nostdinc',
    '-D_XOPEN_SOURCE=700','-include',str(source/'src/include/features.h'),'-include',str(header)]
for directory in [source/'arch/aarch64',source/'arch/generic',source/'src/include',source/'src/internal',generated,source/'include',Path(resource)/'include']:
    flags.extend(['-I',str(directory)])
units=['stdio/'+name+'.c' for name in ['vfprintf','vsnprintf','snprintf','fprintf','printf',
    'sscanf','vsscanf','vfscanf','fwrite','fread','fgets','__uflow','__toread','__towrite','__overflow']]
units+=['internal/shgetc.c','internal/intscan.c','internal/floatscan.c','stdlib/strtod.c','stdlib/strtol.c','stdlib/atoi.c']
backend=ROOT/'tools/jsc_dihos/StdioDihOS.cpp'
result={'target':'aarch64-none-elf','compiled':False,'guest_validated':False,
    'archive_sha256':digest,'builder_sha256':sha(Path(__file__)),'backend_sha256':sha(backend),
    'private_header':{'before':sha(original),'after':sha(header),'edit':'remove unused syscall.h include; preserve private stdio/errno C linkage in the native C++ backend'},
    'sources':{relative:sha(source/'src'/relative) for relative in units},'steps':[],
    'scope':'Diagnostic stdout/stderr, EOF stdin, denied file opens, fixed C locale, thread-local errno'}
result['compiler_version']=subprocess.check_output(['clang-22','--version'],text=True)
result['sdk_inputs']={str(path.relative_to(ROOT)):sha(path) for path in (ROOT/'sdk/sacx/include').glob('sacx_user*') if path.is_file()}
objects=[]
def compile_unit(compiler,options,path):
    obj=out/(path.stem+'.o');command=[compiler,*options,'-c',str(path),'-o',str(obj)]
    process=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    result['steps'].append({'command':command,'exit_code':process.returncode,'diagnostics':process.stdout})
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    if process.returncode:raise SystemExit('Native stdio compilation failed: '+str(path))
    objects.append(obj)
for relative in units:compile_unit('clang-22',flags+['-std=c99'],source/'src'/relative)
compile_unit('clang++-22',flags+['-std=c++23','-nostdinc++','-fno-exceptions','-fno-rtti',
    '-I'+str(ROOT/'sdk/sacx/include')],backend)
library=out/'libdihos-stdio.a'
subprocess.run(['aarch64-linux-gnu-ar','rcs',library,*objects],check=True)
result.update(compiled=True,library={'path':str(library),'sha256':sha(library),'bytes':library.stat().st_size})
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
runtime_report=json.loads((ROOT/'build/jsc-dihos/report.json').read_text())
runtime=Path(runtime_report['runtime_archive']['path'])
if sha(runtime)!=runtime_report['runtime_archive']['sha256']:raise SystemExit('Changed native runtime')
dependencies=[library,runtime]
for name in ['user-libm','user-jsc-support']:
    dependency=json.loads((ROOT/'build'/name/'report.json').read_text())
    path=Path(dependency['library']['path'])
    if not dependency['compiled'] or sha(path)!=dependency['library']['sha256']:raise SystemExit('Invalid native dependency: '+name)
    dependencies.append(path)
fixture=ROOT/'tools/user_stdio_program.cpp';obj=out/'fixture.o';elf=out/'stdio.elf'
subprocess.run(['clang++-22',*flags,'-std=c++23','-nostdinc++','-fno-exceptions','-fno-rtti',
    '-I'+str(ROOT/'sdk/sacx/include'),'-c',fixture,'-o',obj],check=True)
process=subprocess.run(['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic','--gc-sections',
    '--undefined=dihos_user_thread_finalize','--undefined=dihos_user_thread_keys_finalize',
    '-z','max-page-size=4096','-T',ROOT/'tools/user_threads_program.ld',obj,
    '--start-group',*dependencies,'--end-group','-o',elf],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/'fixture-link.log').write_text(process.stdout)
if process.returncode:raise SystemExit('Native C stdio fixture link failed; see '+str(out/'fixture-link.log'))
if subprocess.check_output(['aarch64-linux-gnu-nm','-u',elf],text=True).strip():raise SystemExit('Stdio fixture has unresolved symbols')
result['fixture']={'source_sha256':sha(fixture),'elf_sha256':sha(elf),'elf_bytes':elf.stat().st_size,
    'exit_status':99,'undefined_symbols':[],'qemu_validated':False,
    'libraries':{str(path):sha(path) for path in dependencies}}
for relative,digest in result['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK changed during stdio build')
if sha(backend)!=result['backend_sha256']:raise SystemExit('Native stdio backend changed during build')
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'compiled':True,'units':len(objects),'report':str(out/'report.json')}))
