param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [Parameter(Mandatory = $true)]
  [string]$Source,

  [string]$OutSacx = "build\sacx\app.sacx",

  [string]$OutElf = "build\sacx\app.elf",

  [string]$Imports = "",

  [ValidateSet("Release","Debug")]
  [string]$Config = "Release",

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
$packerExeStable = Join-Path $buildDir "sacx_pack.exe"
$packerExe = $packerExeStable
$objOut = Join-Path $objDir "app.o"
$elfOut = if ([System.IO.Path]::IsPathRooted($OutElf)) { $OutElf } else { Join-Path $ProjectRoot $OutElf }
$sacxOut = if ([System.IO.Path]::IsPathRooted($OutSacx)) { $OutSacx } else { Join-Path $ProjectRoot $OutSacx }

New-Item -Force -ItemType Directory -Path $buildDir,$objDir | Out-Null
New-Item -Force -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($elfOut)),([System.IO.Path]::GetDirectoryName($sacxOut)) | Out-Null

$opt = if ($Config -eq "Release") { "-O2" } else { "-O0" }

Write-Host "== Compile AArch64 object ==" -ForegroundColor Cyan
& $clangxx `
  "-target" "aarch64-unknown-none-elf" `
  "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
  "-fPIE" "-std=c++17" "-fno-exceptions" "-fno-rtti" `
  $opt "-g" `
  "-I" $sdkInc `
  "-c" $SourcePath "-o" $objOut
if ($LASTEXITCODE) { throw "clang++ failed on $SourcePath" }

Write-Host "== Link ELF (ET_DYN PIE) ==" -ForegroundColor Cyan
& $ld "-pie" "-nostdlib" "-e" "sacx_main" "-o" $elfOut $objOut
if ($LASTEXITCODE) { throw "ld.lld failed" }

Write-Host "== Build sacx packer ==" -ForegroundColor Cyan
$needsPackerBuild = $RebuildPacker.IsPresent -or !(Test-Path $packerExeStable)

if ($needsPackerBuild) {
  & $clangxx "-std=c++17" $packerCpp "-o" $packerExeStable
  if ($LASTEXITCODE) {
    if (Test-Path $packerExeStable) {
      Write-Warning "Could not rebuild sacx packer; using existing binary."
    } else {
      throw "failed to build sacx packer"
    }
  }
}

Write-Host "== Pack .sacx ==" -ForegroundColor Cyan
$packerArgs = @($elfOut, $sacxOut)
if ($Imports -and $Imports.Trim().Length -gt 0) {
  $importsPath = if ([System.IO.Path]::IsPathRooted($Imports)) { $Imports } else { Join-Path $ProjectRoot $Imports }
  $importsPath = (Resolve-Path -LiteralPath $importsPath).Path
  $packerArgs += $importsPath
}

& $packerExe @packerArgs
if ($LASTEXITCODE) { throw "sacx packer failed" }

Write-Host "Built SACX app:" -ForegroundColor Green
Write-Host "  ELF : $elfOut"
Write-Host "  SACX: $sacxOut"
