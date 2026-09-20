[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [string]$PrivateKeyPath = (Join-Path $env:USERPROFILE 'Documents\DihOS Keys\mesart-development-p256-private.hex'),

  # Offline Mesa-produced MIR3 artifacts copied under OS\MesaRuntime.  They
  # are signed as kernel-compositor shader assets; the runtime build stays
  # useful with none while the A7xx draw broker is still being implemented.
  [string[]]$KernelShaderPath = @(),

  # Host build outputs.  Each MIR3 file is copied into the signed runtime at
  # shaders/<file-name>; sources are never loaded by DihOS directly.
  [string[]]$KernelShaderSourcePath = @(),

  # One Mesa-derived MPIP reflection file binds an admitted VS/FS pair.  It
  # contains no PM4 or GPU addresses; the kernel translates it into state.
  [string[]]$KernelPipelinePath = @(),

  # Host MPIP outputs copied into pipelines/<file-name> before signing.
  [string[]]$KernelPipelineSourcePath = @()
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$defaultPrivateKeyPath = Join-Path $env:USERPROFILE 'Documents\DihOS Keys\mesart-development-p256-private.hex'
# When this script is launched through `powershell -File`, PowerShell expands
# @('vert.mir3', 'frag.mir3') before binding this child script.  With the
# documented command line that can land the second MIR3 argument in the
# earlier PrivateKeyPath parameter.  Recover that unambiguously rather than
# trying to read a shader as a signing key.
if ([IO.Path]::GetExtension($PrivateKeyPath) -ieq '.mir3') {
  $KernelShaderSourcePath = @($KernelShaderSourcePath) + @($PrivateKeyPath)
  $PrivateKeyPath = $defaultPrivateKeyPath
}
$llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
$clang = Join-Path $llvm 'clang.exe'
$ld = Join-Path $llvm 'ld.lld.exe'
if (!(Test-Path -LiteralPath $clang) -or !(Test-Path -LiteralPath $ld)) {
  throw 'LLVM clang.exe and ld.lld.exe are required under Program Files\LLVM\bin.'
}
if (!(Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf)) {
  throw "Development private key not found: $PrivateKeyPath"
}

$objectDir = Join-Path $root 'build\mesart_stub'
$bundleDir = Join-Path $root 'OS\MesaRuntime'
$source = Join-Path $root 'mesart\stub\renderer_stub.S'
$runtimeSource = Join-Path $root 'mesart\runtime\mesart_runtime.c'
$winsysSource = Join-Path $root 'mesart\mesa_port\mesart_freedreno_winsys.c'
$mesaShimSource = Join-Path $root 'mesart\mesa_port\mesart_mesa_shim.c'
$mesaDeviceSource = Join-Path $root 'mesart\mesa_port\mesart_freedreno_mesa_device.c'
$mesaPm4Source = Join-Path $root 'mesart\mesa_port\mesart_freedreno_pm4.c'
$mesaIr3Source = Join-Path $root 'mesart\mesa_port\mesart_freedreno_ir3.c'
$mesaRoot = Join-Path $root 'third_party\mesa'
$mesaDevInfoSource = Join-Path $mesaRoot 'src\freedreno\common\freedreno_dev_info.c'
$mesaRegisterGenerator = Join-Path $mesaRoot 'src\freedreno\registers\gen_header.py'
$mesaRegisterRoot = Join-Path $mesaRoot 'src\freedreno\registers'
$mesaA6xxXml = Join-Path $mesaRegisterRoot 'adreno\a6xx.xml'
$mesaDevicesGenerator = Join-Path $mesaRoot 'src\freedreno\common\freedreno_devices.py'
$mesaLibcInclude = Join-Path $root 'mesart\mesa_port\libc\include'
$linker = Join-Path $root 'mesart\stub\renderer_stub.ld'
$object = Join-Path $objectDir 'renderer_stub.o'
$runtimeObject = Join-Path $objectDir 'mesart_runtime.o'
$winsysObject = Join-Path $objectDir 'mesart_freedreno_winsys.o'
$mesaShimObject = Join-Path $objectDir 'mesart_mesa_shim.o'
$mesaDevInfoObject = Join-Path $objectDir 'freedreno_dev_info.o'
$mesaDeviceObject = Join-Path $objectDir 'mesart_freedreno_mesa_device.o'
$mesaPm4Object = Join-Path $objectDir 'mesart_freedreno_pm4.o'
$mesaIr3Object = Join-Path $objectDir 'mesart_freedreno_ir3.o'
$renderer = Join-Path $bundleDir 'renderer.elf'
$signer = Join-Path $root 'tools\pack_mesart_bundle.ps1'
New-Item -ItemType Directory -Force -Path $objectDir, $bundleDir | Out-Null

$shaderBundlePaths = [System.Collections.Generic.List[string]]::new()
$pipelineBundlePaths = [System.Collections.Generic.List[string]]::new()
foreach ($path in $KernelShaderPath) {
  if ($path) { [void]$shaderBundlePaths.Add($path) }
}
foreach ($path in $KernelPipelinePath) {
  if ($path) { [void]$pipelineBundlePaths.Add($path) }
}
if ($KernelShaderSourcePath.Count -gt 0) {
  $shaderDir = Join-Path $bundleDir 'shaders'
  New-Item -ItemType Directory -Force -Path $shaderDir | Out-Null
  foreach ($source in $KernelShaderSourcePath) {
    if (!$source -or !(Test-Path -LiteralPath $source -PathType Leaf)) {
      throw "Kernel shader source is missing: $source"
    }
    $resolvedSource = (Resolve-Path -LiteralPath $source).Path
    if ([IO.Path]::GetExtension($resolvedSource) -ine '.mir3') {
      throw "Kernel shader source must be an MIR3 artifact: $resolvedSource"
    }
    $leaf = Split-Path -Leaf $resolvedSource
    $relative = "shaders/$leaf"
    if ($shaderBundlePaths -contains $relative) {
      throw "Duplicate kernel shader bundle path: $relative"
    }
    Copy-Item -LiteralPath $resolvedSource -Destination (Join-Path $shaderDir $leaf) -Force
    [void]$shaderBundlePaths.Add($relative)
  }
}
if ($KernelPipelineSourcePath.Count -gt 0) {
  $pipelineDir = Join-Path $bundleDir 'pipelines'
  New-Item -ItemType Directory -Force -Path $pipelineDir | Out-Null
  foreach ($source in $KernelPipelineSourcePath) {
    if (!$source -or !(Test-Path -LiteralPath $source -PathType Leaf)) {
      throw "Kernel pipeline source is missing: $source"
    }
    $resolvedSource = (Resolve-Path -LiteralPath $source).Path
    if ([IO.Path]::GetExtension($resolvedSource) -ine '.mpip') {
      throw "Kernel pipeline source must be an MPIP artifact: $resolvedSource"
    }
    $leaf = Split-Path -Leaf $resolvedSource
    $relative = "pipelines/$leaf"
    if ($pipelineBundlePaths -contains $relative) {
      throw "Duplicate kernel pipeline bundle path: $relative"
    }
    Copy-Item -LiteralPath $resolvedSource -Destination (Join-Path $pipelineDir $leaf) -Force
    [void]$pipelineBundlePaths.Add($relative)
  }
}

if (!(Test-Path -LiteralPath $mesaDevInfoSource) -or
    !(Test-Path -LiteralPath $mesaRegisterGenerator) -or
    !(Test-Path -LiteralPath $mesaDevicesGenerator)) {
  throw 'Pinned Mesa source is incomplete; initialize third_party\mesa before building Mesart.'
}
$mesaGeneratedDir = Join-Path $objectDir 'mesa_generated'
New-Item -ItemType Directory -Force -Path $mesaGeneratedDir | Out-Null
$mesaA6xxPython = Join-Path $mesaGeneratedDir 'a6xx.py'
$mesaDevicesHeader = Join-Path $mesaGeneratedDir 'freedreno_devices.h'
$mesaPm4Header = Join-Path $mesaGeneratedDir 'adreno_pm4.xml.h'
$a6xxOutput = & py.exe -3 $mesaRegisterGenerator --rnn $mesaRegisterRoot --xml $mesaA6xxXml py-defines
if ($LASTEXITCODE) { throw 'Could not generate Mesa A6xx register Python definitions.' }
[IO.File]::WriteAllText($mesaA6xxPython,
  (($a6xxOutput -join [Environment]::NewLine) + [Environment]::NewLine),
  [Text.UTF8Encoding]::new($false))
$pm4Output = & py.exe -3 $mesaRegisterGenerator --rnn $mesaRegisterRoot --xml (Join-Path $mesaRegisterRoot 'adreno\adreno_pm4.xml') c-defines
if ($LASTEXITCODE) { throw 'Could not generate Mesa Adreno PM4 definitions.' }
[IO.File]::WriteAllText($mesaPm4Header,
  (($pm4Output -join [Environment]::NewLine) + [Environment]::NewLine),
  [Text.UTF8Encoding]::new($false))
$devicesOutput = & py.exe -3 $mesaDevicesGenerator -p $mesaGeneratedDir
if ($LASTEXITCODE) { throw 'Could not generate Mesa Freedreno device definitions.' }
[IO.File]::WriteAllText($mesaDevicesHeader,
  (($devicesOutput -join [Environment]::NewLine) + [Environment]::NewLine),
  [Text.UTF8Encoding]::new($false))

$mesaCFlags = @(
  '-target', 'aarch64-unknown-none-elf',
  '-D_WIN32=1', '-DMESA_DEBUG=0', '-Wno-initializer-overrides',
  '-ffreestanding', '-fno-builtin', '-fPIE',
  '-I', $mesaLibcInclude,
  '-I', $mesaGeneratedDir,
  '-I', (Join-Path $mesaRoot 'src'),
  '-I', (Join-Path $mesaRoot 'include'),
  '-I', (Join-Path $mesaRoot 'src\freedreno'),
  '-I', (Join-Path $mesaRoot 'src\freedreno\common'),
  '-I', (Join-Path $root 'mesart\include'),
  '-I', (Join-Path $root 'mesart\runtime'),
  '-I', (Join-Path $root 'mesart\mesa_port')
)

& $clang -target aarch64-unknown-none-elf -fPIE -c $source -o $object
if ($LASTEXITCODE) { throw 'Could not compile the Mesart EL0 stub.' }
& $clang -target aarch64-unknown-none-elf -ffreestanding -fno-builtin -fPIE `
  -I (Join-Path $root 'mesart\include') -I (Join-Path $root 'mesart\runtime') `
  -I (Join-Path $root 'mesart\mesa_port') `
  -c $runtimeSource -o $runtimeObject
if ($LASTEXITCODE) { throw 'Could not compile the Mesart runtime foundation.' }
& $clang -target aarch64-unknown-none-elf -ffreestanding -fno-builtin -fPIE `
  -I (Join-Path $root 'mesart\include') -I (Join-Path $root 'mesart\runtime') `
  -I (Join-Path $root 'mesart\mesa_port') `
  -c $winsysSource -o $winsysObject
if ($LASTEXITCODE) { throw 'Could not compile the Mesart Freedreno winsys bridge.' }
& $clang -target aarch64-unknown-none-elf -ffreestanding -fno-builtin -fPIE `
  -I $mesaLibcInclude -I (Join-Path $root 'mesart\include') `
  -I (Join-Path $root 'mesart\runtime') `
  -c $mesaShimSource -o $mesaShimObject
if ($LASTEXITCODE) { throw 'Could not compile the Mesart Mesa runtime shim.' }
& $clang $mesaCFlags -c $mesaDevInfoSource -o $mesaDevInfoObject
if ($LASTEXITCODE) { throw 'Could not compile Mesa Freedreno device information.' }
& $clang $mesaCFlags -c $mesaDeviceSource -o $mesaDeviceObject
if ($LASTEXITCODE) { throw 'Could not compile the Mesart Mesa device adapter.' }
& $clang $mesaCFlags -c $mesaPm4Source -o $mesaPm4Object
if ($LASTEXITCODE) { throw 'Could not compile the Mesart Mesa PM4 adapter.' }
& $clang $mesaCFlags -c $mesaIr3Source -o $mesaIr3Object
if ($LASTEXITCODE) { throw 'Could not compile the Mesart Mesa IR3 vocabulary adapter.' }
& $ld -pie -nostdlib -e _start -T $linker -o $renderer `
  $object $runtimeObject $winsysObject $mesaShimObject $mesaDevInfoObject $mesaDeviceObject $mesaPm4Object $mesaIr3Object
if ($LASTEXITCODE) { throw 'Could not link the Mesart EL0 stub.' }

$shaderBundleArray = $shaderBundlePaths.ToArray()
$pipelineBundleArray = $pipelineBundlePaths.ToArray()
# Splat arrays into the PowerShell script call.  A plain command invocation
# can expand an array and shift the remaining positional parameters (which
# made the signer read a non-key file when two MIR3 files were supplied).
$signerArgs = @{
  BundleRoot = $bundleDir
  PrivateKeyPath = $PrivateKeyPath
  RendererPath = 'renderer.elf'
  RuntimeAbi = 1
  KernelShaderPath = $shaderBundleArray
  KernelPipelinePath = $pipelineBundleArray
}
& $signer @signerArgs
if ($LASTEXITCODE) { throw 'Could not sign the Mesart runtime bundle.' }
Write-Host "Kernel MIR3 artifacts signed: $($shaderBundleArray.Count) [$($shaderBundleArray -join ', ')]" -ForegroundColor Cyan
Write-Host "Kernel MPIP artifacts signed: $($pipelineBundleArray.Count) [$($pipelineBundleArray -join ', ')]" -ForegroundColor Cyan
Write-Host "Mesart stub bundle ready: $bundleDir" -ForegroundColor Green
