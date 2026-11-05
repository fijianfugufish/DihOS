param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$Output='build/user-http-header-tests')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  New-Item -ItemType Directory -Force $Output | Out-Null
  $cc=Join-Path $LLVM 'clang.exe';$cxx=Join-Path $LLVM 'clang++.exe'
  function Invoke-Check([string]$Name,[string]$Program,[string[]]$Arguments) {
    & $Program @Arguments *> (Join-Path $Output ($Name+'.log'))
    if($LASTEXITCODE){throw "$Name failed; inspect $Output/$Name.log"}
  }
  $inc=@('-Ikernel/include','-iquote','include')
  $fiber=Join-Path $Output 'fiber.obj'
  Invoke-Check 'compile-fiber' $cc (@('-std=c11','-O2')+$inc+@('-c','kernel/system/kfiber.c','-o',$fiber))
  $exe=Join-Path $Output 'network.exe'
  Invoke-Check 'compile-network' $cxx (@('-std=c++17','-O2')+$inc+@('tools/sacx_network_service_tests.cpp',$fiber,'-o',$exe))
  Invoke-Check 'network' (Join-Path $root $exe) @()
  $exe=Join-Path $Output 'sacx.exe'
  Invoke-Check 'compile-sacx' $cc (@('-std=c11','-O2')+$inc+@('tools/user_sacx_tests.c','kernel/process/dihos_user_sacx.c','-o',$exe))
  Invoke-Check 'sacx' (Join-Path $root $exe) @()
  $exe=Join-Path $Output 'headers.exe'
  Invoke-Check 'compile-headers' $cc @('-std=c11','-O2','-Ikernel/include','tools/http_request_headers_tests.c','-o',$exe)
  Invoke-Check 'headers' (Join-Path $root $exe) @()
  Invoke-Check 'compile-arm64-transport' $cc @('--target=aarch64-none-elf','-ffreestanding','-fno-builtin','-fPIE','-O2',
    '-DKERNEL_ARCH_AA64=1','-DDIHOS_ARCH_AARCH64=1','-DKERNEL_ARCH=1','-Ikernel/include','-Iinclude',
    '-Ithird_party/BearSSL/inc','-Ithird_party/BearSSL/src','-c','kernel/net/knet_usb.c','-o',(Join-Path $Output 'knet.o'))
  Get-Content (Join-Path $Output 'network.log'),(Join-Path $Output 'sacx.log'),(Join-Path $Output 'headers.log')
  Write-Output 'PASS: ARM64 transport compiles; host request, ownership, lifecycle, and syscall admission checks passed'
} finally { Pop-Location }
