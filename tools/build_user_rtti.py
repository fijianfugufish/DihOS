"""Build selected pinned libc++abi RTTI and a freestanding native ELF proof."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--sdk-report',type=Path,default=ROOT/'build/jsc-dihos/report.json')
args=parser.parse_args()
report_path=args.sdk_report
jsc=json.loads(report_path.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for relative,digest in jsc['sdk_inputs'].items():
    if sha(ROOT/relative)!=digest:raise SystemExit('SDK changed: rerun the JSC native inventory')
runtime=Path(jsc['runtime_archive']['path'])
if sha(runtime)!=jsc['runtime_archive']['sha256']:raise SystemExit('Runtime changed')
abi=json.loads(subprocess.check_output(['python3',str(ROOT/'tools/prepare_user_abi.py')],text=True))
source=Path(abi['bundle'])/'libcxxabi'
step=next(s for s in jsc['steps'] if s['label']=='runtime-runtime')
flags=step['argv'][1:step['argv'].index('-O2')]+['-frtti','-DNDEBUG','-I'+str(source/'include'),'-O2','-ffunction-sections','-fdata-sections']
out=ROOT/'build/user-rtti';out.mkdir(exist_ok=True)
objects=[]
for path in [source/'src'/name for name in ['private_typeinfo.cpp','stdlib_typeinfo.cpp','stdlib_exception.cpp','cxa_aux_runtime.cpp']]+[ROOT/'tools/jsc_dihos/rtti_platform.cpp']:
    obj=out/(path.stem+'.o')
    with (out/(path.stem+'.log')).open('w') as log:
        subprocess.run([step['argv'][0],*flags,'-c',str(path),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
    objects.append(obj)
library=out/'libdihos-rtti.a'
subprocess.run(['aarch64-linux-gnu-ar','rcs',str(library),*[str(p) for p in objects]],check=True)
main=out/'main.o'
subprocess.run([step['argv'][0],*flags,'-c',str(ROOT/'tools/user_rtti_program.cpp'),'-o',str(main)],check=True)
elf=out/'rtti.elf'
subprocess.run(['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic','--gc-sections',
                '--undefined=dihos_user_thread_finalize','--undefined=dihos_user_thread_keys_finalize','-z','max-page-size=4096',
                '-T',str(ROOT/'tools/user_threads_program.ld'),str(main),str(library),str(runtime),'-o',str(elf)],check=True)
undefined=subprocess.check_output(['aarch64-linux-gnu-nm','-u',str(elf)],text=True)
if undefined.strip():raise SystemExit('RTTI ELF has unresolved symbols: '+undefined)
result={'abi':abi,'jsc_report_sha256':sha(report_path),'probe_sha256':sha(Path(__file__)),
        'library_sha256':sha(library),'elf_sha256':sha(elf),'elf_bytes':elf.stat().st_size,
        'undefined_symbols':[],'guest_validated':False,'qemu_validated':False}
(out/'report.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
