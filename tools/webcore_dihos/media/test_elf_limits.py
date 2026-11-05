"""Exercise malformed and dense relocation tables, including the real renderer."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/dihscover-video-player/elf-tests';OUT.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
steps=[]
def run(label,args):
    with (OUT/(label+'.log')).open('w') as log:
        code=subprocess.call(list(map(str,args)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    steps.append({'label':label,'argv':list(map(str,args)),'exit_code':code,'log_sha256':sha(OUT/(label+'.log'))})
    if code:raise SystemExit('Inspect '+str(OUT/(label+'.log')))
for name,source,arguments in [
    ('small','tools/user_elf_tests.c',[]),
    ('dense','tools/user_media_elf_tests.c',[]),
    ('renderer','tools/user_large_elf_tests.c',[ROOT/'build/dihscover-video-player/renderer/page.elf'])]:
    run('compile-'+name,['gcc','-O1','-g','-fsanitize=address,undefined','-Ikernel/include',
        '-Ibuild/user-runtime',source,'kernel/process/dihos_user_elf.c','-o',OUT/name])
    run('run-'+name,[OUT/name,*arguments])
files=['kernel/include/process/dihos_user_elf.h','kernel/process/dihos_user_elf.c',
       'tools/user_elf_tests.c','tools/user_media_elf_tests.c','tools/user_large_elf_tests.c',
       'build/user-runtime/user_program.inc','build/dihscover-video-player/renderer/page.elf',
       'tools/webcore_dihos/media/test_elf_limits.py']
(OUT/'report.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],
    'inputs':{p:sha(ROOT/p) for p in files},'steps':steps},indent=2)+'\n')
print('PASS: small malformed ELF, exact dense fixup ceiling and actual video-renderer relocation validation',flush=True)
