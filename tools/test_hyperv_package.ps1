$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path $root ('build\hyperv-package-test-' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $fixture 'source'
$assets = Join-Path $fixture 'assets'
$package = Join-Path $fixture 'package'
$target = Join-Path $fixture 'disk'
foreach ($path in @('build','build\sacx','OS\aa64','OS\System')) {
  New-Item -ItemType Directory -Force (Join-Path $source $path) | Out-Null
}
foreach ($path in @('OS\System\Images\Mouse','OS\System\Fonts')) {
  New-Item -ItemType Directory -Force (Join-Path $assets $path) | Out-Null
}
foreach ($path in @('build\BOOTAA64.EFI','build\STAGE2.EFI','OS\aa64\KERNEL.ELF','build\sacx\image_viewer.sacx')) {
  Set-Content -LiteralPath (Join-Path $source $path) -Value 'fixture binary' -Encoding ASCII
}
Set-Content (Join-Path $source 'build\sacx\Dihscover.sacx') 'fixture browser'
foreach ($library in @('netsurf','libparserutils','libwapcaplet','libhubbub','libdom','libcss','libnsutils','libnsbmp','libnsgif')) {
  $licenseDir=Join-Path $source "third_party\netsurf-all-3.11\$library"
  New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
  Set-Content (Join-Path $licenseDir 'COPYING') 'fixture license'
}
Set-Content (Join-Path $assets 'OS\System\Images\bgpaper.jpg') 'fixture wallpaper'
Set-Content (Join-Path $assets 'OS\System\Images\Mouse\arrow.cur') 'fixture cursor'
Set-Content (Join-Path $assets 'OS\System\Fonts\Solarize.psf') 'fixture font'
New-Item -ItemType Directory -Force (Join-Path $target 'Documents') | Out-Null
Set-Content (Join-Path $target 'Documents\keep.txt') 'preserve me'
& (Join-Path $root 'tools\hyperv.ps1') -Action Package -ProjectRoot $source -AssetSource $assets -PackagePath $package -IncludeDihscover
$manifest = Get-Content (Join-Path $package 'manifest.json') -Raw | ConvertFrom-Json
if (!$manifest.files.Count -or !$manifest.build) { throw 'Package manifest absent' }
if (!($manifest.files | Where-Object path -eq 'OS/System/Programs/Dihscover/Dihscover.sacx')) { throw 'Browser absent from manifest' }
if (@($manifest.files | Where-Object path -like 'OS/System/Programs/Dihscover/Licenses/*').Count -ne 9) { throw 'Browser licenses absent' }

# Fake only the host disk APIs. The production manifest verification and copy
# implementation runs against a disposable directory without Hyper-V privileges.
$script:mounts=0
function Get-VM { param($Name) [pscustomobject]@{State='Off'} }
function Get-VHD { param($Path) [pscustomobject]@{Attached=$false} }
function Mount-VHD { param($Path,[switch]$ReadOnly,[switch]$Passthru) ++$script:mounts; [pscustomobject]@{TestDisk=1} }
function Dismount-VHD { param($Path) }
function Get-Disk { param([Parameter(ValueFromPipeline)]$InputObject) process { [pscustomobject]@{Number=99} } }
function Get-Partition { param([Parameter(ValueFromPipeline)]$InputObject) process { [pscustomobject]@{Type='Basic';GptType=''} } }
function Get-Volume { param([Parameter(ValueFromPipeline)]$InputObject) process { [pscustomobject]@{FileSystem='FAT32';Path=$target+'\'} } }
& (Join-Path $root 'tools\hyperv.ps1') -Action Update -ProjectRoot $source -VhdPath 'fixture.vhdx' -PackagePath $package
if ((Get-Content (Join-Path $target 'Documents\keep.txt')) -ne 'preserve me') { throw 'User file changed' }
foreach ($file in $manifest.files) {
  if ((Get-FileHash (Join-Path $target $file.path)).Hash -ne $file.sha256) { throw "Mismatch: $($file.path)" }
}
Set-Content (Join-Path $package 'OS\System\Images\bgpaper.jpg') 'tampered'
$prior=$script:mounts
$rejected=$false
try { & (Join-Path $root 'tools\hyperv.ps1') -Action Update -ProjectRoot $source -VhdPath 'fixture.vhdx' -PackagePath $package }
catch { if ($_ -notmatch 'hash/size mismatch') { throw }; $rejected=$true }
if (!$rejected -or $script:mounts -ne $prior) { throw 'Tampered package mounted a disk' }
Write-Output 'PASS: package manifest/hashes, full system assets, preserved user file, tamper rejection before mounting'

# Exercise a real OneDrive CLOUD-tagged file when the checkout provides one.
$cloudFile = Join-Path $root 'OS\System\Programs\guessing_game.sac'
if ((Test-Path -LiteralPath $cloudFile) -and ((Get-Item -LiteralPath $cloudFile).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
  $cloudPackage = Join-Path $fixture 'cloud-package'
  & (Join-Path $root 'tools\hyperv.ps1') -Action Package -ProjectRoot $root -AssetSource $assets -PackagePath $cloudPackage
  if ((Get-FileHash (Join-Path $cloudPackage 'OS\System\Programs\guessing_game.sac')).Hash -ne (Get-FileHash $cloudFile).Hash) {
    throw 'OneDrive file contents changed during packaging'
  }
  Write-Output 'PASS: actual OneDrive file packaged as normal contents'
}
# Reject directory junctions before descending into them, including on PS 5.1.
$outside = Join-Path $fixture 'outside'
New-Item -ItemType Directory -Path $outside | Out-Null
Set-Content (Join-Path $outside 'not-an-asset.txt') 'must not be followed'
New-Item -ItemType Junction -Path (Join-Path $assets 'OS\System\linked') -Target $outside | Out-Null
$rejected=$false
try {
  & (Join-Path $root 'tools\hyperv.ps1') -Action Package -ProjectRoot $source -AssetSource $assets -PackagePath (Join-Path $fixture 'link-package')
} catch { if ($_ -notmatch 'Unsupported reparse point') { throw }; $rejected=$true }
if (!$rejected) { throw 'Directory junction was followed' }
Write-Output 'PASS: asset directory junction rejected before traversal'

# Existing VMs must receive actual startup RAM, not only a dynamic maximum.
$configurationMemory=[pscustomobject]@{StartupBytes=0;Dynamic=$true}
function Get-VMNetworkAdapter { param($VM) [pscustomobject]@{Name='fixture NIC'} }
function Connect-VMNetworkAdapter { param([Parameter(ValueFromPipeline)]$InputObject,$SwitchName) process {} }
function Set-VMMemory { param($VM,[bool]$DynamicMemoryEnabled,[uint64]$StartupBytes) $configurationMemory.StartupBytes=$StartupBytes; $configurationMemory.Dynamic=$DynamicMemoryEnabled }
function Set-VMVideo { param($VM,$ResolutionType,$HorizontalResolution,$VerticalResolution) }
function Get-VMVideo { param($VM) }
& (Join-Path $root 'tools\hyperv.ps1') -Action Configure -ProjectRoot $source
if ($configurationMemory.StartupBytes -ne 2GB -or $configurationMemory.Dynamic) { throw 'Existing VM did not receive fixed 2 GiB startup memory' }
& (Join-Path $root 'tools\hyperv.ps1') -Action Configure -ProjectRoot $source -MemoryStartupBytes 1GB
if ($configurationMemory.StartupBytes -ne 1GB) { throw 'Explicit startup memory ignored' }
Write-Output 'PASS: existing VM fixed startup memory default and explicit override'
