param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [string]$BootEntry   = "EfiMain",
  [string]$Stage2Entry = "EfiMain",

  [string]$BootOut       = "build\BOOTAA64.EFI",
  [string]$Stage2Out     = "build\STAGE2.EFI",
  [string]$KernelOut     = "",
  [string]$KernelAa64Out = "OS\aa64\KERNEL.ELF",
  [string]$KernelX64Out  = "OS\x64\KERNEL.ELF",

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
$ObjKAA64 = Join-Path $BuildDir "obj_kernel_aa64"
$ObjKX64  = Join-Path $BuildDir "obj_kernel_x64"
New-Item -Force -ItemType Directory -Path $BuildDir,$ObjBoot,$ObjS2,$ObjKAA64,$ObjKX64 | Out-Null

if ($KernelOut) {
  # Back-compat for older invocations: -KernelOut now means "AA64 kernel output".
  $KernelAa64Out = $KernelOut
}

$BootOutFull       = Join-Path $ProjectRoot $BootOut
$Stage2OutFull     = Join-Path $ProjectRoot $Stage2Out
$KernelAa64OutFull = Join-Path $ProjectRoot $KernelAa64Out
$KernelX64OutFull  = Join-Path $ProjectRoot $KernelX64Out

New-Item -Force -ItemType Directory -Path @(
  (Split-Path -Parent $BootOutFull),
  (Split-Path -Parent $Stage2OutFull),
  (Split-Path -Parent $KernelAa64OutFull),
  (Split-Path -Parent $KernelX64OutFull)
) | Out-Null

# Source discovery (recursive)
$SrcDir = Join-Path $ProjectRoot "src"
$IncDir = Join-Path $ProjectRoot "include"
$KerDir = Join-Path $ProjectRoot "kernel"
$KerInc = Join-Path $KerDir "include"

$bootSrc  = Get-ChildItem -Recurse -Path $SrcDir -Filter "boot.c" -File | Select-Object -First 1
$stageSrc = Get-ChildItem -Recurse -Path $SrcDir -Filter "*.c" -File | Where-Object { $_.Name -ne "boot.c" }
$kernSrcAll = Get-ChildItem -Recurse -Path $KerDir -Include "*.c","*.cpp" -File

if (!$bootSrc)     { throw "src\boot.c not found" }
if (!$stageSrc)    { throw "No stage2 sources found under src\" }
if (!$kernSrcAll)  { throw "No kernel sources found under kernel\" }

$opt = if ($Config -eq "Release") { "-O2" } else { "-O0" }

function Include-Flags {
  param([string[]]$Dirs)

  $flags = @()
  foreach ($dir in $Dirs) {
    if ($dir -and (Test-Path $dir)) {
      $flags += @("-I", $dir)
    }
  }
  return ,$flags
}

function Kernel-Include-Dirs {
  param([string]$Arch)

  return @(
    (Join-Path $KerInc "arch\$Arch"),
    (Join-Path $KerDir "asm\$Arch"),
    $IncDir,
    $KerInc
  )
}

function Kernel-Arch-Defines {
  param([string]$Arch)

  if ($Arch -eq "aa64") {
    return @("-DKERNEL_ARCH_AA64=1", "-DKERNEL_ARCH=1")
  }
  if ($Arch -eq "x64") {
    return @("-DKERNEL_ARCH_X64=1", "-DKERNEL_ARCH=2")
  }
  throw "Unknown kernel arch: $Arch"
}

function New-Kernel-CFlags {
  param(
    [string]$Arch,
    [string]$Target
  )

  $flags = @(
    "-target",$Target,
    "-ffreestanding","-fno-builtin","-fno-stack-protector",
    "-fPIE","-std=c11",$opt,"-g"
  )
  $flags += Kernel-Arch-Defines -Arch $Arch
  $flags += Include-Flags -Dirs (Kernel-Include-Dirs -Arch $Arch)
  return ,$flags
}

function New-Kernel-CppFlags {
  param(
    [string]$Arch,
    [string]$Target
  )

  $flags = @(
    "-target",$Target,
    "-ffreestanding","-fno-builtin","-fno-stack-protector",
    "-fPIE","-std=c++17",
    "-fno-exceptions","-fno-rtti",
    $opt,"-g"
  )
  $flags += Kernel-Arch-Defines -Arch $Arch
  $flags += Include-Flags -Dirs (Kernel-Include-Dirs -Arch $Arch)
  return ,$flags
}

function Get-KernelSourcesForArch {
  param([string]$Arch)

  $asmRoot = Join-Path $KerDir "asm"
  $asmArch = Join-Path $asmRoot $Arch
  if (!(Test-Path $asmArch)) {
    throw "kernel\asm\$Arch not found"
  }

  $asmRootFull = (Resolve-Path -LiteralPath $asmRoot).Path
  $asmArchFull = (Resolve-Path -LiteralPath $asmArch).Path
  if (!$asmRootFull.EndsWith([IO.Path]::DirectorySeparatorChar)) {
    $asmRootFull += [IO.Path]::DirectorySeparatorChar
  }
  if (!$asmArchFull.EndsWith([IO.Path]::DirectorySeparatorChar)) {
    $asmArchFull += [IO.Path]::DirectorySeparatorChar
  }

  $sources = @(
    $kernSrcAll | Where-Object {
      $path = $_.FullName
      if ($path.StartsWith($asmRootFull, [StringComparison]::OrdinalIgnoreCase)) {
        $path.StartsWith($asmArchFull, [StringComparison]::OrdinalIgnoreCase)
      } else {
        $true
      }
    } | ForEach-Object { $_.FullName }
  )

  if (!$sources) {
    throw "No kernel sources selected for $Arch"
  }

  return ,$sources
}

# ---- CFLAGS ----
# UEFI (PE/COFF, MSVC ABI)
$uefi_cflags = @(
  "-target","aarch64-pc-windows-msvc",
  "-ffreestanding","-fno-builtin","-fshort-wchar",
  "-Wall","-Wextra","-Wno-unused-parameter",
  "-std=c11",$opt
)
$uefi_cflags += Include-Flags -Dirs @($IncDir)

function Compile-C {
  param(
    [string[]]$Sources,
    [string]$ObjDir,
    [string[]]$CFlags,
    [string[]]$CppFlags = @()
  )

  $objs=@()

  foreach ($f in $Sources) {
    $ext = [IO.Path]::GetExtension($f)
    $o = Join-Path $ObjDir ([IO.Path]::GetFileNameWithoutExtension($f) + ".o")

    if ($ext -eq ".cpp") {
      if (!$CppFlags) { throw "C++ source needs CppFlags: $f" }
      & $clang $CppFlags "-c" $f "-o" $o
    } else {
      & $clang $CFlags "-c" $f "-o" $o
    }

    if ($LASTEXITCODE) { throw "clang failed on $f" }

    $objs += $o
  }

  return ,$objs
}

function Build-Kernel {
  param(
    [string]$Arch,
    [string]$Target,
    [string]$ObjDir,
    [string]$OutFile,
    [string]$LinkerScript
  )

  Write-Host "== KERNEL $Arch (PIE ELF) -> $OutFile ==" -ForegroundColor Cyan

  $sources = Get-KernelSourcesForArch -Arch $Arch
  $cflags = New-Kernel-CFlags -Arch $Arch -Target $Target
  $cppflags = New-Kernel-CppFlags -Arch $Arch -Target $Target
  $objs = Compile-C -Sources $sources -ObjDir $ObjDir -CFlags $cflags -CppFlags $cppflags

  & $ldelf -o $OutFile -T $LinkerScript -pie -nostdlib $objs
  if ($LASTEXITCODE) { throw "ld.lld (kernel $Arch) failed" }

  Write-Host "Built: $OutFile" -ForegroundColor Green
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

# ---- KERNELS (PIE ELF) ----
$ldsAA64 = Join-Path $KerDir "kernel.ld"
$ldsX64  = Join-Path $KerDir "kernel_x64.ld"
Build-Kernel -Arch "aa64" -Target "aarch64-unknown-none-elf" -ObjDir $ObjKAA64 -OutFile $KernelAa64OutFull -LinkerScript $ldsAA64
Build-Kernel -Arch "x64"  -Target "x86_64-unknown-none-elf"  -ObjDir $ObjKX64  -OutFile $KernelX64OutFull  -LinkerScript $ldsX64

# ---- USB copy (U:\) if present ----
$UsbRoot = "U:\"
if (Test-Path $UsbRoot) {
  $destBoot = Join-Path $UsbRoot "EFI\BOOT"
  $destAA64 = Join-Path $UsbRoot "OS\aa64"
  $destX64  = Join-Path $UsbRoot "OS\x64"
  New-Item -Force -ItemType Directory -Path $destBoot,$destAA64,$destX64 | Out-Null

  Copy-Item -Force $BootOutFull       (Join-Path $destBoot "BOOTAA64.EFI")
  Copy-Item -Force $Stage2OutFull     (Join-Path $destAA64 "STAGE2.EFI")
  Copy-Item -Force $KernelAa64OutFull (Join-Path $destAA64 "KERNEL.ELF")
  Copy-Item -Force $KernelX64OutFull  (Join-Path $destX64 "KERNEL.ELF")

  Write-Host "Copied outputs to U:\ successfully:" -ForegroundColor Green
  Write-Host "  U:\EFI\BOOT\BOOTAA64.EFI"
  Write-Host "  U:\OS\aa64\STAGE2.EFI"
  Write-Host "  U:\OS\aa64\KERNEL.ELF"
  Write-Host "  U:\OS\x64\KERNEL.ELF"
} else {
  Write-Host "USB drive U:\ not found. Skipping copy." -ForegroundColor Yellow
  Write-Host ""
  Write-Host "Copy to USB:" -ForegroundColor Yellow
  Write-Host ("  \EFI\BOOT\BOOTAA64.EFI   <= " + $BootOutFull)
  Write-Host ("  \OS\aa64\STAGE2.EFI       <= " + $Stage2OutFull)
  Write-Host ("  \OS\aa64\KERNEL.ELF       <= " + $KernelAa64OutFull)
  Write-Host ("  \OS\x64\KERNEL.ELF        <= " + $KernelX64OutFull)
}
