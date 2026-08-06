param(
  [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path,
  [ValidateSet("Release","Debug")][string]$Config = "Release",
  [switch]$BuildX64
)

$sources = @(
  "sdk\sacx\apps\dihscover\main.cpp",
  "sdk\sacx\apps\dihscover\document.cpp",
  "sdk\sacx\apps\dihscover\url.cpp",
  "sdk\sacx\apps\dihscover\script.cpp",
  "sdk\sacx\apps\dihscover\runtime.cpp",
  "sdk\sacx\apps\dihscover\heap.cpp",
  "third_party\tlsf\tlsf.c"
)

$args = @{
  ProjectRoot = $ProjectRoot
  Source = $sources
  OutSacx = "build\sacx\Dihscover.sacx"
  OutElfAA64 = "build\sacx\Dihscover_aa64.elf"
  OutElfX64 = "build\sacx\Dihscover_x64.elf"
  Imports = "sdk\sacx\imports\default.imports.txt"
  Config = $Config
  IncludeDir = @("sdk\sacx\freestanding\include", "third_party\tlsf")
  Define = @("NDEBUG", "DIHSCOVER_FREESTANDING")
}
if ($BuildX64) { $args.BuildX64 = $true }

& (Join-Path $ProjectRoot "build_sacx_app.ps1") @args
if ($LASTEXITCODE) { throw "Dihscover SACX build failed" }
