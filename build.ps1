param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [string]$BootEntry   = "EfiMain",
  [string]$Stage2Entry = "EfiMain",

  [string]$BootOut     = "build\BOOTAA64.EFI",
  [string]$Stage2Out   = "build\STAGE2.EFI",
  [string]$KernelOut   = "build\KERNEL.ELF",

  [ValidateSet("Release","Debug")]
  [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# LLVM
$llvm = Join-Path $Env:ProgramFiles 'LLVM\bin'
if (Test-Path (Join-Path $llvm 'clang.exe')) { $env:Path = "$llvm;$env:Path" }
$clang = "clang.exe"
$lld   = "lld-link.exe"
$ldelf = "ld.lld"

# Dirs
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$BuildDir = Join-Path $ProjectRoot "build"
$ObjBoot  = Join-Path $BuildDir "obj_boot"
$ObjS2    = Join-Path $BuildDir "obj_stage2"
$ObjK     = Join-Path $BuildDir "obj_kernel"
New-Item -Force -ItemType Directory -Path $BuildDir,$ObjBoot,$ObjS2,$ObjK | Out-Null

$BootOutFull   = Join-Path $ProjectRoot $BootOut
$Stage2OutFull = Join-Path $ProjectRoot $Stage2Out
$KernelOutFull = Join-Path $ProjectRoot $KernelOut

# Source discovery (recursive)
$SrcDir = Join-Path $ProjectRoot "src"
$IncDir = Join-Path $ProjectRoot "include"
$KerDir = Join-Path $ProjectRoot "kernel"
$KerInc = Join-Path $KerDir "include"

$bootSrc  = Get-ChildItem -Recurse -Path $SrcDir -Filter "boot.c" -File | Select-Object -First 1
$stageSrc = Get-ChildItem -Recurse -Path $SrcDir -Filter "*.c" -File | Where-Object { $_.Name -ne "boot.c" }
$kernSrc = Get-ChildItem -Recurse -Path $KerDir -Include "*.c","*.cpp" -File

if (!$bootSrc)  { throw "src\boot.c not found" }
if (!$stageSrc) { throw "No stage2 sources found under src\" }
if (!$kernSrc)  { throw "No kernel sources found under kernel\" }

$opt = if ($Config -eq "Release") { "-O2" } else { "-O0" }

# ---- CFLAGS ----
# UEFI (PE/COFF, MSVC ABI)
$uefi_cflags = @(
  "-target","aarch64-pc-windows-msvc",
  "-ffreestanding","-fno-builtin","-fshort-wchar",
  "-Wall","-Wextra","-Wno-unused-parameter",
  "-std=c11",$opt
)
if (Test-Path $IncDir) { $uefi_cflags += @("-I", $IncDir) }

# KERNEL (ELF PIE)
$kern_cflags = @(
  "-target","aarch64-unknown-none-elf",
  "-ffreestanding","-fno-builtin","-fno-stack-protector",
  "-fPIE","-std=c11",$opt,"-g"
)

if (Test-Path $IncDir) { $kern_cflags += @("-I", $IncDir) }
if (Test-Path $KerInc) { $kern_cflags += @("-I", $KerInc) }

function Compile-C {
  param(
    [string[]]$Sources,
    [string]$ObjDir,
    [string[]]$CFlags
  )

  $objs=@()

  foreach ($f in $Sources) {
    $ext = [IO.Path]::GetExtension($f)
    $o = Join-Path $ObjDir ([IO.Path]::GetFileNameWithoutExtension($f) + ".o")

    if ($ext -eq ".cpp") {
      $cppflags = @(
          "-target","aarch64-unknown-none-elf",
          "-ffreestanding","-fno-builtin","-fno-stack-protector",
          "-fPIE","-std=c++17",
          "-fno-exceptions","-fno-rtti",
          $opt,"-g"
      )

      if (Test-Path $IncDir) { $cppflags += @("-I", $IncDir) }
      if (Test-Path $KerInc) { $cppflags += @("-I", $KerInc) }

      & $clang $cppflags "-c" $f "-o" $o
    } else {
      & $clang $CFlags "-c" $f "-o" $o
    }

    if ($LASTEXITCODE) { throw "clang failed on $f" }

    $objs += $o
  }

  return ,$objs
}

# ---- BOOT ----
Write-Host "== BOOT -> $BootOutFull ==" -ForegroundColor Cyan
$bootObjs = Compile-C -Sources @($bootSrc.FullName) -ObjDir $ObjBoot -CFlags $uefi_cflags
& $lld /nologo /machine:arm64 /subsystem:efi_application /entry:$BootEntry /nodefaultlib /out:$BootOutFull $bootObjs
if ($LASTEXITCODE) { throw "lld-link (boot) failed" }
Write-Host "Built: $BootOutFull" -ForegroundColor Green

# ---- STAGE2 ----
Write-Host "== STAGE2 -> $Stage2OutFull ==" -ForegroundColor Cyan
$stageObjs = Compile-C -Sources ($stageSrc.FullName) -ObjDir $ObjS2 -CFlags $uefi_cflags
& $lld /nologo /machine:arm64 /subsystem:efi_application /entry:$Stage2Entry /nodefaultlib /out:$Stage2OutFull $stageObjs
if ($LASTEXITCODE) { throw "lld-link (stage2) failed" }
Write-Host "Built: $Stage2OutFull" -ForegroundColor Green

# ---- KERNEL (PIE ELF) ----
Write-Host "== KERNEL (PIE ELF) -> $KernelOutFull ==" -ForegroundColor Cyan
$kObjs = Compile-C -Sources ($kernSrc.FullName) -ObjDir $ObjK -CFlags $kern_cflags
$lds = Join-Path $KerDir "kernel.ld"
& $ldelf -o $KernelOutFull -T $lds -pie -nostdlib $kObjs
if ($LASTEXITCODE) { throw "ld.lld (kernel) failed" }
Write-Host "Built: $KernelOutFull" -ForegroundColor Green

# ---- USB copy (U:\) if present ----
$UsbRoot = "U:\"
if (Test-Path $UsbRoot) {
  $destBoot = Join-Path $UsbRoot "EFI\BOOT"
  $destOS   = Join-Path $UsbRoot "OS\aa64"
  New-Item -Force -ItemType Directory -Path $destBoot,$destOS | Out-Null

  Copy-Item -Force $BootOutFull   (Join-Path $destBoot "BOOTAA64.EFI")
  Copy-Item -Force $Stage2OutFull (Join-Path $destOS "STAGE2.EFI")
  Copy-Item -Force $KernelOutFull (Join-Path $destOS "KERNEL.ELF")

  Write-Host "Copied outputs to U:\ successfully:" -ForegroundColor Green
  Write-Host "  U:\EFI\BOOT\BOOTAA64.EFI"
  Write-Host "  U:\OS\aa64\STAGE2.EFI"
  Write-Host "  U:\OS\aa64\KERNEL.ELF"
} else {
  Write-Host "USB drive U:\ not found. Skipping copy." -ForegroundColor Yellow
  Write-Host ""
  Write-Host "Copy to USB:" -ForegroundColor Yellow
  Write-Host ("  \EFI\BOOT\BOOTAA64.EFI   <= " + $BootOutFull)
  Write-Host ("  \OS\aa64\STAGE2.EFI       <= " + $Stage2OutFull)
  Write-Host ("  \OS\aa64\KERNEL.ELF       <= " + $KernelOutFull)
}
