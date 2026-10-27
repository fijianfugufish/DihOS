param([switch]$Fonts,[string]$Python='python',[string]$LLVM='C:\Program Files\LLVM\bin')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  $output=Join-Path $root 'build\browser-tests'
  New-Item -ItemType Directory -Force $output | Out-Null
  $cc=Join-Path $LLVM 'clang.exe';$cxx=Join-Path $LLVM 'clang++.exe'
  $inc=@('-Ikernel/include','-iquote','include')
  function Compile([string]$Compiler,[string[]]$Arguments) { & $Compiler @Arguments;if($LASTEXITCODE){throw "Browser test compilation failed: $($Arguments -join ' ')"} }
  function Run([string]$File) { & $File;if($LASTEXITCODE){throw "Browser test failed: $File"} }
  $fiber=Join-Path $output 'kfiber.obj'
  Compile $cc (@('-std=c11','-O2')+$inc+@('-c','kernel/system/kfiber.c','-o',$fiber))
  $exe=Join-Path $output 'fiber.exe'
  Compile $cc (@('-std=c11','-O2')+$inc+@('tools/kfiber_tests.c',$fiber,'-o',$exe));Run $exe
  $exe=Join-Path $output 'launch.exe'
  Compile $cxx (@('-std=c++17','-O2')+$inc+@('tools/browser_launch_tests.cpp',$fiber,'-o',$exe));Run $exe
  $exe=Join-Path $output 'cursor.exe'
  Compile $cxx @('-std=c++17','-O2','tools/browser_cursor_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'mouse.exe'
  Compile $cxx @('-std=c++17','-O2','-Ithird_party/netsurf-all-3.11/netsurf/include','tools/browser_mouse_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'panic.exe'
  Compile $cxx @('-std=c++17','-O2','tools/browser_panic_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'numbers.exe'
  Compile $cxx @('-std=c++17','-O2','tools/browser_numbers_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'http.exe'
  Compile $cc (@('-std=c11','-O2')+$inc+@('tools/http_stream_tests.c','kernel/net/http_stream.c','-o',$exe));Run $exe
  $exe=Join-Path $output 'receive.exe'
  Compile $cxx @('-std=c++17','-O2','tools/browser_receive_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'network.exe'
  Compile $cxx (@('-std=c++17','-O2')+$inc+@('tools/sacx_network_service_tests.cpp',$fiber,'-o',$exe));Run $exe
  $infinite=Join-Path $output 'infinite.obj'
  Compile $cc @('-O2','-fsanitize-coverage=trace-pc,bb,no-prune','-c','tools/browser_infinite_fixture.c','-o',$infinite)
  $exe=Join-Path $output 'preemption.exe'
  Compile $cxx (@('-std=c++17','-O2')+$inc+@('tools/browser_preemption_tests.cpp',$fiber,$infinite,'-o',$exe));Run $exe
  $exe=Join-Path $output 'plot.exe'
  Compile $cxx @('-std=c++17','-O2','-Ithird_party/netsurf-all-3.11/netsurf','-Ithird_party/netsurf-all-3.11/netsurf/include','-Ithird_party/netsurf-all-3.11/libwapcaplet/include','tools/browser_plot_tests.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'clock.exe'
  Compile $cxx @('-std=c++17','-O2','-Isdk/sacx/include','tools/browser_clock_tests.cpp','-o',$exe);Run $exe
  $heap=Join-Path $output 'heap.obj';$tlsf=Join-Path $output 'tlsf.obj'
  Compile $cxx @('-std=c++17','-O2','-Isdk/sacx/include','-Ithird_party/tlsf','-Dmalloc=browser_test_malloc','-Dfree=browser_test_free','-Dcalloc=browser_test_calloc','-Drealloc=browser_test_realloc','-c','sdk/sacx/apps/dihscover/heap.cpp','-o',$heap)
  Compile $cc @('-std=c11','-O2','-c','third_party/tlsf/tlsf.c','-o',$tlsf)
  $exe=Join-Path $output 'heap.exe'
  Compile $cxx @('-std=c++17','-O2','-Isdk/sacx/include','tools/browser_heap_tests.cpp',$heap,$tlsf,'-o',$exe);Run $exe
  $exe=Join-Path $output 'profile.exe'
  Compile $cxx @('-std=c++17','-O2','-Isdk/sacx/include','tools/browser_profile_tests.cpp','sdk/sacx/apps/dihscover/scheduling.cpp','sdk/sacx/apps/dihscover/profiling.cpp','-o',$exe);Run $exe
  $exe=Join-Path $output 'fetch.exe'
  Compile $cxx @('-std=c++17','-O2','-Isdk/sacx/include','-Ithird_party/netsurf-all-3.11/netsurf','-Ithird_party/netsurf-all-3.11/netsurf/include','-Ithird_party/netsurf-all-3.11/libwapcaplet/include','tools/browser_fetch_tests.cpp','-o',$exe);Run $exe
  & $Python tools/generate_sacx_api_guards.py --check;if($LASTEXITCODE){throw 'Stale SACX scheduling guards'}
  & $Python tools/generate_browser_resources.py --check;if($LASTEXITCODE){throw 'Stale NetSurf resources'}
  & $Python tools/verify_browser_dependencies.py;if($LASTEXITCODE){throw 'Browser dependency verification failed'}
  & "$PSScriptRoot\test_sacx_build_cache.ps1" -Clang $cxx
  if($Fonts){& "$PSScriptRoot\test_browser_fonts.ps1" -Clang $cc -ClangXX $cxx}
} finally { Pop-Location }
