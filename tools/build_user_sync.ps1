param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$OutputDirectory='build/user-programs',[string]$Python='python')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 $verified=& $Python tools/prepare_user_libcxx.py
 if($LASTEXITCODE){throw 'Pinned libc++ source verification failed'}
 $bundle=$verified | ConvertFrom-Json
 $include=(Resolve-Path -LiteralPath (Join-Path $bundle.bundle 'libcxx/include')).Path
 $config=Join-Path $root 'build/user-libcxx-sync-config'
 & cmake -S sdk/sacx/user_runtime/libcxx -B $config "-DLIBCXX_INCLUDE=$include" -DDIHOS_SYNCHRONIZATION=ON
 if($LASTEXITCODE){throw 'libc++ synchronization configuration failed'}
 $flags=@('-target','aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin','-fno-stack-protector','-fpie','-fno-exceptions','-fno-rtti','-ftls-model=local-exec','-fno-emulated-tls','-std=c++23','-O2','-nostdinc++','-D_LIBCPP_AVAILABILITY_MINIMUM_HEADER_VERSION=22','-Isdk/sacx/include','-Isdk/sacx/user_runtime/include','-isystem',$config,'-isystem',$include,'-idirafter','sdk/sacx/user_runtime/libcxx/c','-idirafter','sdk/sacx/freestanding/include')
 $flags+="-ffile-prefix-map=$root=."
 $support=@()
 foreach($source in @('platform.cpp','synchronization.cpp','threading.cpp')){
  $object=Join-Path $OutputDirectory ('sync-'+$source+'.o')
  & (Join-Path $LLVM 'clang++.exe') @flags -c (Join-Path 'sdk/sacx/user_runtime/libcxx' $source) -o $object
  if($LASTEXITCODE){throw 'Synchronization platform compilation failed'};$support+=$object
 }
 $runtime=@('runtime.cpp.o','thread_lifetime.cpp.o','thread_keys.cpp.o','allocator.cpp.o','allocator_platform.cpp.o','tlsf-user.o') | ForEach-Object {Join-Path $OutputDirectory $_}
 $names=@('sync','sync_invalid')
 for($i=0;$i -lt $names.Count;$i++){
  $object=Join-Path $OutputDirectory ($names[$i]+'-main.o')
  & (Join-Path $LLVM 'clang++.exe') @flags "-DUSER_SYNC_KIND=$($i+1)" -c tools/user_sync_program.cpp -o $object
  if($LASTEXITCODE){throw 'Synchronization fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --gc-sections --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_threads_program.ld @runtime @support $object -o (Join-Path $OutputDirectory ($names[$i]+'.elf'))
  if($LASTEXITCODE){throw 'Synchronization fixture link failed'}
 }
 $threadNames=@('stdthreads','stdthreads_limit','stdthreads_unjoined','stdthreads_detach','keys')
 for($i=0;$i -lt $threadNames.Count;$i++){
  $object=Join-Path $OutputDirectory ($threadNames[$i]+'-main.o')
  $source=if($threadNames[$i] -eq 'keys'){'tools/user_keys_program.cpp'}else{'tools/user_stdthreads_program.cpp'}
  & (Join-Path $LLVM 'clang++.exe') @flags "-DUSER_STDTHREAD_KIND=$($i+1)" -c $source -o $object
  if($LASTEXITCODE){throw 'Standard thread/key fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --gc-sections --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_threads_program.ld @runtime @support $object -o (Join-Path $OutputDirectory ($threadNames[$i]+'.elf'))
  if($LASTEXITCODE){throw 'Standard thread/key fixture link failed'}
 }
}finally{Pop-Location}
