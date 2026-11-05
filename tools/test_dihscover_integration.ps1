param([string]$LLVM='C:\Program Files\LLVM\bin',[string]$Output='build/dihscover-integration-validation/host')
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 New-Item -ItemType Directory -Force $Output | Out-Null
 $cc=Join-Path $LLVM 'clang.exe';$cxx=Join-Path $LLVM 'clang++.exe'
 function Check([string]$name,[string]$program,[string[]]$arguments){
  & $program @arguments *> (Join-Path $Output ($name+'.log'))
  if($LASTEXITCODE){throw "$name failed: $Output/$name.log"}
 }
 $inc=@('-Ikernel/include','-iquote','include')
 $bridge=Join-Path $Output 'bridge.obj'
 Check 'compile-bridge' $cc (@('-std=c11','-O2')+$inc+@('-c','kernel/process/dihos_browser_bridge.c','-o',$bridge))
 foreach($test in @('browser_bridge_tests.c','browser_session_lifecycle_tests.cpp')){
  $name=[IO.Path]::GetFileNameWithoutExtension($test);$exe=Join-Path $Output ($name+'.exe')
  $compiler=if($test.EndsWith('.cpp')){$cxx}else{$cc};$standard=if($test.EndsWith('.cpp')){'-std=c++17'}else{'-std=c11'}
  Check ('compile-'+$name) $compiler (@($standard,'-O2','-D_CRT_SECURE_NO_WARNINGS')+$inc+@(('tools/'+$test),$bridge,'-o',$exe))
  Check $name (Join-Path $root $exe) @()
 }
 $layout=Join-Path $Output 'layout.obj'
 Check 'compile-layout-kernel' $cc (@('-std=c11','-O2')+$inc+@('-c','tools/browser_api_layout_kernel.c','-o',$layout))
 $exe=Join-Path $Output 'layout.exe'
 Check 'compile-layout' $cxx @('-std=c++17','-O2','tools/browser_api_layout_tests.cpp',$layout,'-o',$exe)
 Check 'layout' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'ui.exe'
 Check 'compile-ui' $cxx @('-std=c++17','-O2','-D_CRT_SECURE_NO_WARNINGS','-DDIHSCOVER_WEBCORE','-Isdk/sacx/include','-Ithird_party/netsurf-all-3.11/netsurf/include','tools/browser_existing_ui_tests.cpp','sdk/sacx/apps/dihscover/preferences.cpp','-o',$exe)
 Check 'ui' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'loader.exe'
 Check 'compile-loader' $cc (@('-std=c11','-O2','-D_CRT_SECURE_NO_WARNINGS','-Ibuild/user-runtime')+$inc+@('tools/user_loader_tests.c','kernel/process/dihos_user_loader.c','-o',$exe))
 Check 'loader' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'sacx.exe'
 Check 'compile-sacx' $cc (@('-std=c11','-O2')+$inc+@('tools/user_sacx_tests.c','kernel/process/dihos_user_sacx.c','-o',$exe))
 Check 'sacx' (Join-Path $root $exe) @()
 $exe=Join-Path $Output 'window.exe'
 Check 'compile-window' $cc (@('-std=c11','-O2')+$inc+@('tools/user_window_tests.c','kernel/process/dihos_user_window.c','-o',$exe))
 Check 'window' (Join-Path $root $exe) @()
 foreach($name in @('browser_bridge_tests','browser_session_lifecycle_tests','layout','ui','loader','sacx','window')){Get-Content (Join-Path $Output ($name+'.log'))}
 Write-Output 'PASS: Dihscover existing frontend, isolated renderer bridge, ownership, lifecycle, ABI and loader checks'
} finally {Pop-Location}
