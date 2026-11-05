"""Verify/reuse the preceding selected kernel objects; rebuild only its loader.

An unchanged relink must reproduce the preceding kernel byte-for-byte before
any object is reused. The original objects, kernel and source are read-only.
"""
from pathlib import Path
import hashlib,json,subprocess,shutil
ROOT=Path(__file__).resolve().parents[2];BASE=ROOT/'build/stp/src'
OUT=ROOT/'build/dihscover-startup-speed-validation';OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
proof=json.loads((ROOT/'docs/browser-evaluation/dihscover-startup-progress-20261103.json').read_text())
assert sha(BASE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
snapshot=json.loads((ROOT/'build/stp/source.json').read_text())
for n,d in snapshot['files'].items():assert sha(BASE/n)==d,n
rsp=BASE/'build/obj_kernel_aa64/kernel_link.rsp'
args=[line.strip().strip('"') for line in rsp.read_text().splitlines()]
objects={n:sha(Path(n)) for n in args if n.endswith(('.o','.ld'))}
LLVM=Path('C:/Program Files/LLVM/bin')
def run(name,command):
    if str(command[0]).endswith('ld.lld.exe'):
        response=OUT/(name+'.rsp')
        response.write_text('\n'.join('"'+str(arg).replace('"','\\"')+'"' for arg in command[1:])+'\n')
        command=[command[0],'@'+str(response)]
    with (OUT/(name+'.log')).open('w') as f:
        result=subprocess.run(list(map(str,command)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    assert result.returncode==0,name+' failed'
original=args.copy();original[original.index('-o')+1]=str(OUT/'baseline-check.elf')
run('baseline-relink',[LLVM/'ld.lld.exe',*original])
assert sha(OUT/'baseline-check.elf')==proof['kernel_sha256'],'Cached objects do not reproduce the validated kernel'
source=OUT/'dihos_user_loader.c';shutil.copy2(ROOT/'kernel/process/dihos_user_loader.c',source)
prefix=(BASE/'build.ps1').read_text().split('# ---- BOOT ----')[0]
def q(p):return "'"+str(p).replace("'","''")+"'"
script=OUT/'compile-loader.ps1'
script.write_text(prefix+'\n$flags=New-Kernel-CFlags -Arch aa64 -Target aarch64-none-elf\n'+
    '$flags | ConvertTo-Json | Set-Content -LiteralPath '+q(OUT/'loader-flags.json')+'\n'+
    '& $clang @flags -c '+q(source)+' -o '+q(OUT/'dihos_user_loader.o')+'\n'+
    'if($LASTEXITCODE){throw "loader compile failed"}\n')
run('compile-loader',['powershell.exe','-NoProfile','-File',script,'-ProjectRoot',BASE,'-ArmOnly'])
new=args.copy();old_loader=str(BASE/'build/obj_kernel_aa64/dihos_user_loader.o')
assert new.count(old_loader)==1
new[new.index(old_loader)]=str(OUT/'dihos_user_loader.o');new[new.index('-o')+1]=str(OUT/'KERNEL.ELF')
run('link-kernel',[LLVM/'ld.lld.exe',*new])
for n,d in objects.items():assert sha(Path(n))==d,n
assert sha(BASE/'OS/aa64/KERNEL.ELF')==proof['kernel_sha256']
snapshot['files']['kernel/process/dihos_user_loader.c']=sha(source)
snapshot['parent_snapshot_sha256']=sha(ROOT/'build/stp/source.json')
snapshot['source_overrides']={'kernel/process/dihos_user_loader.c':str(source)}
snapshot['overlay_sha256']=hashlib.sha256(json.dumps(snapshot['files'],sort_keys=True).encode()).hexdigest()
(OUT/'source.json').write_text(json.dumps(snapshot,indent=2)+'\n')
report={'baseline_reproduced':True,'baseline_kernel_sha256':proof['kernel_sha256'],
    'kernel_sha256':sha(OUT/'KERNEL.ELF'),'loader_source_sha256':sha(source),'loader_object_sha256':sha(OUT/'dihos_user_loader.o'),
    'reused_objects':objects,'link_argv':new,'original_objects_unchanged':True,'source_snapshot_sha256':sha(OUT/'source.json')}
(OUT/'kernel-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: baseline kernel reproduced byte-for-byte; only loader object rebuilt with original flags; selected kernel linked; previous binaries/objects unchanged')
