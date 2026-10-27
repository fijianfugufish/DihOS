param(
  [ValidateSet('ExportAssets','Package','CreateImage','Update','Configure')]
  [string]$Action = 'Package',
  [string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent),
  [string]$VMName = 'DihOS',
  [string]$VhdPath = 'C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\DihOS.vhdx',
  [string]$AssetSource = '',
  [string]$PackagePath = '',
  [switch]$IncludeDihscover,
  [uint64]$SizeBytes = 4GB,
  [ValidateRange(512MB, 1TB)]
  [uint64]$MemoryStartupBytes = 2GB,
  [string]$SwitchName = 'Default Switch'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (!$AssetSource) { $AssetSource = Join-Path $ProjectRoot 'build\hyperv-assets' }
if (!$PackagePath) { $PackagePath = Join-Path $ProjectRoot 'build\hyperv-package' }
$AssetSource = [IO.Path]::GetFullPath($AssetSource)
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$VhdPath = [IO.Path]::GetFullPath($VhdPath)

function Assert-VMStopped {
  $vm = Get-VM -Name $VMName -ErrorAction SilentlyContinue
  if ($vm -and $vm.State -ne 'Off') { throw "Shut down $VMName before accessing its disk." }
  if (Get-VHD -Path $VhdPath | Where-Object Attached) { throw 'Disk is already attached; refusing to reuse another mount.' }
}
function With-Disk([bool]$ReadOnly, [scriptblock]$Body) {
  Assert-VMStopped
  $mounted = $false
  try {
    $vhd = Mount-VHD -Path $VhdPath -ReadOnly:$ReadOnly -Passthru
    $mounted = $true
    $disk = $vhd | Get-Disk
    $parts = @($disk | Get-Partition | Where-Object { $_.GptType -eq '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -or $_.Type -eq 'Basic' })
    $roots = @($parts | Get-Volume -ErrorAction SilentlyContinue | Where-Object { $_.FileSystem -eq 'FAT32' } | ForEach-Object { $_.Path })
    if ($roots.Count -ne 1) { throw "Expected exactly one FAT32 system volume, found $($roots.Count)." }
    & $Body $roots[0]
  } finally { if ($mounted) { Dismount-VHD -Path $VhdPath } }
}
function Safe-Path([string]$Root, [string]$Relative) {
  $base = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
  if (!$path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes root: $Relative" }
  return $path
}
function Assert-AssetEntry($Entry) {
  if (!($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return }
  # OneDrive Files On-Demand files/directories have CLOUD reparse tags too.
  # Inspect the tag without following links; only the CLOUD tag family is safe
  # to copy as ordinary contents. Junctions and symbolic links stay rejected.
  $info = & "$env:SystemRoot\System32\fsutil.exe" reparsepoint query $Entry.FullName 2>&1
  if ($LASTEXITCODE) { throw "Cannot inspect asset reparse point: $($Entry.FullName)" }
  $match = [regex]::Match(($info -join "`n"), '0x([0-9a-fA-F]{8})')
  $cloud = [Convert]::ToUInt32('9000001A',16)
  $mask = [Convert]::ToUInt32('FFFF0FFF',16)
  if (!$match.Success -or (([Convert]::ToUInt32($match.Groups[1].Value,16) -band $mask) -ne $cloud)) {
    throw "Unsupported reparse point in assets (links are not followed): $($Entry.FullName)"
  }
}
function Copy-Tree([string]$From, [string]$To) {
  if (!(Test-Path -LiteralPath $From)) { return }
  Assert-AssetEntry (Get-Item -LiteralPath $From -Force)
  foreach ($file in Get-ChildItem -LiteralPath $From) {
    Assert-AssetEntry $file
    $dest = Safe-Path $To $file.Name
    if ($file.PSIsContainer) {
      Copy-Tree $file.FullName $dest
      continue
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
  }
}
if ($Action -eq 'ExportAssets') {
  # Never write to the source disk. Export into a new bundle, never a merge.
  if (Test-Path -LiteralPath $AssetSource) { throw "Asset bundle already exists: $AssetSource" }
  With-Disk $true {
    param($root)
    foreach ($tree in @('OS\System','OS\Fonts')) {
      Copy-Tree (Join-Path $root $tree) (Join-Path $AssetSource $tree)
    }
  }
  Write-Host "Assets exported to $AssetSource"
  return
}
if ($Action -eq 'Configure') {
  $vm = Get-VM -Name $VMName -ErrorAction SilentlyContinue
  if (!$vm) {
    $vm = New-VM -Name $VMName -Generation 2 -MemoryStartupBytes $MemoryStartupBytes -VHDPath $VhdPath -SwitchName $SwitchName
    Set-VMProcessor -VM $vm -Count 2
    Set-VMFirmware -VM $vm -EnableSecureBoot Off
  } else {
    if ($vm.State -ne 'Off') { throw "Shut down $VMName before configuring it." }
    $nic = @(Get-VMNetworkAdapter -VM $vm)
    if (!$nic.Count) { Add-VMNetworkAdapter -VM $vm -SwitchName $SwitchName }
    else { $nic[0] | Connect-VMNetworkAdapter -SwitchName $SwitchName }
  }
  # This guest has no balloon driver: dynamic-memory maximum is not usable RAM.
  # Apply the requested fixed allocation to existing VMs as well as new ones.
  Set-VMMemory -VM $vm -DynamicMemoryEnabled $false -StartupBytes $MemoryStartupBytes
  Set-VMVideo -VM $vm -ResolutionType Maximum -HorizontalResolution 1920 -VerticalResolution 1080
  Get-VMVideo -VM $vm
  Get-VMNetworkAdapter -VM $vm
  return
}
if ($Action -eq 'Package') {
  if (Test-Path -LiteralPath $PackagePath) { throw "Use a new PackagePath; package already exists: $PackagePath" }
  New-Item -ItemType Directory -Path $PackagePath | Out-Null
  Copy-Tree $AssetSource $PackagePath
  # Native resources in the checkout take precedence over the recovered bundle.
  foreach ($tree in @('OS\System','OS\Fonts')) { Copy-Tree (Join-Path $ProjectRoot $tree) (Join-Path $PackagePath $tree) }
  $binaries = @{
    'EFI\BOOT\BOOTAA64.EFI' = 'build\BOOTAA64.EFI'
    'OS\aa64\STAGE2.EFI' = 'build\STAGE2.EFI'
    'OS\aa64\KERNEL.ELF' = 'OS\aa64\KERNEL.ELF'
    'OS\System\Programs\Image Viewer\image_viewer.sacx' = 'build\sacx\image_viewer.sacx'
  }
  if ($IncludeDihscover) {
    $binaries['OS\System\Programs\Dihscover\Dihscover.sacx'] = 'build\sacx\Dihscover.sacx'
    $licenseRoot = Join-Path $PackagePath 'OS\System\Programs\Dihscover\Licenses'
    foreach ($library in @('netsurf','libparserutils','libwapcaplet','libhubbub','libdom','libcss','libnsutils','libnsbmp','libnsgif')) {
      $license = Join-Path $ProjectRoot "third_party\netsurf-all-3.11\$library\COPYING"
      if (!(Test-Path -LiteralPath $license)) { throw "Dihscover license missing: $license" }
      New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
      Copy-Item -LiteralPath $license -Destination (Join-Path $licenseRoot "$library.txt") -Force
    }
  }
  foreach ($rel in $binaries.Keys) {
    $dest = Safe-Path $PackagePath $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $binaries[$rel]) -Destination $dest -Force
  }
  $images = Join-Path $PackagePath 'OS\System\Images'
  if (!(Test-Path (Join-Path $images 'bgpaper.jpg')) -and !(Test-Path (Join-Path $images 'wallpaper.jpg')) -and !(Test-Path (Join-Path $images 'bgpaper.bmp'))) { throw 'Wallpaper absent from asset bundle.' }
  if (!(Test-Path -LiteralPath (Join-Path $images 'Mouse\arrow.cur'))) { throw 'Required arrow.cur cursor absent from bundle.' }
  if (!(Test-Path -LiteralPath (Join-Path $PackagePath 'OS\System\Fonts\Solarize.psf'))) { throw 'Required Solarize.psf font absent from bundle.' }
  $build = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + (Get-FileHash (Join-Path $ProjectRoot 'OS\aa64\KERNEL.ELF')).Hash.Substring(0,12)
  "DihOS ARM64 Hyper-V build $build`nSystem file hashes: 0:/manifest.json" | Set-Content -Encoding ASCII (Join-Path $PackagePath 'OS\System\BUILD.TXT')
  $files = @(Get-ChildItem -LiteralPath $PackagePath -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($PackagePath.TrimEnd('\').Length + 1).Replace('\','/'); size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
  })
  @{ version = 1; build = $build; files = $files } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $PackagePath 'manifest.json')
  Write-Host "Package ready: $PackagePath ($build)"
  return
}
# Validate every file before mounting a writable image. Only manifest files are copied.
$manifest = Get-Content -LiteralPath (Join-Path $PackagePath 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne 1 -or !$manifest.files.Count) { throw 'Invalid package manifest.' }
foreach ($file in $manifest.files) {
  $path = Safe-Path $PackagePath $file.path
  if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256 -or (Get-Item -LiteralPath $path).Length -ne $file.size) { throw "Package hash/size mismatch: $($file.path)" }
}
if ($Action -eq 'CreateImage') {
  if (Test-Path -LiteralPath $VhdPath) { throw 'CreateImage never overwrites an existing disk.' }
  if ($SizeBytes -lt 2GB) { throw 'CreateImage needs at least 2 GiB for the 1 GiB system partition and GPT metadata.' }
  New-Item -ItemType Directory -Force (Split-Path $VhdPath -Parent) | Out-Null
  New-VHD -Path $VhdPath -SizeBytes $SizeBytes -Dynamic | Out-Null
  $mounted = $false
  try {
    $vhd = Mount-VHD -Path $VhdPath -Passthru
    $mounted = $true
    $disk = $vhd | Get-Disk
    $disk | Initialize-Disk -PartitionStyle GPT
    $part = $disk | New-Partition -Size 1GB -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
    $part | Format-Volume -FileSystem FAT32 -NewFileSystemLabel DihOS -Confirm:$false | Out-Null
  } finally { if ($mounted) { Dismount-VHD -Path $VhdPath } }
}
With-Disk $false {
  param($root)
  foreach ($file in $manifest.files) {
    $dest = Safe-Path $root $file.path
    New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
    Copy-Item -LiteralPath (Safe-Path $PackagePath $file.path) -Destination $dest -Force
    if ((Get-FileHash -LiteralPath $dest).Hash -ne $file.sha256) { throw "Deployed file verification failed: $($file.path)" }
  }
  Copy-Item -LiteralPath (Join-Path $PackagePath 'manifest.json') -Destination (Join-Path $root 'manifest.json') -Force
  Copy-Item -LiteralPath (Join-Path $PackagePath 'OS\System\BUILD.TXT') -Destination (Join-Path $root 'OS\System\BUILD.TXT') -Force
}
Write-Host "Deployed $($manifest.build). User files outside the manifest were preserved."
