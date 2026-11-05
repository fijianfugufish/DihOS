"""Build genuine pinned LLVM arithmetic and libc++ hashing for native JSC."""
import hashlib,json,platform,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
if platform.system()!='Linux':raise SystemExit('Run in WSL; target stays aarch64-none-elf')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
proof=json.loads((ROOT/'build/jsc-dihos/report.json').read_text())
for relative,digest in proof['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed SDK source: '+relative)
prepared=json.loads(subprocess.check_output(['python3',ROOT/'tools/prepare_user_builtins.py'],text=True))
pin=json.loads((ROOT/'tools/user-libraries.lock.json').read_text())
repo=ROOT/'build/user-runtime-deps/llvm-project'
def blob(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
out=ROOT/'build/user-jsc-support';out.mkdir(exist_ok=True)
sources={}
for relative in ['libcxx/src/hash.cpp','libcxx/src/functional.cpp']:
    row=subprocess.check_output(['git','-C',repo,'ls-tree',pin['commit'],relative],text=True).strip()
    meta,name=row.split('\t');mode,kind,oid=meta.split()
    if mode!='100644' or kind!='blob' or name!=relative:raise SystemExit('Missing pinned libc++ source')
    data=(repo/relative).read_bytes()
    if blob(data)!=oid:data=data.replace(b'\r\n',b'\n')
    if blob(data)!=oid:raise SystemExit('Modified pinned libc++ source')
    source=out/Path(relative).name;source.write_bytes(data);sources[source]=sha(source)
step=next(step for step in proof['steps'] if step['label']=='runtime-runtime')
flags=step['argv'][1:step['argv'].index('-O2')]+['-O2','-ffunction-sections','-fdata-sections']
objects=[]
result={'target':'aarch64-none-elf','compiled':False,'guest_validated':False,
    'compiler_rt':prepared,'libcxx_commit':pin['commit'],'libcxx_sources':{str(p):digest for p,digest in sources.items()},
    'builder_sha256':sha(Path(__file__)),'sdk_inputs':proof['sdk_inputs'],'steps':[]}
def compile_unit(compiler,flags,path):
    obj=out/(path.stem+'.o');command=[compiler,*flags,'-c',str(path),'-o',str(obj)]
    completed=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    result['steps'].append({'command':command,'exit_code':completed.returncode,'diagnostics':completed.stdout})
    if completed.returncode:
        (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
        raise SystemExit('Native support compilation failed: '+str(path))
    objects.append(obj)
for source in sources:compile_unit(step['argv'][0],flags,source)
resource=subprocess.check_output(['clang-22','-print-resource-dir'],text=True).strip()
cflags=['--target=aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin',
    '-fno-stack-protector','-fPIC','-ffunction-sections','-fdata-sections','-O2','-nostdinc',
    '-I'+str(ROOT/'sdk/sacx/freestanding/include'),'-isystem',resource+'/include']
for unit in prepared['units']:compile_unit('clang-22',cflags,Path(prepared['bundle'])/unit)
library=out/'libdihos-jsc-support.a'
subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),*[str(path) for path in objects]],check=True)
result.update(compiled=True,library={'path':str(library),'sha256':sha(library),'bytes':library.stat().st_size})
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
subprocess.run(['python3',ROOT/'tools/generate_user_arithmetic_vectors.py','--check'],check=True)
fixture=ROOT/'tools/user_jsc_support_program.cpp';obj=out/'fixture.o';elf=out/'support.elf'
subprocess.run([step['argv'][0],*flags,'-c',fixture,'-o',obj],check=True)
imports=subprocess.check_output(['aarch64-linux-gnu-nm','-u',obj],text=True)
required={'__divti3','__modti3','__udivti3','__umodti3','__floattidf','__fixdfti'}
required.update({'__addtf3','__subtf3','__multf3','__divtf3','__extenddftf2','__extendsftf2','__trunctfdf2','__trunctfsf2'})
if not required.issubset({line.split()[-1] for line in imports.splitlines()}):raise SystemExit('Arithmetic fixture must retain actual compiler helper calls')
if '__hash_memory' not in imports or '__next_prime' not in imports:raise SystemExit('Fixture must retain actual out-of-line libc++ hash functions')
runtime=Path(proof['runtime_archive']['path'])
if sha(runtime)!=proof['runtime_archive']['sha256']:raise SystemExit('Changed native runtime')
extra=[Path(path) for path in proof['extra_cxx_libraries']]
for path in extra:
    if sha(path)!=proof['extra_cxx_libraries'][str(path)]:raise SystemExit('Changed C++ dependency')
subprocess.run(['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic','--gc-sections',
    '--undefined=dihos_user_thread_finalize','--undefined=dihos_user_thread_keys_finalize',
    '-z','max-page-size=4096','-T',ROOT/'tools/user_threads_program.ld',obj,
    '--start-group',library,runtime,*extra,'--end-group','-o',elf],check=True)
if subprocess.check_output(['aarch64-linux-gnu-nm','-u',elf],text=True).strip():raise SystemExit('Support fixture has unresolved symbols')
result['fixture']={'source_sha256':sha(fixture),'vectors_sha256':sha(ROOT/'tools/user_arithmetic_vectors.inc'),
    'generator_sha256':sha(ROOT/'tools/generate_user_arithmetic_vectors.py'),
    'elf_sha256':sha(elf),'elf_bytes':elf.stat().st_size,'exit_status':98,
    'retained_imports':imports.splitlines(),'undefined_symbols':[],'qemu_validated':False}
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'compiled':True,'report':str(out/'report.json'),'units':len(objects)}))
