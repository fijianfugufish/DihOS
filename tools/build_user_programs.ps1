param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$OutputDirectory='build/user-programs',[string]$Python='python')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 & $Python tools/generate_user_calendar_vectors.py --check
 if($LASTEXITCODE){throw 'Stale calendar vectors'}
 New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
 $names=@('exit','loop','fault','sacx','memory','memory_fault')
 for($i=0;$i -lt $names.Count;$i++){
  $obj=Join-Path $OutputDirectory ($names[$i]+'.o');$elf=Join-Path $OutputDirectory ($names[$i]+'.elf')
  & (Join-Path $LLVM 'clang.exe') -target aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fno-stack-protector -fpie -O2 -Isdk/sacx/include "-DUSER_PROGRAM_KIND=$($i+1)" -c tools/user_program.c -o $obj
  if($LASTEXITCODE){throw 'User program compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_program.ld $obj -o $elf
  if($LASTEXITCODE){throw 'User program link failed'}
 }
 $cppFlags=@('-target','aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin','-fno-stack-protector','-fpie','-fno-exceptions','-fno-rtti','-ftls-model=local-exec','-fno-emulated-tls','-std=c++20','-O2','-Isdk/sacx/include','-Isdk/sacx/user_runtime/include','-Ithird_party/tlsf','-idirafter','sdk/sacx/user_runtime/libcxx/c','-idirafter','sdk/sacx/freestanding/include')
 $cppObjects=@()
 foreach($source in @('sdk/sacx/user_runtime/runtime.cpp','sdk/sacx/user_runtime/fenv.cpp','sdk/sacx/user_runtime/time.cpp','sdk/sacx/user_runtime/thread_lifetime.cpp','sdk/sacx/user_runtime/thread_keys.cpp','sdk/sacx/user_runtime/allocator.cpp','sdk/sacx/user_runtime/allocator_platform.cpp','sdk/sacx/user_runtime/virtual_memory.cpp')){
  $object=Join-Path $OutputDirectory ((Split-Path $source -Leaf)+'.o')
  & (Join-Path $LLVM 'clang++.exe') @cppFlags -c $source -o $object
  if($LASTEXITCODE){throw 'Freestanding C++ compilation failed'};$cppObjects+=$object
 }
 $tlsfObject=Join-Path $OutputDirectory 'tlsf-user.o'
 & (Join-Path $LLVM 'clang.exe') -target aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fno-stack-protector -fpie -O2 -DDIHOS_USER_TLSF -Isdk/sacx/user_runtime/include -c third_party/tlsf/tlsf.c -o $tlsfObject
 if($LASTEXITCODE){throw 'Freestanding TLSF compilation failed'};$cppObjects+=$tlsfObject
 foreach($name in @('cpp','arena','vm','vm_guard','vm_decommit','vm_readonly','random','random_unready','platform','calendar')){
  $object=Join-Path $OutputDirectory ($name+'-main.o')
  $source=if($name -eq 'cpp'){'tools/user_cpp_program.cpp'}elseif($name.StartsWith('vm')){'tools/user_vm_program.cpp'}elseif($name.StartsWith('random')){'tools/user_random_program.cpp'}elseif($name -eq 'platform'){'tools/user_platform_program.cpp'}elseif($name -eq 'calendar'){'tools/user_calendar_program.cpp'}else{'tools/user_allocator_program.cpp'}
  $extra=@();if($name.StartsWith('vm')){$extra+="-DUSER_VM_KIND=$(@('vm','vm_guard','vm_decommit','vm_readonly').IndexOf($name)+1)"}
  if($name -eq 'random_unready'){$extra+='-DUSER_RANDOM_UNREADY=1'}
  & (Join-Path $LLVM 'clang++.exe') @cppFlags @extra -c $source -o $object
  if($LASTEXITCODE){throw 'C++ fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_cpp_program.ld @cppObjects $object -o (Join-Path $OutputDirectory ($name+'.elf'))
  if($LASTEXITCODE){throw 'Freestanding C++ link failed'}
 }
 $threadNames=@('threads','threads_fault','threads_wait','threads_oom','threads_bss','threads_spin')
 foreach($name in @('context','context_cancel')){
  $object=Join-Path $OutputDirectory ($name+'-main.o');$extra=@()
  if($name -eq 'context_cancel'){$extra+='-DUSER_CONTEXT_CANCEL=1'}
  & (Join-Path $LLVM 'clang++.exe') @cppFlags @extra -c tools/user_context_program.cpp -o $object
  if($LASTEXITCODE){throw 'Thread context fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_threads_program.ld @cppObjects $object -o (Join-Path $OutputDirectory ($name+'.elf'))
  if($LASTEXITCODE){throw 'Thread context fixture link failed'}
 }
 for($i=0;$i -lt $threadNames.Count;$i++){
  $object=Join-Path $OutputDirectory ($threadNames[$i]+'-main.o')
  & (Join-Path $LLVM 'clang++.exe') @cppFlags -ftls-model=local-exec -fno-emulated-tls "-DUSER_THREAD_KIND=$($i+1)" -c tools/user_threads_program.cpp -o $object
  if($LASTEXITCODE){throw 'Thread fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_threads_program.ld @cppObjects $object -o (Join-Path $OutputDirectory ($threadNames[$i]+'.elf'))
  if($LASTEXITCODE){throw 'Thread fixture link failed'}
 }
 $lifetimeNames=@('lifetime','lifetime_recursive','lifetime_limits','lifetime_overflow')
 for($i=0;$i -lt $lifetimeNames.Count;$i++){
  $object=Join-Path $OutputDirectory ($lifetimeNames[$i]+'-main.o')
  & (Join-Path $LLVM 'clang++.exe') @cppFlags "-DUSER_LIFETIME_KIND=$($i+1)" -c tools/user_lifetime_program.cpp -o $object
  if($LASTEXITCODE){throw 'C++ lifetime fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_threads_program.ld @cppObjects $object -o (Join-Path $OutputDirectory ($lifetimeNames[$i]+'.elf'))
  if($LASTEXITCODE){throw 'C++ lifetime fixture link failed'}
 }
 & (Join-Path $PSScriptRoot 'build_user_library.ps1') -LLVM $LLVM -OutputDirectory $OutputDirectory -Python $Python
 & (Join-Path $PSScriptRoot 'build_user_sync.ps1') -LLVM $LLVM -OutputDirectory $OutputDirectory -Python $Python
 $largeNames=@('large','large_fault')
 for($i=0;$i -lt $largeNames.Count;$i++){
  $object=Join-Path $OutputDirectory ($largeNames[$i]+'-main.o')
  & (Join-Path $LLVM 'clang.exe') -target aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fno-stack-protector -fpie -O2 -std=gnu11 -Isdk/sacx/include "-DUSER_LARGE_KIND=$($i+1)" -c tools/user_large_program.c -o $object
  if($LASTEXITCODE){throw 'Large ELF compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_cpp_program.ld @cppObjects $object -o (Join-Path $OutputDirectory ($largeNames[$i]+'.elf'))
  if($LASTEXITCODE){throw 'Large ELF link failed'}
 }
 $sparseNames=@('sparse','sparse_guard','sparse_decommit','sparse_readonly','sparse_spin','sparse_nx')
 for($i=0;$i -lt $sparseNames.Count;$i++){
  $name=$sparseNames[$i];$object=Join-Path $OutputDirectory ($name+'-main.o')
  & (Join-Path $LLVM 'clang.exe') -target aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fno-stack-protector -fpie -O2 -std=gnu11 -Isdk/sacx/include "-DUSER_SPARSE_KIND=$($i+1)" -c tools/user_sparse_program.c -o $object
  if($LASTEXITCODE){throw 'Sparse ELF compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_program.ld $object -o (Join-Path $OutputDirectory ($name+'.elf'))
  if($LASTEXITCODE){throw 'Sparse ELF link failed'}
 }
}finally{Pop-Location}
