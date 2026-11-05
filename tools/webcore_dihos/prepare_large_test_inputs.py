"""Refresh the changed heap-bound fixture without touching historical inputs."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
OUT=(ROOT/args.output/'fixtures').resolve();assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(parents=True,exist_ok=True)
commands=[['clang-22','--target=aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin','-fno-stack-protector',
 '-fpie','-O2','-Isdk/sacx/include','-DUSER_PROGRAM_KIND=5','-c','tools/user_program.c','-o',str(OUT/'memory.o')],
 ['ld.lld-22','-pie','--no-dynamic-linker','-Bsymbolic','-z','max-page-size=4096','-T','tools/user_program.ld',str(OUT/'memory.o'),'-o',str(OUT/'memory.elf')]]
for command in commands:subprocess.run(command,cwd=ROOT,check=True)
probe_compile=list(commands[0]);probe_compile.remove('-DUSER_PROGRAM_KIND=5')
probe_compile[probe_compile.index('tools/user_program.c')]='tools/user_browser_budget_program.c';probe_compile[-1]=str(OUT/'browser-budget.o')
probe_link=list(commands[1]);probe_link[probe_link.index(str(OUT/'memory.o'))]=str(OUT/'browser-budget.o');probe_link[-1]=str(OUT/'browser-budget.elf')
for command in [probe_compile,probe_link]:subprocess.run(command,cwd=ROOT,check=True)
text=(ROOT/'build/user-runtime/user_program.inc').read_text();data=(OUT/'memory.elf').read_bytes()
replacement='static const unsigned char user_memory_elf[]={'+','.join(str(v) for v in data)+'};'
text,count=re.subn(r'static const unsigned char user_memory_elf\[\]=\{[^}]*\};',replacement,text)
assert count==1;(OUT/'user_program.inc').write_text(text)
probe=(OUT/'browser-budget.elf').read_bytes()
text+='\n#define USER_BROWSER_BUDGET_TEST 1\nstatic const unsigned char user_browser_budget_elf[]={'+','.join(str(v) for v in probe)+'};\n'
(OUT/'user_program.inc').write_text(text)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
names=['tools/user_program.c','tools/user_browser_budget_program.c','tools/user_program.ld','sdk/sacx/include/sacx_user_api.h','build/user-runtime/user_program.inc','tools/webcore_dihos/prepare_large_test_inputs.py']
(OUT/'report.json').write_text(json.dumps({'passed':True,'inputs':{n:sha(ROOT/n) for n in names},'commands':commands,
 'memory_elf_sha256':sha(OUT/'memory.elf'),'include_sha256':sha(OUT/'user_program.inc')},indent=2)+'\n')
