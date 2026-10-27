param([string]$AppPath='', [string]$Fixture='tools/browser_fixtures/auto-margins.html', [switch]$Images, [switch]$Regression, [switch]$Click)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  if(!$AppPath){$result=Get-Content build/browser-validation/result.json -Raw | ConvertFrom-Json;$AppPath=Join-Path $result.package 'OS/System/Programs/Dihscover/Dihscover.sacx'}
  $AppPath=(Resolve-Path -LiteralPath $AppPath).Path
  $Fixture=(Resolve-Path -LiteralPath $Fixture).Path
  $output=Join-Path $root 'build/browser-binary-repro'
  New-Item -ItemType Directory -Force $output | Out-Null
  $header=(Get-Content -LiteralPath sdk/sacx/include/sacx_api.h -Raw).Replace("`r",'')
  $header=$header.Substring($header.IndexOf("struct sacx_api`n"))
  $fields=@([regex]::Matches($header,'\(\*(\w+)\)\s*\(') | ForEach-Object { 'STUB('+ $_.Groups[1].Value + ');' })
  [IO.File]::WriteAllLines((Join-Path $output 'browser_runner_api.inc'),$fields,[Text.UTF8Encoding]::new($false))
  & 'C:\Program Files\LLVM\bin\clang++.exe' -target aarch64-linux-gnu -std=c++17 -O1 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -static -fuse-ld=lld '-Wl,-e,_start' -Isdk/sacx/include -Ikernel/include "-I$output" tools/browser_binary_runner.cpp -x c++ kernel/system/kfiber.c -o (Join-Path $output 'runner')
  if($LASTEXITCODE){throw 'ARM64 host-adapter build failed'}
  function Relative([string]$Path){if(!$Path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test input must be inside this repository'};return './'+$Path.Substring($root.Length+1).Replace('\','/')}
  $app=Relative $AppPath
  function Invoke-Fixture([string]$InputPath,[string]$Name,[bool]$WithImages){
    $fixturePath=Relative $InputPath
    $ppm=Join-Path $output ($Name+'.ppm');$log=Join-Path $output ($Name+'.log')
    $args=@('-d','Ubuntu','--cd',$root,'--','timeout','20s','./build/browser-binary-repro/runner',$app,$fixturePath,(Relative $ppm))
    if($WithImages){$args+='images'}
    if($Click){$args+='click'}
    & wsl.exe @args *> $log
    $code=$LASTEXITCODE
    Get-Content -LiteralPath $log
    if($code){throw "ARM64 browser reproduction failed: $code ($Name)"}
    return
  }
  if($Regression){
    $auto=Join-Path $root 'tools/browser_fixtures/auto-margins.html'
    $zero=Join-Path $output 'zero-vertical-margins.html'
    $html=Get-Content -LiteralPath $auto -Raw
    [IO.File]::WriteAllText($zero,$html.Replace('margin:auto','margin:0 auto'),[Text.UTF8Encoding]::new($false))
    foreach($enabled in @($false,$true)){
      $suffix=if($enabled){'on'}else{'off'}
      Invoke-Fixture $auto ('auto-'+$suffix) $enabled
      Invoke-Fixture $zero ('zero-'+$suffix) $enabled
      $a=Join-Path $output ('auto-'+$suffix);$z=Join-Path $output ('zero-'+$suffix)
      $log=Get-Content -LiteralPath ($a+'.log') -Raw
      $match=[regex]::Match($log,'runner: nonwhite pixels\s+(\d+)')
      if(!$match.Success -or [uint64]$match.Groups[1].Value -lt 100){throw 'Auto-margin page painted no readable content'}
      if((Get-FileHash -LiteralPath ($a+'.ppm')).Hash -ne (Get-FileHash -LiteralPath ($z+'.ppm')).Hash){throw 'Auto vertical margins differ from zero; horizontal centering or margin collapse regressed'}
    }
    Write-Output 'PASS: production ARM64 binary renders auto margins identically to zero vertical margins, with images on and off'
  }else{
    Invoke-Fixture $Fixture 'page' ([bool]$Images)
    if($Click){
      $requests=@(Get-Content (Join-Path $output 'page.log') | Where-Object { $_ -eq 'https://target.test/' })
      if($requests.Count -ne 1){throw "One link click started $($requests.Count) destination requests"}
      Write-Output 'PASS: one mouse press/release starts exactly one destination request'
    }
  }
} finally {Pop-Location}
