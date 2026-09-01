[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectRoot,

  [string]$PrivateKeyPath = (Join-Path $env:USERPROFILE 'Documents\DihOS Keys\mesart-development-p256-private.hex')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
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
$renderer = Join-Path $bundleDir 'renderer.elf'
$signer = Join-Path $root 'tools\pack_mesart_bundle.ps1'
New-Item -ItemType Directory -Force -Path $objectDir, $bundleDir | Out-Null

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
& $ld -pie -nostdlib -e _start -T $linker -o $renderer `
  $object $runtimeObject $winsysObject $mesaShimObject $mesaDevInfoObject $mesaDeviceObject $mesaPm4Object
if ($LASTEXITCODE) { throw 'Could not link the Mesart EL0 stub.' }

& $signer -BundleRoot $bundleDir -PrivateKeyPath $PrivateKeyPath -RendererPath 'renderer.elf' -RuntimeAbi 1
if ($LASTEXITCODE) { throw 'Could not sign the Mesart runtime bundle.' }
Write-Host "Mesart stub bundle ready: $bundleDir" -ForegroundColor Green
