param([string]$ProjectRoot = (Resolve-Path $PSScriptRoot).Path)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$llvm = Join-Path $Env:ProgramFiles "LLVM\bin"
if (Test-Path -LiteralPath (Join-Path $llvm "clang++.exe")) {
  $env:Path = "$llvm;$env:Path"
}

$out = Join-Path $ProjectRoot "build\dihscover_tests.exe"
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
& clang++.exe -std=c++17 -O2 `
  -I (Join-Path $ProjectRoot "sdk\sacx\include") `
  -I (Join-Path $ProjectRoot "sdk\sacx\apps\dihscover") `
  (Join-Path $ProjectRoot "tools\dihscover_tests.cpp") `
  (Join-Path $ProjectRoot "sdk\sacx\apps\dihscover\document.cpp") `
  (Join-Path $ProjectRoot "sdk\sacx\apps\dihscover\script.cpp") `
  (Join-Path $ProjectRoot "sdk\sacx\apps\dihscover\url.cpp") `
  -o $out
if ($LASTEXITCODE) { throw "Dihscover host tests failed to compile" }
& $out (Join-Path $ProjectRoot "tools\fixtures\wikipedia-wiki.html")
if ($LASTEXITCODE) { throw "Dihscover host tests failed" }
