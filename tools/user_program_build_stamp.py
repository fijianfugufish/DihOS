"""Verify immutable base-program inputs/outputs for focused guest-kernel reruns."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--llvm',type=Path,required=True)
mode=parser.add_mutually_exclusive_group(required=True);mode.add_argument('--write',action='store_true');mode.add_argument('--verify',action='store_true');args=parser.parse_args()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
files=set()
for directory in ['sdk/sacx/user_runtime','sdk/sacx/freestanding/include','sdk/sacx/include']:
    files.update(p for p in (ROOT/directory).rglob('*') if p.is_file())
# Harness-only code is always recompiled. These are the sources for its base ELFs.
excluded={'user_runtime_qemu_tests.c','user_runtime_qemu_start.S','user_runtime_qemu.ld'}
files.update(p for p in (ROOT/'tools').glob('user_*') if p.is_file() and p.name not in excluded and p.suffix in ('.c','.cpp','.S','.ld','.inc'))
for relative in ['tools/build_user_programs.ps1','tools/build_user_library.ps1','tools/build_user_sync.ps1',
    'tools/prepare_user_libcxx.py','tools/user-libraries.lock.json','tools/generate_user_calendar_vectors.py',
    'tools/user_program_build_stamp.py','third_party/tlsf/tlsf.c','third_party/tlsf/tlsf.h']:
    files.add(ROOT/relative)
prepared=json.loads(subprocess.check_output([sys.executable,ROOT/'tools/prepare_user_libcxx.py'],text=True))
inputs={str(p.relative_to(ROOT)):sha(p) for p in sorted(files)}
outputs={str(p.relative_to(ROOT)):sha(p) for p in sorted((ROOT/'build/user-programs').iterdir()) if p.suffix in ('.elf','.a')}
if len(outputs)<30:raise SystemExit('Base program build is incomplete')
compilers={name:sha(args.llvm/name) for name in ['clang.exe','clang++.exe','ld.lld.exe','llvm-ar.exe']}
record={'inputs':inputs,'outputs':outputs,'llvm':str(args.llvm.resolve()),'compilers':compilers,
    'libcxx':{key:prepared[key] for key in ['commit','tree_sha256']}}
path=ROOT/'build/user-runtime/base-programs.json'
if args.write:
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(record,indent=2)+'\n')
elif not path.exists() or json.loads(path.read_text())!=record:
    raise SystemExit('Base program inputs/outputs changed; rerun without -ReusePrograms')
print('PASS: base program source, dependency, compiler and ELF hashes verified')
