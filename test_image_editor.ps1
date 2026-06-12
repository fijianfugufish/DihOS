param(
  [string]$ProjectRoot = "."
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$llvm = Join-Path $Env:ProgramFiles "LLVM\bin"
if (Test-Path (Join-Path $llvm "clang++.exe")) {
  $env:Path = "$llvm;$env:Path"
}

$outDir = Join-Path $ProjectRoot "build\tests"
$testExe = Join-Path $outDir ("image_editor_tests_{0}.exe" -f [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

& clang++.exe `
  "-std=c++17" "-O2" "-Wall" "-Wextra" "-Wno-missing-field-initializers" "-Wno-missing-braces" `
  "-D_CRT_SECURE_NO_WARNINGS" `
  "-I" (Join-Path $ProjectRoot "sdk\sacx\include") `
  "-I" (Join-Path $ProjectRoot "sdk\sacx\apps\image_viewer") `
  (Join-Path $ProjectRoot "tools\image_editor_tests.cpp") `
  (Join-Path $ProjectRoot "sdk\sacx\apps\image_viewer\document.cpp") `
  (Join-Path $ProjectRoot "sdk\sacx\apps\image_viewer\project.cpp") `
  "-o" $testExe
if ($LASTEXITCODE) {
  throw "image editor test build failed"
}

& $testExe
if ($LASTEXITCODE) {
  throw "image editor tests failed"
}
