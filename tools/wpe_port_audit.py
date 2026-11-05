"""Record source-level dependency evidence for the pinned WPE/JSC port.

Call matches are an inventory, not a complete link or correctness analysis.
"""
from pathlib import Path
import argparse,hashlib,json,re
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--source',default=str(Path.home()/'.cache/dihos-webkit/wpewebkit-2.54.2'));args=parser.parse_args()
source=Path(args.source)
expected='f3284e19e47fcb5ac8c63c2e1c113dbd65b2423eb8a545f5b345707829004f56'
if (source/'.dihos-archive-sha256').read_text()!=expected:raise SystemExit('Wrong source provenance')
record={'version':'2.54.2','archive_sha256':expected,'guest_port':False,'inventory_is_not_complete_link_analysis':True,'files':{}}
paths=['Source/cmake/OptionsWPE.cmake','Source/cmake/OptionsJSCOnly.cmake',
       'Source/WTF/wtf/posix/OSAllocatorPOSIX.cpp','Source/WTF/wtf/posix/ThreadingPOSIX.cpp',
       'Source/WTF/wtf/posix/FileSystemPOSIX.cpp','Source/WTF/wtf/posix/FileHandlePOSIX.cpp',
       'Source/WTF/wtf/generic/RunLoopGeneric.cpp','Source/WTF/wtf/RunLoop.cpp',
       'Source/WTF/wtf/ThreadingPrimitives.cpp','Source/WTF/wtf/PlatformOS.h']
pattern=r'\b((?:pthread_\w+)|mmap|munmap|mprotect|madvise|clock_gettime|nanosleep|open|openat|close|read|write|pread|pwrite|stat|fstat|lseek|poll|epoll_wait|eventfd|getrandom|syscall|dlopen|dlsym|fcntl)\s*\('
for relative in paths:
    path=source/relative
    if not path.exists():continue
    text=path.read_text(errors='replace')
    record['files'][relative]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'calls':[{'name':m.group(1),'line':text[:m.start()].count('\n')+1} for m in re.finditer(pattern,text)]}
wpe=(source/'Source/cmake/OptionsWPE.cmake').read_text()
record['wpe_initial_required_packages']=re.findall(r'find_package\(([^\n]+)\)',wpe.split('if (NOT TARGET SQLite3')[0])
api=(root/'sdk/sacx/include/sacx_user_api.h').read_text()
record['current_sacx_user_api_members']=re.findall(r'api\.(\w+)\s*=',api)
record['current_elf_limits']={'file_bytes':32*1024*1024,'image_span_bytes':64*1024*1024,'private_table_pages':192,'threads_per_process':4,'userspace_cores':1,'tls':'bounded local-exec PT_TLS, max 4080 bytes and alignment 16','relocations':'bounded R_AARCH64_RELATIVE only','max_relocations':32768,'dynamic_linker':False}
record['next_target_contracts']=[
    {'area':'memory','required':'Scalable virtual regions and larger engine heap; keep JIT off initially','current':'Larger-image layer: 128 MiB process quota, sixteen guarded 16 MiB regions with 64 KiB commits, expandable TLSF pools and 8 MiB max request pass QEMU; new guest gate pending'},
    {'area':'threads','required':'WTF/POSIX adaptation, detach policy and remaining timed synchronization','current':'Native SACX threads/TLS, C++ lifetime and corrected standard synchronization passed reported Hyper-V fixtures. Selected std::thread/native keys passed reported Hyper-V run (16 joined workers, exit 0x54); untimed standard conditions and monotonic sleep/clock pass ARM64 QEMU'},
    {'area':'events_and_ipc','required':'Owned bounded messages, readiness/waits and shared software surfaces','current':'One synchronous SACX request at a time; no general IPC'},
    {'area':'files','required':'Bundle/runtime data and scoped persistent origin storage via handles','current':'New userspace has no file service; directory grants deferred'},
    {'area':'network','required':'Multiple async requests/connections, cancellation and browser session semantics','current':'Working legacy NetSurf networking; no user-mode network broker'},
    {'area':'loader_runtime','required':'Larger incrementally loaded images and full target C/C++/ICU libraries','current':'32 MiB file / 64 MiB static PIE image with bounded indexed RELATIVE fixups and incremental admission passes QEMU; new image/heap guest gate pending. Target C/ICU dependencies remain'},
    {'area':'rendering_media','required':'WebCore/platform view, fonts/images, then decoded video/MSE with no audio output','current':'NetSurf UI and assets; no replacement engine or media pipeline'}]
out=root/'build/browser-engine-arm64/source-audit.json';out.write_text(json.dumps(record,indent=2))
print(json.dumps({'report':str(out),'files':len(record['files']),'wpe_packages':len(record['wpe_initial_required_packages'])}))
