param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$OutputDirectory='build/user-programs',[string]$Python='python')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 $verified=& $Python tools/prepare_user_libcxx.py
 if($LASTEXITCODE){throw 'Pinned libc++ source verification failed'}
 $bundle=$verified | ConvertFrom-Json
 $include=(Resolve-Path -LiteralPath (Join-Path $bundle.bundle 'libcxx/include')).Path
 $config=Join-Path $root 'build/user-libcxx-config'
 & cmake -S sdk/sacx/user_runtime/libcxx -B $config "-DLIBCXX_INCLUDE=$include"
 if($LASTEXITCODE){throw 'libc++ header configuration failed'}
 $flags=@('-target','aarch64-none-elf','-march=armv8-a','-ffreestanding','-fno-builtin','-fno-stack-protector','-fpie','-fno-exceptions','-fno-rtti','-ftls-model=local-exec','-fno-emulated-tls','-std=c++23','-O2','-nostdinc++','-D_LIBCPP_AVAILABILITY_MINIMUM_HEADER_VERSION=22','-Isdk/sacx/include','-Isdk/sacx/user_runtime/include','-isystem',$config,'-isystem',$include,'-idirafter','sdk/sacx/user_runtime/libcxx/c','-idirafter','sdk/sacx/freestanding/include')
 $flags+="-ffile-prefix-map=$root=."
 $platform=Join-Path $OutputDirectory 'libcxx-platform.o'
 & (Join-Path $LLVM 'clang++.exe') @flags -c sdk/sacx/user_runtime/libcxx/platform.cpp -o $platform
 if($LASTEXITCODE){throw 'libc++ platform compilation failed'}
 $cSupport=Join-Path $OutputDirectory 'libcxx-c-support.o'
 & (Join-Path $LLVM 'clang++.exe') @flags -c sdk/sacx/user_runtime/libcxx/c_support.cpp -o $cSupport
 if($LASTEXITCODE){throw 'libc++ C support compilation failed'}
 $libraryObjects=@()
 foreach($source in @('algorithm.cpp','string.cpp')){
  $object=Join-Path $OutputDirectory ('libcxx-'+$source+'.o')
  & (Join-Path $LLVM 'clang++.exe') @flags -ffunction-sections -fdata-sections -c (Join-Path (Split-Path $include -Parent) "src/$source") -o $object
  if($LASTEXITCODE){throw 'libc++ upstream source compilation failed'};$libraryObjects+=$object
 }
 $archive=Join-Path $OutputDirectory 'libdihos-libcxx.a'
 & (Join-Path $LLVM 'llvm-ar.exe') rcs $archive @libraryObjects
 if($LASTEXITCODE){throw 'libc++ archive creation failed'}
 $runtime=@('runtime.cpp.o','thread_lifetime.cpp.o','thread_keys.cpp.o','allocator.cpp.o','allocator_platform.cpp.o','tlsf-user.o') | ForEach-Object {Join-Path $OutputDirectory $_}
 foreach($name in @('library','library_fail')){
  $object=Join-Path $OutputDirectory ($name+'-main.o');$extra=@()
  if($name -eq 'library_fail'){$extra+='-DDIHOS_LIBRARY_FAILURE_FIXTURE'}
  & (Join-Path $LLVM 'clang++.exe') @flags @extra -c tools/user_library_program.cpp -o $object
  if($LASTEXITCODE){throw 'libc++ fixture compilation failed'}
  & (Join-Path $LLVM 'ld.lld.exe') -pie --gc-sections --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/user_cpp_program.ld @runtime $platform $cSupport $object $archive -o (Join-Path $OutputDirectory ($name+'.elf'))
  if($LASTEXITCODE){throw 'libc++ target link failed'}
 }
} finally {Pop-Location}
