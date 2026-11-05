"""Execute the native memory-only decoder inside the real ARM64 EL0 test kernel."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT/'build/dihscover-media-validation';OUT=BASE/'qemu';OUT.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
fixture=json.loads((BASE/'fixture-report.json').read_text())
elf=BASE/'video.elf';assert fixture['linked'] and sha(elf)==fixture['elf_sha256']
for name,digest in fixture['linked_inputs'].items():assert sha(Path(name))==digest,name
text=(ROOT/'build/user-runtime/user_program.inc').read_text()
text=re.sub(r'^#define USER_(?:HAS_|NATIVE_PAGE_).*\n','',text,flags=re.M)
text+='\n#define USER_HAS_NATIVE_VIDEO 1\nextern const unsigned char user_native_video_elf['+str(elf.stat().st_size)+'];\n'
(OUT/'user_program.inc').write_text(text)
assembly=[]
for name,path in [('large',ROOT/'build/user-programs/large.elf'),('large_fault',ROOT/'build/user-programs/large_fault.elf'),('native_video',elf)]:
    assembly.append('.section .rodata.user_'+name+',"a"\n.balign 16\n.global user_'+name+'_elf\nuser_'+name+'_elf:\n.incbin "'+str(path)+'"\n')
(OUT/'user_program_blobs.S').write_text('\n'.join(assembly))
inputs={};objects=[];steps=[]
def run(name,args,timeout=120):
    with (OUT/(name+'.log')).open('w') as log:
        result=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    steps.append({'label':name,'exit_code':result.returncode,'argv':list(map(str,args))})
    if result.returncode:raise SystemExit('Inspect '+str(OUT/(name+'.log')))
flags=['--target=aarch64-none-elf','-march=armv8-a','-mstrict-align','-ffreestanding',
       '-fno-builtin','-fno-stack-protector','-O2','-g','-I'+str(OUT),'-Ikernel/include',
       '-Iinclude','-Ithird_party/BearSSL/inc','-Ithird_party/BearSSL/src','-DUSER_TEST_ARENA_PAGES=65536']
sources=['tools/user_runtime_qemu_start.S',str((OUT/'user_program_blobs.S').relative_to(ROOT)),
    'tools/user_runtime_qemu_tests.c','kernel/asm/aa64/aa64_user.c','kernel/asm/aa64/aa64_user_slice.c',
    'kernel/asm/aa64/aa64_user_timer.c','kernel/memory/aarch64_user_vm.c','kernel/process/dihos_process.c',
    'kernel/process/dihos_user_elf.c','kernel/process/dihos_user_sacx.c','kernel/process/dihos_user_window.c',
    'kernel/process/dihos_browser_bridge.c','kernel/process/dihos_user_memory.c',
    'kernel/process/dihos_user_runtime.c','kernel/system/entropy.c',
    'third_party/BearSSL/src/rand/hmac_drbg.c','third_party/BearSSL/src/mac/hmac.c',
    'third_party/BearSSL/src/hash/sha2small.c','third_party/BearSSL/src/codec/dec32be.c',
    'third_party/BearSSL/src/codec/enc32be.c']
for name in sources:
    obj=OUT/(Path(name).name+'.o');run('compile-'+Path(name).name,['clang-22',*flags,'-c',name,'-o',obj])
    objects.append(obj);inputs[name]=sha(ROOT/name)
kernel=OUT/'kernel.elf';run('link',['ld.lld-22','-T','tools/user_runtime_qemu.ld','-nostdlib',*objects,'-o',kernel])
qemu=shutil.which('qemu-system-aarch64');command=[]
if not qemu:
    qemu=str(ROOT/'build/user-runtime/qemu-root/usr/bin/qemu-system-aarch64')
    assert Path(qemu).is_file()
    command=['env','LD_LIBRARY_PATH='+str(ROOT/'build/user-runtime/qemu-root/usr/lib/aarch64-linux-gnu')]
run('run',command+[qemu,'-machine','virt,secure=off,virtualization=off,gic-version=3','-cpu','cortex-a72',
    '-m','1G','-smp','1','-nodefaults','-nographic','-serial','stdio','-monitor','none',
    '-semihosting-config','enable=on,target=native','-no-reboot','-kernel',kernel],180)
log=(OUT/'run.log').read_text();assert 'FAIL:' not in log and 'FATAL' not in log
assert 'PASS: native EL0 MP4/H.264 decoder' in log
assert 'SACX video: owned MP4, H.264 frames, BGRA, timestamps, seek, cancellation and malformed input OK' in log
(OUT/'report.json').write_text(json.dumps({'passed':True,'real_arm64_el0':True,
    'fixture_report_sha256':sha(BASE/'fixture-report.json'),'elf_sha256':sha(elf),
    'kernel_sha256':sha(kernel),'log_sha256':sha(OUT/'run.log'),
    'inputs':inputs,'steps':steps,'groups':[line for line in log.splitlines() if line.startswith('PASS:')]},indent=2)+'\n')
print('PASS: real native MP4/H.264 decoding and cleanup in DihOS ARM64 EL0; no browser playback claimed',flush=True)
