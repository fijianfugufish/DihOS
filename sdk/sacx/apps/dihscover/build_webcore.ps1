param([string]$ProjectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path,[ValidateSet('Release','Debug')][string]$Config='Release',[string]$OutputDirectory='build/sacx',[string]$EngineDirectory='build/webcore-headless-page')
$ErrorActionPreference='Stop'
$engineDir=Join-Path $ProjectRoot $EngineDirectory
$engineReportPath=Join-Path $engineDir 'report.json'
$enginePath=Join-Path $engineDir 'page.elf'
if (!(Test-Path -LiteralPath $engineReportPath)) {throw 'Build the native embedded WebCore engine first (compile_page_fixture.py --embedded).'}
$engineReport=Get-Content -LiteralPath $engineReportPath -Raw | ConvertFrom-Json
if (!$engineReport.embedded -or !$engineReport.linked -or !$engineReport.loader_accepted) {throw 'An embedded, linked and loader-validated WebCore engine is required.'}
if ((Get-FileHash -LiteralPath $enginePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $engineReport.elf_sha256) {throw 'WebCore engine hash changed; rebuild it first.'}
$arguments=@{
 ProjectRoot=$ProjectRoot
 Config=$Config
 Source=@('sdk/sacx/apps/dihscover/main.cpp','sdk/sacx/apps/dihscover/webcore_backend.cpp','sdk/sacx/apps/dihscover/preferences.cpp',
  'sdk/sacx/apps/dihscover/runtime.cpp','sdk/sacx/apps/dihscover/netsurf_libc.cpp','sdk/sacx/apps/dihscover/heap.cpp',
  'sdk/sacx/apps/dihscover/scheduling.cpp','third_party/tlsf/tlsf.c')
 OutSacx=(Join-Path $OutputDirectory 'Dihscover-WebCore.sacx')
 OutElfAA64=(Join-Path $OutputDirectory 'Dihscover-WebCore-aa64.elf')
 Imports='sdk/sacx/imports/default.imports.txt'
 Preemptible=$true
 IncludeDir=@('sdk/sacx/apps/dihscover/port','sdk/sacx/apps/dihscover/port/cxx','sdk/sacx/freestanding/include','third_party/tlsf','third_party/netsurf-all-3.11/netsurf/include')
 Define=@('NDEBUG','DIHSCOVER_FREESTANDING','DIHSCOVER_WEBCORE')
}
& (Join-Path $ProjectRoot 'build_sacx_app.ps1') @arguments
if($LASTEXITCODE){throw 'Dihscover WebCore frontend build failed'}
$out=Join-Path $ProjectRoot $OutputDirectory
$sacx=Join-Path $out 'Dihscover-WebCore.sacx'
Copy-Item -LiteralPath $sacx -Destination (Join-Path $out 'Dihscover.sacx') -Force
Copy-Item -LiteralPath $enginePath -Destination (Join-Path $out 'Dihscover-WebCore.bin') -Force
$hashes=@{}
Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File | Where-Object {$_.Extension -in @('.h','.cpp','.inc','.ps1')} | ForEach-Object {
 $relative=$_.FullName.Substring($ProjectRoot.Length+1).Replace('\','/');$hashes[$relative]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
@{engine='WebCore';frontend_sha256=(Get-FileHash -LiteralPath $sacx -Algorithm SHA256).Hash.ToLowerInvariant();engine_sha256=$engineReport.elf_sha256;
 native_report_sha256=(Get-FileHash -LiteralPath $engineReportPath -Algorithm SHA256).Hash.ToLowerInvariant();sources=$hashes} |
 ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $out 'Dihscover-engine.json')
