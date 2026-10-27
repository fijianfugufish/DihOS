param([string]$Python='python',[string]$PackagePath='',[switch]$Fonts)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  $reportDir=Join-Path $root 'build\browser-validation'
  New-Item -ItemType Directory -Force $reportDir | Out-Null
  $export=& $Python tools/export_browser_snapshot.py
  if($LASTEXITCODE){throw 'Browser snapshot export failed'}
  $snapshot=$export | ConvertFrom-Json
  $snapshot | ConvertTo-Json | Set-Content (Join-Path $reportDir 'source.json')
  Write-Host "Validating isolated browser sources: $($snapshot.path)"
  $project=$snapshot.path
  & (Join-Path $project 'build.ps1') -ProjectRoot $project -ArmOnly -KernelAa64Out 'build\browser-validation\KERNEL.ELF' *> (Join-Path $reportDir 'kernel-build.log')
  & (Join-Path $project 'sdk\sacx\apps\dihscover\build.ps1') -ProjectRoot $project *> (Join-Path $reportDir 'browser-build.log')
  & (Join-Path $project 'tools\test_browser.ps1') -Python $Python -Fonts:$Fonts *> (Join-Path $reportDir 'browser-tests.log')
  & (Join-Path $project 'tools\test_hyperv.ps1') *> (Join-Path $reportDir 'hyperv-tests.log')
  & (Join-Path $project 'tools\test_hyperv_package.ps1') *> (Join-Path $reportDir 'package-tests.log')
  $sourceRecord=Join-Path $project 'OS\System\Programs\Dihscover\SOURCE.JSON'
  New-Item -ItemType Directory -Force (Split-Path $sourceRecord -Parent) | Out-Null
  @{base=$snapshot.base;overlay_sha256=$snapshot.overlay_sha256;dependencies=(Get-FileHash (Join-Path $project 'tools\browser-dependencies.lock.json')).Hash} | ConvertTo-Json | Set-Content $sourceRecord
  if(!$PackagePath){$PackagePath=Join-Path $root ('build\dihscover-responsive-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))}
  & (Join-Path $project 'tools\hyperv.ps1') -Action Package -ProjectRoot $project -AssetSource (Join-Path $root 'build\hyperv-assets') -PackagePath $PackagePath -IncludeDihscover -KernelFile 'build\browser-validation\KERNEL.ELF'
  @{package=$PackagePath;snapshot=$project;overlay_sha256=$snapshot.overlay_sha256;guestValidated=$false} | ConvertTo-Json | Set-Content (Join-Path $reportDir 'result.json')
  Write-Host "Built and host-tested package: $PackagePath"
} finally { Pop-Location }
