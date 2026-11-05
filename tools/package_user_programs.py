"""Add the built development ELFs and their exact source recipe to a snapshot."""
from pathlib import Path
import hashlib,json,shutil,sys
root=Path(__file__).resolve().parents[1]
project=Path(sys.argv[1]).resolve()
record_path=project.parent/'source.json'
record=json.loads(record_path.read_text())
sources={}
recipe=['tools/user_program.c','tools/user_program.ld','tools/user_cpp_program.cpp','tools/user_cpp_program.ld','sdk/sacx/user_runtime/runtime.cpp','tools/user_allocator_program.cpp','sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp','sdk/sacx/user_runtime/include/dihos_user_allocator.h','sdk/sacx/user_runtime/include/dihos_user_tlsf.h','third_party/tlsf/tlsf.c','third_party/tlsf/tlsf.h','tools/build_user_programs.ps1','tools/package_user_programs.py','sdk/sacx/include/sacx_user_syscall.h','sdk/sacx/include/sacx_user_api.h','tools/user_library_program.cpp','tools/build_user_library.ps1','tools/prepare_user_libcxx.py','tools/user-libraries.lock.json']
recipe += [p.relative_to(root).as_posix() for p in (root/'sdk/sacx/user_runtime/libcxx').rglob('*') if p.is_file()]
recipe += ['tools/user_threads_program.cpp','tools/user_threads_program.ld','sdk/sacx/include/sacx_user_threads.h']
recipe += ['tools/user_lifetime_program.cpp','sdk/sacx/user_runtime/thread_lifetime.cpp','sdk/sacx/user_runtime/include/dihos_user_sync.h']
recipe += ['tools/user_sync_program.cpp','tools/build_user_sync.ps1','sdk/sacx/include/sacx_user_sync.hpp']
recipe += ['tools/user_stdthreads_program.cpp','tools/user_keys_program.cpp','sdk/sacx/user_runtime/thread_keys.cpp','sdk/sacx/include/sacx_user_keys.h']
recipe += ['tools/user_large_program.c']
recipe += ['tools/user_vm_program.cpp','sdk/sacx/user_runtime/virtual_memory.cpp','sdk/sacx/include/sacx_user_vm.h']
recipe += ['tools/user_random_program.cpp','sdk/sacx/include/sacx_user_random.h']
recipe += ['tools/user_sparse_program.c','sdk/sacx/include/sacx_user_sparse_vm.h']
recipe += ['tools/user_context_program.cpp','tools/user_platform_program.cpp','tools/user_calendar_program.cpp','tools/generate_user_calendar_vectors.py','tools/user_calendar_vectors.inc','sdk/sacx/include/sacx_user_thread_context.h','sdk/sacx/user_runtime/fenv.cpp','sdk/sacx/user_runtime/time.cpp','sdk/sacx/freestanding/include/time.h','sdk/sacx/freestanding/include/fenv.h','sdk/sacx/freestanding/include/stdlib.h','sdk/sacx/freestanding/include/stdio.h','sdk/sacx/freestanding/include/string.h','sdk/sacx/freestanding/include/sys/types.h']
for relative in recipe:
    source=root/relative;target=project/relative;target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
    sources[relative]=hashlib.sha256(source.read_bytes()).hexdigest()
    record['files'][relative]=sources[relative]
record['overlay_sha256']=hashlib.sha256(json.dumps(record['files'],sort_keys=True).encode()).hexdigest()
record_path.write_text(json.dumps(record,indent=2))
destination=project/'OS/System/Programs/UserRuntime'
destination.mkdir(parents=True,exist_ok=True)
programs={}
for name in ('vm','vm_guard','vm_decommit','vm_readonly','random','random_unready','context','context_cancel','platform','calendar','sparse','sparse_guard','sparse_decommit','sparse_readonly','sparse_spin','sparse_nx'):
    source=root/f'build/user-programs/{name}.elf';shutil.copy2(source,destination/source.name)
    programs[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
for name in ('exit','loop','fault','sacx','memory','memory_fault','cpp','arena','library','library_fail','threads','threads_fault','threads_wait','threads_oom','threads_bss','threads_spin','lifetime','lifetime_recursive','lifetime_limits','lifetime_overflow','sync','sync_invalid','stdthreads','stdthreads_limit','stdthreads_unjoined','stdthreads_detach','keys','large','large_fault'):
    source=root/f'build/user-programs/{name}.elf';shutil.copy2(source,destination/source.name)
    programs[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
(destination/'SOURCE.JSON').write_text(json.dumps({'format':'DihOS ARM64 static ELF prototype','sources':sources,'programs':programs},indent=2))
(destination/'Licenses').mkdir(exist_ok=True)
shutil.copy2(root/'third_party/tlsf/tlsf.h',destination/'Licenses/TLSF.txt')
shutil.copy2(root/'build/user-libcxx-22.1.0/libcxx/LICENSE.TXT',destination/'Licenses/LLVM-libcxx.txt')
shutil.copy2(root/'build/user-libcxx-22.1.0/SOURCE.JSON',destination/'LIBRARIES.JSON')
print(json.dumps({'project':str(project),'overlay_sha256':record['overlay_sha256'],'programs':programs}))
