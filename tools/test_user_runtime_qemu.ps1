param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$Distro='Ubuntu',[string]$Qemu='',[string]$Python='python',[switch]$TimerEdge,[switch]$TimerPageOffset,[switch]$FirmwareL2,[switch]$FirmwareL0,[switch]$Rtti,[switch]$WebkitVm,[switch]$WebkitRandom,[switch]$WebkitSync,[switch]$FeatureMain,[switch]$Libm,[switch]$JscSupport,[switch]$Stdio,[switch]$Jsc,[switch]$JscSpin,[switch]$WebcoreLoop,[switch]$UserHttp,[switch]$ResourceLoop,[switch]$WebcoreResource,[switch]$CairoRaster,[switch]$NativeFonts,[switch]$FontPaint,[switch]$FontSelect,[switch]$OpenSsl,[switch]$NativePage,[switch]$DebugMonitor,[switch]$JscFramePaced,[switch]$ReusePrograms,[string]$Cpu='cortex-a72',[switch]$RequireRndr)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 if($OpenSsl -and $RequireRndr){throw '-OpenSsl uses test-owned entropy to validate unavailable/available states; run -RequireRndr separately'}
 if($WebcoreResource -and (!$Jsc -or !$UserHttp)){throw '-WebcoreResource requires -Jsc -UserHttp'}
 if($ResourceLoop -and (!$Jsc -or !$UserHttp)){throw '-ResourceLoop requires -Jsc -UserHttp'}
 if($WebcoreLoop -and !$Jsc){throw '-WebcoreLoop requires -Jsc'}
 if($JscSpin -and !$Jsc){throw '-JscSpin requires -Jsc'}
 if($JscFramePaced -and !$Jsc){throw '-JscFramePaced requires -Jsc'}
 $output='build/user-runtime';New-Item -ItemType Directory -Force $output | Out-Null
 & $Python tools/generate_user_vectors.py --check
 if($LASTEXITCODE){throw 'Stale EL0 assembly'}
 if($ReusePrograms){
  & $Python tools/user_program_build_stamp.py --verify --llvm $LLVM
 }else{
  & (Join-Path $PSScriptRoot 'build_user_programs.ps1') -LLVM $LLVM -Python $Python
  if($LASTEXITCODE){throw 'Base ELF build failed'}
  & $Python tools/user_program_build_stamp.py --write --llvm $LLVM
 }
 if($LASTEXITCODE){throw 'Base ELF verification failed'}
 if(([int][bool]$FontPaint+[int][bool]$NativeFonts+[int][bool]$FontSelect) -gt 1){throw 'Select one of -FontSelect, -FontPaint or -NativeFonts'}
 $embedArgs=@();if($FontSelect){$embedArgs+='--font-select'}
 if($FontPaint){$embedArgs+='--font-paint'}
 if($NativeFonts){$embedArgs+='--native-fonts'}
 if($CairoRaster){$embedArgs+='--cairo-raster'}
 if($OpenSsl){$embedArgs+='--openssl'}
 if($NativePage){$embedArgs+='--native-page'}
 if($Rtti){$embedArgs+='--rtti'}
 if($WebcoreResource){$embedArgs+='--webcore-resource'}
 if($WebkitVm){$embedArgs+='--webkit-vm'}
 if($WebkitRandom){$embedArgs+='--webkit-random'}
 if($WebkitSync){$embedArgs+='--webkit-sync'}
 if($FeatureMain){$embedArgs+='--feature-main'}
 if($Libm){$embedArgs+='--libm'}
 if($JscSupport){$embedArgs+='--jsc-support'}
 if($Stdio){$embedArgs+='--stdio'}
 if($Jsc){$embedArgs+='--jsc'}
 if($JscSpin){$embedArgs+='--jsc-spin'}
 if($WebcoreLoop){$embedArgs+='--webcore-loop'}
 if($UserHttp){$embedArgs+='--user-http'}
 if($ResourceLoop){$embedArgs+='--resource-loop'}
 & $Python tools/embed_user_programs.py @embedArgs
 if($LASTEXITCODE){throw 'ELF fixture embedding failed'}
 $objects=@();$cc=Join-Path $LLVM 'clang.exe'
 $flags=@('-target','aarch64-none-elf','-march=armv8-a','-mstrict-align','-ffreestanding','-fno-builtin','-fno-stack-protector','-O2','-g','-Ikernel/include','-Iinclude','-Ibuild/user-runtime','-Ithird_party/BearSSL/inc','-Ithird_party/BearSSL/src')
 if($TimerEdge){$flags+='-DUSER_TEST_TIMER_FLAGS=1'}
 if($TimerPageOffset){$flags+='-DUSER_TEST_GICR_BASE=0x080ae000'}
 if($FirmwareL2){$flags+='-DUSER_TEST_FIRMWARE_L2=1'}
 if($FirmwareL0){$flags+='-DUSER_TEST_FIRMWARE_L0=1'}
 if($RequireRndr){$flags+='-DUSER_TEST_REQUIRE_RNDR=1'}
 if($OpenSsl){$flags+='-DDIHOS_ENTROPY_TEST=1'}
 if($Jsc -or $NativePage){$flags+='-DUSER_TEST_ARENA_PAGES=65536'}
 if($JscFramePaced){$flags+='-DUSER_JSC_FRAME_PACED=1'}
 foreach($source in @('tools/user_runtime_qemu_start.S','build/user-runtime/user_program_blobs.S','tools/user_runtime_qemu_tests.c','kernel/asm/aa64/aa64_user.c','kernel/asm/aa64/aa64_user_slice.c','kernel/asm/aa64/aa64_user_timer.c','kernel/memory/aarch64_user_vm.c','kernel/process/dihos_process.c','kernel/process/dihos_user_elf.c','kernel/process/dihos_user_sacx.c','kernel/process/dihos_user_window.c','kernel/process/dihos_browser_bridge.c','kernel/process/dihos_user_memory.c','kernel/process/dihos_user_runtime.c')){
  $object=Join-Path $output ((Split-Path $source -Leaf)+'.o');& $cc @flags -c $source -o $object
  if($LASTEXITCODE){throw "ARM64 test compilation failed: $source"};$objects+=$object
 }
 $elf=Join-Path $output 'user-runtime.elf'
 foreach($source in @('kernel/system/entropy.c','third_party/BearSSL/src/rand/hmac_drbg.c','third_party/BearSSL/src/mac/hmac.c','third_party/BearSSL/src/hash/sha2small.c','third_party/BearSSL/src/codec/dec32be.c','third_party/BearSSL/src/codec/enc32be.c')){
  $object=Join-Path $output ((Split-Path $source -Leaf)+'.o');& $cc @flags -c $source -o $object
  if($LASTEXITCODE){throw "Entropy kernel compilation failed: $source"};$objects+=$object
 }
 & (Join-Path $LLVM 'ld.lld.exe') '-T' tools/user_runtime_qemu.ld -nostdlib @objects -o $elf
 if($LASTEXITCODE){throw 'ARM64 test link failed'}
 $environment=@()
 if(!$Qemu){
  if(Test-Path build/user-runtime/qemu-root/usr/bin/qemu-system-aarch64){
   $Qemu='./build/user-runtime/qemu-root/usr/bin/qemu-system-aarch64'
   $environment=@('env','LD_LIBRARY_PATH=./build/user-runtime/qemu-root/usr/lib/aarch64-linux-gnu')
  }else{$Qemu='qemu-system-aarch64'}
 }
 # The full suite embeds multiple independent engine ELF files alongside its
 # unchanged 256 MiB test allocator. Their combined kernel image exceeds 512M.
 $testMemory=if($NativePage){'1G'}elseif($WebcoreResource){'1G'}elseif($Jsc){'512M'}else{'256M'};$testTimeout=if($Jsc){'120s'}else{'60s'}
 if($JscFramePaced -or $NativePage){$testTimeout='180s'}
 if($DebugMonitor){$testTimeout='600s'}
 $args=@('-d',$Distro,'--cd',$root,'--')+$environment+@('timeout',$testTimeout,$Qemu,'-machine','virt,secure=off,virtualization=off,gic-version=3','-cpu',$Cpu,'-m',$testMemory,'-smp','1','-nodefaults','-nographic','-serial','stdio','-monitor','none','-semihosting-config','enable=on,target=native','-no-reboot','-kernel','./build/user-runtime/user-runtime.elf')
 if($DebugMonitor){$args+=@('-monitor','unix:/tmp/dihos-user-runtime-debug.sock,server=on,wait=off')}
 & wsl.exe @args *> (Join-Path $output 'qemu.log');$code=$LASTEXITCODE
 if($NativePage){Get-Content (Join-Path $output 'qemu.log') | Where-Object {$_ -notmatch '^Page: PIX:'}}
 else{Get-Content (Join-Path $output 'qemu.log')}
 if($code){throw "ARM64 QEMU integration failed: $code"}
 if(!(Select-String -LiteralPath (Join-Path $output 'qemu.log') -SimpleMatch 'PASS: sparse quota/slot bounds' -Quiet)){throw 'ARM64 QEMU exited before completing the suite'}
}finally{Pop-Location}
