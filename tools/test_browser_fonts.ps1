param([string]$Clang='C:\Program Files\LLVM\bin\clang.exe',[string]$ClangXX='C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  $dir=Join-Path $root 'build\browser-font-tests'
  New-Item -ItemType Directory -Force $dir | Out-Null
  $ft=@('base/ftbitmap.c','base/ftbase.c','base/ftinit.c','base/ftsystem.c','base/ftdebug.c','base/ftglyph.c','base/ftmm.c','base/ftsynth.c','autofit/autofit.c','truetype/truetype.c','cff/cff.c','psaux/psaux.c','psnames/psnames.c','pshinter/pshinter.c','sfnt/sfnt.c','smooth/smooth.c','raster/raster.c')
  $objects=@()
  foreach($source in $ft) {
    $obj=Join-Path $dir (([IO.Path]::GetFileNameWithoutExtension($source))+'.obj')
    & $Clang -O1 -DFT2_BUILD_LIBRARY -D_CRT_SECURE_NO_WARNINGS '-Isdk/sacx/apps/dihscover/port' '-Ithird_party/freetype/include' -c "third_party/freetype/src/$source" -o $obj
    if($LASTEXITCODE){throw "Font compilation failed: $source"};$objects+=$obj
  }
  $hb=Join-Path $dir 'harfbuzz.obj'
  & $ClangXX -std=c++17 -O1 -DHB_NO_MT -DHB_NO_MMAP -DHB_NO_GETENV -DHB_NO_SETLOCALE -DHB_NO_ATEXIT -D_CRT_SECURE_NO_WARNINGS '-Ithird_party/harfbuzz/src' -c third_party/harfbuzz/src/harfbuzz.cc -o $hb
  if($LASTEXITCODE){throw 'HarfBuzz compilation failed'}
  $exe=Join-Path $dir 'fonts.exe'
  & $ClangXX -std=c++17 -O1 -D_CRT_SECURE_NO_WARNINGS '-Isdk/sacx/include' '-Ithird_party/freetype/include' '-Ithird_party/harfbuzz/src' '-Ithird_party/netsurf-all-3.11/netsurf/include' '-Ithird_party/netsurf-all-3.11/libwapcaplet/include' tools/browser_font_tests.cpp sdk/sacx/apps/dihscover/web_font.cpp $objects $hb -o $exe
  if($LASTEXITCODE){throw 'Font test link failed'}
  & $exe
  if($LASTEXITCODE){throw 'Font tests failed'}
} finally { Pop-Location }
