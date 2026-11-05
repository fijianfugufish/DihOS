param([string]$LLVM='C:\Program Files\LLVM\bin')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $root
try {
 New-Item -ItemType Directory -Force build/user-http | Out-Null
 foreach($name in @('http','https')) {
  $extra=@();if($name -eq 'https'){$extra+='-DHTTP_FIXTURE_URL="https://example.com/"'}
  & "$LLVM\clang.exe" --target=aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fpie -fno-stack-protector -O2 -Isdk/sacx/include @extra -c tools/webcore_dihos/http_fixture.c -o "build/user-http/$name.o"
  if($LASTEXITCODE){throw 'HTTP fixture compile failed'}
  # Request descriptors may contain compiler-generated relative pointers.
  # Retain validated PIE relocations without advertising an empty TLS segment.
  & "$LLVM\ld.lld.exe" -pie --no-dynamic-linker -Bsymbolic -z max-page-size=4096 -T tools/webcore_dihos/http_fixture.ld "build/user-http/$name.o" -o "build/user-http/$name.elf"
  if($LASTEXITCODE){throw 'HTTP fixture link failed'}
 }
}finally{Pop-Location}
