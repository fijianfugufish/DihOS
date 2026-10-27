param([string]$Clang = 'C:\Program Files\LLVM\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\hyperv_protocol_tests.exe'
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
& $Clang '-std=c11' '-Wall' '-Wextra' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_protocol_tests.c') (Join-Path $root 'kernel\hyperv\hv_hid.c') '-o' $exe
if ($LASTEXITCODE) { throw 'Hyper-V host test compilation failed' }
& $exe
if ($LASTEXITCODE) { throw 'Hyper-V host tests failed' }
$buttons = Join-Path $root 'build\hyperv_button_tests.exe'
& $Clang '-std=c11' '-Wall' '-Wextra' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_button_tests.c') '-o' $buttons
if ($LASTEXITCODE) { throw 'Button host test compilation failed' }
& $buttons
if ($LASTEXITCODE) { throw 'Button host tests failed' }
$video = Join-Path $root 'build\hyperv_video_tests.exe'
& $Clang '-std=c11' '-Wall' '-Wextra' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_video_tests.c') '-o' $video
if ($LASTEXITCODE) { throw 'Video host test compilation failed' }
& $video
if ($LASTEXITCODE) { throw 'Video host tests failed' }
$channels = Join-Path $root 'build\hyperv_channel_tests.exe'
& $Clang '-std=c11' '-Wall' '-Wextra' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_channel_tests.c') '-o' $channels
if ($LASTEXITCODE) { throw 'Channel host test compilation failed' }
& $channels
if ($LASTEXITCODE) { throw 'Channel host tests failed' }
foreach ($name in @('storage','memory','clock')) {
  $test = Join-Path $root "build\hyperv_${name}_tests.exe"
  & $Clang '-std=c11' '-Wall' '-Wextra' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root "tools\hyperv_${name}_tests.c") '-o' $test
  if ($LASTEXITCODE) { throw "$name host test compilation failed" }
  & $test
  if ($LASTEXITCODE) { throw "$name host tests failed" }
}
$terminals = Join-Path $root 'build\hyperv_terminal_tests.exe'
& $Clang '-std=c++17' '-Wall' '-Wextra' (Join-Path $root 'tools\hyperv_terminal_tests.cpp') '-o' $terminals
if ($LASTEXITCODE) { throw 'Terminal allocation test compilation failed' }
& $terminals
if ($LASTEXITCODE) { throw 'Terminal allocation tests failed' }
$renderer = Join-Path $root 'build\hyperv_render_tests.exe'
& $Clang '-std=c11' '-Wno-unused-function' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_render_tests.c') '-o' $renderer
if ($LASTEXITCODE) { throw 'CPU renderer test compilation failed' }
& $renderer
if ($LASTEXITCODE) { throw 'CPU renderer regression tests failed' }
$dhcp = Join-Path $root 'build\hyperv_dhcp_tests.exe'
& $Clang '-std=c11' '-O1' "-I$root\kernel\include" "-I$root\third_party\BearSSL\inc" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_dhcp_tests.c') '-o' $dhcp
if ($LASTEXITCODE) { throw 'Automatic DHCP test compilation failed' }
& $dhcp
if ($LASTEXITCODE) { throw 'Automatic DHCP tests failed' }
$tls = Join-Path $root 'build\hyperv_tls_tests.exe'
& $Clang '-std=c11' '-O1' "-I$root\kernel\include" "-I$root\third_party\BearSSL\inc" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_tls_tests.c') '-o' $tls
if ($LASTEXITCODE) { throw 'TLS transport test compilation failed' }
& $tls
if ($LASTEXITCODE) { throw 'TLS transport tests failed' }
$wallpaper = Join-Path $root 'build\hyperv-assets\OS\System\Images\bgpaper.jpg'
if (Test-Path -LiteralPath $wallpaper) {
  $test = Join-Path $root 'build\hyperv_wallpaper_tests.exe'
  & $Clang '-std=c11' '-D_CRT_SECURE_NO_WARNINGS' "-I$root\kernel\include" '-iquote' "$root\include" (Join-Path $root 'tools\hyperv_wallpaper_tests.c') '-o' $test
  if ($LASTEXITCODE) { throw 'Wallpaper host test compilation failed' }
  & $test $wallpaper
  if ($LASTEXITCODE) { throw 'Wallpaper host tests failed' }
}
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'tools\hyperv.ps1'), [ref]$null, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
Write-Output 'PASS: deployment script syntax'
