"""Compile pinned musl's generic arithmetic sources for the native SACX ABI.

No musl syscall, thread, allocator, loader or file implementation is included.
Archive compilation is not execution validation; remaining imports are recorded.
Run in ARM64 WSL. Download the upstream archive separately before invoking.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--fixture-runtime-report',type=Path)
parser.add_argument('--c-support',action='store_true',help='also build selected pure C string, sorting and environment support')
args=parser.parse_args()
DIGEST = 'a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'
ARCHIVE = ROOT / 'build/musl-1.2.5.tar.gz'
if platform.system() != 'Linux' or platform.machine() not in ('aarch64', 'arm64'):
    raise SystemExit('Run in ARM64 WSL; target remains aarch64-none-elf')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

if sha(ARCHIVE) != DIGEST:
    raise SystemExit('Pinned musl source archive hash mismatch')
cache = Path.home() / '.cache/dihos-libm-1.2.5'
source = cache / 'source'
source.mkdir(parents=True, exist_ok=True)
expected = set()
with tarfile.open(ARCHIVE) as archive:
    for member in archive:
        relative = PurePosixPath(member.name)
        if relative.is_absolute() or '..' in relative.parts or relative.parts[0] != 'musl-1.2.5':
            raise SystemExit('Unsafe source archive member')
        if member.isdir():
            continue
        if not member.isfile():
            raise SystemExit('Non-regular source archive member: ' + member.name)
        path = source.joinpath(*relative.parts[1:])
        if path.is_symlink() or not path.resolve().is_relative_to(source.resolve()):
            raise SystemExit('Source cache path escapes verified directory')
        data = archive.extractfile(member).read()
        expected.add(path)
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if path.read_bytes() != data:
            raise SystemExit('Modified cached source: ' + str(path))
if set(path for path in source.rglob('*') if path.is_file()) != expected:
    raise SystemExit('Unexpected files in cached musl source')

generated = cache / 'generated/bits'
generated.mkdir(parents=True, exist_ok=True)
alltypes = subprocess.check_output(['sed', '-f', source/'tools/mkalltypes.sed',
    source/'arch/aarch64/bits/alltypes.h.in', source/'include/alltypes.h.in'])
(generated/'alltypes.h').write_bytes(alltypes)
resource = subprocess.check_output(['clang-22', '-print-resource-dir'], text=True).strip()
flags = ['--target=aarch64-none-elf', '-march=armv8-a', '-std=c99',
    '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fPIC',
    '-ffunction-sections', '-fdata-sections', '-O2', '-ffp-contract=off',
    '-D_XOPEN_SOURCE=700', '-nostdinc', '-include', str(source/'src/include/features.h')]
for directory in [source/'arch/aarch64', source/'arch/generic', source/'src/include',
        source/'src/internal', generated.parent, source/'include', Path(resource)/'include']:
    flags.extend(['-I', str(directory)])
units = sorted((source/'src/math').glob('*.c'))
if args.c_support:
    units += [source/('src/'+relative) for relative in [
        'string/strchr.c','string/strchrnul.c','string/strrchr.c','string/memrchr.c','string/strcpy.c',
        'string/stpcpy.c','string/strncpy.c','string/stpncpy.c','string/strcat.c',
        'string/strncat.c','string/strncmp.c','string/strncasecmp.c','string/strstr.c','string/strnlen.c',
        'ctype/tolower.c','stdlib/abs.c','stdlib/labs.c','stdlib/div.c',
        'stdlib/qsort.c','stdlib/qsort_nr.c','env/getenv.c']]
recipe = {'archive_url':'https://musl.libc.org/releases/musl-1.2.5.tar.gz',
    'archive_sha256':DIGEST, 'builder_sha256':sha(Path(__file__)),
    'target':'aarch64-none-elf', 'flags':flags,
    'compiler':subprocess.check_output(['clang-22','--version'],text=True),
    'sources':{str(path.relative_to(source)):sha(path) for path in units},
    'alltypes_sha256':hashlib.sha256(alltypes).hexdigest()}
if args.c_support:recipe['native_c_support_sha256']=sha(ROOT/'tools/jsc_dihos/CLibraryDihOS.cpp')
key = hashlib.sha256(json.dumps(recipe,sort_keys=True).encode()).hexdigest()[:12]
out = cache / key
out.mkdir(exist_ok=True)
report_dir = ROOT/'build/user-libm'
report_dir.mkdir(parents=True, exist_ok=True)
report = {**recipe,'compiled':False,'guest_validated':False,'steps':[],
    'scope':'Unmodified generic math sources and optional selected pure C support; no musl OS implementation',
    'c_support':args.c_support,
    'license':str(source/'COPYRIGHT')}

def compile_unit(path):
    obj = out/(path.stem+'.o')
    command = ['clang-22', *flags, '-c', str(path), '-o', str(obj)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return obj, {'source':str(path.relative_to(source)), 'exit_code':result.returncode,
        'command':command, 'diagnostics':result.stdout}

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    results = list(pool.map(compile_unit, units))
report['steps'] = [step for _,step in results]
if all(not step['exit_code'] for _,step in results):
    library = out/'libdihos-math.a'
    objects=[obj for obj,_ in results]
    if args.c_support:
        native=out/'native-c-support.o'
        subprocess.run(['clang++-22',*flags,'-std=c++23','-fno-exceptions','-fno-rtti',
            '-c',str(ROOT/'tools/jsc_dihos/CLibraryDihOS.cpp'),'-o',str(native)],check=True)
        objects.append(native)
    subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),*[str(obj) for obj in objects]],check=True)
    undefined = subprocess.check_output(['aarch64-linux-gnu-nm','-u',str(library)],text=True)
    defined = subprocess.check_output(['aarch64-linux-gnu-nm','-g','--defined-only',str(library)],text=True)
    exports = {line.split()[-1] for line in defined.splitlines() if len(line.split())>=3}
    imports = {line.split()[-1] for line in undefined.splitlines() if len(line.split())==2}
    report.update(compiled=True,library={'path':str(library),'sha256':sha(library),
        'bytes':library.stat().st_size},remaining_imports=sorted(imports-exports))
    (report_dir/'COPYRIGHT').write_bytes((source/'COPYRIGHT').read_bytes())
    (report_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if args.fixture_runtime_report:
        runtime_report=json.loads(args.fixture_runtime_report.read_text())
        for relative,digest in runtime_report['sdk_inputs'].items():
            if sha(ROOT/relative)!=digest:raise SystemExit('Changed runtime source: '+relative)
        runtime=Path(runtime_report['runtime_archive']['path'])
        if sha(runtime)!=runtime_report['runtime_archive']['sha256']:
            raise SystemExit('Changed runtime archive')
        fixture=ROOT/'tools/user_math_program.cpp'
        obj=out/'fixture.o';elf=report_dir/'math.elf'
        subprocess.run(['clang++-22',*flags,'-std=c++23','-fno-exceptions','-fno-rtti',
            '-DDIHOS_C_SUPPORT='+str(int(args.c_support)),
            '-I'+str(ROOT/'sdk/sacx/include'),'-c',str(fixture),'-o',str(obj)],check=True)
        subprocess.run(['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic',
            '--gc-sections','--undefined=dihos_user_thread_finalize',
            '--undefined=dihos_user_thread_keys_finalize','-z','max-page-size=4096',
            '-T',str(ROOT/'tools/user_threads_program.ld'),str(obj),
            '--start-group',str(library),str(runtime),'--end-group','-o',str(elf)],check=True)
        undefined=subprocess.check_output(['aarch64-linux-gnu-nm','-u',str(elf)],text=True)
        if undefined.strip():raise SystemExit('Math fixture has unresolved symbols')
        report['fixture']={'source_sha256':sha(fixture),'elf_sha256':sha(elf),
            'elf_bytes':elf.stat().st_size,'runtime_sha256':sha(runtime),
            'runtime_report_sha256':sha(args.fixture_runtime_report),'exit_status':96,
            'undefined_symbols':[],'guest_validated':False}
(report_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'compiled':report['compiled'],'units':len(units),
    'report':str(report_dir/'report.json')}))
if not report['compiled']:
    raise SystemExit(1)
