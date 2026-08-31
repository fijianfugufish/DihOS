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
$linker = Join-Path $root 'mesart\stub\renderer_stub.ld'
$object = Join-Path $objectDir 'renderer_stub.o'
$renderer = Join-Path $bundleDir 'renderer.elf'
$signer = Join-Path $root 'tools\pack_mesart_bundle.ps1'
New-Item -ItemType Directory -Force -Path $objectDir, $bundleDir | Out-Null

& $clang -target aarch64-unknown-none-elf -fPIE -c $source -o $object
if ($LASTEXITCODE) { throw 'Could not compile the Mesart EL0 stub.' }
& $ld -pie -nostdlib -e _start -T $linker -o $renderer $object
if ($LASTEXITCODE) { throw 'Could not link the Mesart EL0 stub.' }

& $signer -BundleRoot $bundleDir -PrivateKeyPath $PrivateKeyPath -RendererPath 'renderer.elf' -RuntimeAbi 1
if ($LASTEXITCODE) { throw 'Could not sign the Mesart runtime bundle.' }
Write-Host "Mesart stub bundle ready: $bundleDir" -ForegroundColor Green
