param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [Parameter(Mandatory = $true)]
  [string]$Source,

  [string]$OutSacx = "build\sacx\app.sacx",

  [string]$OutElfAA64 = "build\sacx\app_aa64.elf",

  [string]$OutElfX64 = "build\sacx\app_x64.elf",

  [string]$Imports = "",

  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

  [switch]$BuildX64,

  [switch]$RebuildPacker
)

$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$SourcePath = if ([System.IO.Path]::IsPathRooted($Source)) { $Source } else { Join-Path $ProjectRoot $Source }
$SourcePath = (Resolve-Path -LiteralPath $SourcePath).Path

$llvm = Join-Path $Env:ProgramFiles "LLVM\bin"
if (Test-Path (Join-Path $llvm "clang++.exe")) {
  $env:Path = "$llvm;$env:Path"
}

$clangxx = "clang++.exe"
$ld = "ld.lld"

$sdkInc = Join-Path $ProjectRoot "sdk\sacx\include"
$toolsDir = Join-Path $ProjectRoot "tools"
$packerCpp = Join-Path $toolsDir "sacx_pack.cpp"

$buildDir = Join-Path $ProjectRoot "build\sacx"
$objDir = Join-Path $buildDir "obj"

$packerExeStable = Join-Path $buildDir "clang_cache_host.exe"
$packerExe = $packerExeStable

$objAA64 = Join-Path $objDir "app_aa64.o"
$objX64 = Join-Path $objDir "app_x64.o"

$elfOutAA64 = if ([System.IO.Path]::IsPathRooted($OutElfAA64)) { $OutElfAA64 } else { Join-Path $ProjectRoot $OutElfAA64 }
$elfOutX64  = if ([System.IO.Path]::IsPathRooted($OutElfX64))  { $OutElfX64 }  else { Join-Path $ProjectRoot $OutElfX64 }

$sacxOut = if ([System.IO.Path]::IsPathRooted($OutSacx)) { $OutSacx } else { Join-Path $ProjectRoot $OutSacx }

New-Item -Force -ItemType Directory -Path $buildDir,$objDir | Out-Null
New-Item -Force -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($elfOutAA64)),([System.IO.Path]::GetDirectoryName($elfOutX64)),([System.IO.Path]::GetDirectoryName($sacxOut)) | Out-Null

$opt = if ($Config -eq "Release") { "-O2" } else { "-O0" }

Write-Host "== Compile AA64 object ==" -ForegroundColor Cyan
& $clangxx `
  "-target" "aarch64-unknown-none-elf" `
  "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
  "-fPIE" "-std=c++17" "-fno-exceptions" "-fno-rtti" `
  $opt "-g" `
  "-I" $sdkInc `
  "-c" $SourcePath "-o" $objAA64
if ($LASTEXITCODE) { throw "AA64 clang++ failed" }

Write-Host "== Link AA64 ELF ==" -ForegroundColor Cyan
& $ld "-pie" "-nostdlib" "-e" "sacx_main" "-o" $elfOutAA64 $objAA64
if ($LASTEXITCODE) { throw "AA64 ld.lld failed" }

if ($BuildX64)
{
  Write-Host "== Compile x64 object ==" -ForegroundColor Cyan
  & $clangxx `
    "-target" "x86_64-unknown-none-elf" `
    "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
    "-fPIE" "-std=c++17" "-fno-exceptions" "-fno-rtti" `
    $opt "-g" `
    "-I" $sdkInc `
    "-c" $SourcePath "-o" $objX64
  if ($LASTEXITCODE) { throw "x64 clang++ failed" }

  Write-Host "== Link x64 ELF ==" -ForegroundColor Cyan
  & $ld "-pie" "-nostdlib" "-e" "sacx_main" "-o" $elfOutX64 $objX64
  if ($LASTEXITCODE) { throw "x64 ld.lld failed" }
}

Write-Host "== Build sacx packer ==" -ForegroundColor Cyan
$needsPackerBuild = $RebuildPacker.IsPresent -or !(Test-Path $packerExeStable)

if ($needsPackerBuild)
{
  & $clangxx "-std=c++17" "-O2" "-static" $packerCpp "-o" $packerExeStable

  if ($LASTEXITCODE)
  {
    if (Test-Path $packerExeStable)
    {
      Write-Warning "Could not rebuild sacx packer; using existing binary."
    }
    else
    {
      throw "failed to build sacx packer"
    }
  }
}

Write-Host "== Pack .sacx ==" -ForegroundColor Cyan

$packerArgs = @(
  "--aa64", $elfOutAA64
)

if ($BuildX64)
{
  $packerArgs += @("--x64", $elfOutX64)
}

$packerArgs += @(
  "-o", $sacxOut
)

if ($Imports -and $Imports.Trim().Length -gt 0)
{
  $importsPath = if ([System.IO.Path]::IsPathRooted($Imports)) { $Imports } else { Join-Path $ProjectRoot $Imports }
  $importsPath = (Resolve-Path -LiteralPath $importsPath).Path

  $packerArgs += @(
    "--imports", $importsPath
  )
}

& $packerExe @packerArgs
if ($LASTEXITCODE) { throw "sacx packer failed" }

Write-Host "Built SACX app:" -ForegroundColor Green
Write-Host "  AA64 ELF : $elfOutAA64"

if ($BuildX64)
{
  Write-Host "  x64 ELF  : $elfOutX64"
}

Write-Host "  SACX     : $sacxOut"
