"""Execute the real networking adapter fixture in the ARM64 EL0 test kernel.

Keeps the previous QEMU evidence/programs read-only. Ethernet/TLS delivery in
this test kernel is modeled; the WebCore parser and user/kernel copies are real.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,shutil,re
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--include-page',action='store_true');args=p.parse_args();args.include_page=True
OUT=ROOT/('build/dihscover-image-fallback-validation/repro-qemu-page' if args.include_page else 'build/dihscover-image-fallback-validation/repro-qemu');OUT.mkdir(parents=True,exist_ok=True)
renderer=ROOT/'build/dihscover-complex-sites-validation/renderer'
report=json.loads((renderer/'report.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert report['linked'] and report['fixture_elf_sha256']==sha(renderer/'resource.elf')
text=(ROOT/'build/user-runtime/user_program.inc').read_text()
text=re.sub(r'^#define USER_(?:HAS_|NATIVE_PAGE_).*\n','',text,flags=re.M)
text=re.sub(r'^extern const unsigned char user_(?:native_page|webcore_resource)_elf\[\d+\];\n','',text,flags=re.M)
text+='\n#define USER_HAS_HTTP 1\n#define USER_HAS_WEBCORE_RESOURCE 1\n#define USER_IMAGE_TESTS 1\n#define USER_FRAME_TESTS 1\n#define USER_POST_TESTS 1\n#define USER_COOKIE_TESTS 1\n#define USER_BLOB_TESTS 1\n#define USER_FAILED_IMAGE_TESTS 1\n'
text+='extern const unsigned char user_webcore_resource_elf['+str((renderer/'resource.elf').stat().st_size)+'];\n'
if args.include_page:
    text+='\n#define USER_HAS_NATIVE_PAGE 1\n#define USER_NATIVE_PAGE_WINDOW 1\n#define USER_NATIVE_PAGE_NAVIGATION 1\n#define USER_NATIVE_PAGE_HTTPS 1\n#define USER_NATIVE_PAGE_EMBEDDED 1\n'
    text+='extern const unsigned char user_native_page_elf['+str((renderer/'page.elf').stat().st_size)+'];\n'
(OUT/'user_program.inc').write_text(text)
assembly=[]
for name,path in [('large',ROOT/'build/user-programs/large.elf'),('large_fault',ROOT/'build/user-programs/large_fault.elf'),('http',ROOT/'build/user-http/http.elf'),('webcore_resource',renderer/'resource.elf')]:
    assembly.append('.section .rodata.user_'+name+',"a"\n.balign 16\n.global user_'+name+'_elf\nuser_'+name+'_elf:\n.incbin "'+str(path)+'"\n')
(OUT/'user_program_blobs.S').write_text('\n'.join(assembly))
if args.include_page:
    with (OUT/'user_program_blobs.S').open('a') as f:f.write('.section .rodata.user_native_page,"a"\n.balign 16\n.global user_native_page_elf\nuser_native_page_elf:\n.incbin "'+str(renderer/'page.elf')+'"\n')
inputs={};objects=[];steps=[]
def run(name,args,timeout=120):
    with (OUT/(name+'.log')).open('w') as f:result=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    steps.append({'label':name,'exit_code':result.returncode,'argv':list(map(str,args))})
    if result.returncode:raise SystemExit(name+' failed; inspect '+str(OUT/(name+'.log')))
flags=['--target=aarch64-none-elf','-march=armv8-a','-mstrict-align','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-I'+str(OUT),'-Ikernel/include','-Iinclude','-Ithird_party/BearSSL/inc','-Ithird_party/BearSSL/src','-DUSER_TEST_ARENA_PAGES=65536']
sources=['tools/user_runtime_qemu_start.S',str((OUT/'user_program_blobs.S').relative_to(ROOT)),'tools/user_runtime_qemu_tests.c','kernel/asm/aa64/aa64_user.c','kernel/asm/aa64/aa64_user_slice.c','kernel/asm/aa64/aa64_user_timer.c','kernel/memory/aarch64_user_vm.c','kernel/process/dihos_process.c','kernel/process/dihos_user_elf.c','kernel/process/dihos_user_sacx.c','kernel/process/dihos_user_window.c','kernel/process/dihos_browser_bridge.c','kernel/process/dihos_user_memory.c','kernel/process/dihos_user_runtime.c','kernel/system/entropy.c','third_party/BearSSL/src/rand/hmac_drbg.c','third_party/BearSSL/src/mac/hmac.c','third_party/BearSSL/src/hash/sha2small.c','third_party/BearSSL/src/codec/dec32be.c','third_party/BearSSL/src/codec/enc32be.c']
for name in sources:
    obj=OUT/(Path(name).name+'.o');run('compile-'+Path(name).name,['clang-22',*flags,'-c',name,'-o',obj]);objects.append(obj);inputs[name]=sha(ROOT/name)
elf=OUT/'kernel.elf';run('link',['ld.lld-22','-T','tools/user_runtime_qemu.ld','-nostdlib',*objects,'-o',elf])
for name in ['tools/browser_fixtures/blob_probe.html','tools/browser_fixtures/blob_probe.inc']:
    inputs[name]=sha(ROOT/name)
qemu=shutil.which('qemu-system-aarch64')
command=[]
if not qemu:
    qemu=str(ROOT/'build/user-runtime/qemu-root/usr/bin/qemu-system-aarch64')
    command=['env','LD_LIBRARY_PATH='+str(ROOT/'build/user-runtime/qemu-root/usr/lib/aarch64-linux-gnu')]
command +=[qemu,'-machine','virt,secure=off,virtualization=off,gic-version=3','-cpu','cortex-a72','-m','1G','-smp','1','-nodefaults','-nographic','-serial','stdio','-monitor','none','-semihosting-config','enable=on,target=native','-no-reboot','-kernel',str(elf)]
run('run',command,180)
log=(OUT/'run.log').read_text()
assert 'PASS: memory Blob, FileReader slice, blob XHR/fetch/image, revocation and cross-origin read denial' in log
assert 'PASS: iframe JavaScript performs asynchronous POST through checked SACX operation 40' in log
assert 'PASS: native iframes paint, route clicks, run JavaScript and postMessage; cross-origin and sandbox origin isolation hold' in log
assert 'PASS: twenty plain pointer movements cause zero additional viewport uploads' in log
assert 'PASS: inline SVG, data SVG, external SVG and PNG/JPEG/GIF/BMP/WebP paint real pixels' in log
assert 'WebCore cookies: credential omission, separate Set-Cookie fields, trusted TLS gate and malformed-header rejection OK' in log
assert 'PASS: iframe DOM and HTTP share session cookies; HttpOnly and cross-domain writes are denied' in log
assert 'WebCore POST: owned binary body, request-slot retry and kernel-owned framing OK' in log
assert 'WebCore response fields: empty values and Latin-1 octets OK' in log
assert 'PASS: actual WebCore request/response/error metadata' in log
assert 'PASS: sparse quota/slot bounds' in log and 'FAIL:' not in log and 'FATAL' not in log
(OUT/'report.json').write_text(json.dumps({'passed':True,'real_webcore_adapter':True,'transport_modeled':True,'fixture_sha256':sha(renderer/'resource.elf'),'image_test_sha256':sha(renderer/'page.elf'),'kernel_sha256':sha(elf),'inputs':inputs,'steps':steps,'groups':[s for s in log.splitlines() if s.startswith('PASS:')]},indent=2)+'\n')
print('PASS: real WebCore empty/Latin-1 headers, metadata, redirects, TLS rejection, cancellation and checked streaming executed in ARM64 EL0; modeled network')
