param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$Output='build/browser-controls-tests')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 New-Item -ItemType Directory -Force $Output | Out-Null
 $cc=Join-Path $LLVM 'clang.exe';$cxx=Join-Path $LLVM 'clang++.exe'
 function Check([string]$Name,[string]$Program,[string[]]$Arguments){
  & $Program @Arguments *> (Join-Path $Output ($Name+'.log'))
  if($LASTEXITCODE){throw "$Name failed: $Output/$Name.log"}
 }
 $exe=Join-Path $Output 'controls.exe'
 Check 'compile-controls' $cxx @('-std=c++17','-O2','tools/browser_controls_tests.cpp','-o',$exe)
 Check 'controls' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'window.exe'
 Check 'compile-window' $cc @('-std=c11','-O2','-Ikernel/include','-iquote','include','tools/user_window_tests.c','kernel/process/dihos_user_window.c','-o',$exe)
 Check 'window' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'desktop-test.exe'
 Check 'compile-desktop-test' $cxx @('-std=c++17','-O2','-Ikernel/include','-iquote','include','tools/user_window_desktop_tests.cpp','-o',$exe)
 Check 'desktop-test' (Join-Path $root $exe) @()
 Check 'compile-arm64-desktop' $cxx @('--target=aarch64-none-elf','-ffreestanding','-fno-exceptions','-fno-rtti','-std=c++17','-Ikernel/include','-Iinclude','-c','kernel/apps/dihos_user_window_desktop.cpp','-o',(Join-Path $Output 'desktop.o'))
 Get-Content (Join-Path $Output 'controls.log'),(Join-Path $Output 'window.log'),(Join-Path $Output 'desktop-test.log')
 Write-Output 'PASS: browser controls and focused owned-window keyboard delivery'
} finally {Pop-Location}
