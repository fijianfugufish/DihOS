param([string]$Clang='C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sacx_build_cache.ps1')
$root=Split-Path $PSScriptRoot -Parent
$directory=Join-Path $root ('build\cache test '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
$source=Join-Path $directory 'source.cpp';$included=Join-Path $directory 'included implementation.cc';$header=Join-Path $directory 'header without extension';$object=Join-Path $directory 'output.o'
foreach($file in @($source,$included,$header,$object)){Set-Content -LiteralPath $file -Value 'fixture'}
$old=[datetime]::UtcNow.AddMinutes(-2);$new=[datetime]::UtcNow.AddMinutes(-1)
foreach($file in @($source,$included,$header)){(Get-Item -LiteralPath $file).LastWriteTimeUtc=$old}
(Get-Item -LiteralPath $object).LastWriteTimeUtc=$new
function Escape-Dependency([string]$Path){return $Path.Replace('\','/').Replace(' ','\ ')}
$dependencyText=(Escape-Dependency $object)+": `\`n "+(Escape-Dependency $source)+" `\`n "+(Escape-Dependency $included)+' '+(Escape-Dependency $header)
[IO.File]::WriteAllText($object+'.d',$dependencyText)
if(!(Test-SacxObjectFresh $object $source)){throw 'Fresh dependency cache rejected spaced paths'}
(Get-Item -LiteralPath $included).LastWriteTimeUtc=[datetime]::UtcNow
Clear-SacxDependencyCache
if(Test-SacxObjectFresh $object $source){throw 'Included .cc change did not invalidate cache'}
(Get-Item -LiteralPath $included).LastWriteTimeUtc=$old
(Get-Item -LiteralPath $header).LastWriteTimeUtc=[datetime]::UtcNow
Clear-SacxDependencyCache
if(Test-SacxObjectFresh $object $source){throw 'Extensionless C++ header change did not invalidate cache'}
[IO.File]::WriteAllText($included,'int cache_value(){return 4;}')
[IO.File]::WriteAllText($header,'#define CACHE_DELTA 1')
[IO.File]::WriteAllText($source,"#include `"included implementation.cc`"`n#include `"header without extension`"`nint result(){return cache_value()+CACHE_DELTA;}`n")
& $Clang -target aarch64-unknown-none-elf -ffreestanding -MMD -MF ($object+'.d') -c $source -o $object
if($LASTEXITCODE){throw 'Real compiler dependency fixture failed'}
Clear-SacxDependencyCache
if(!(Test-SacxObjectFresh $object $source)){throw 'Actual Clang dependency file did not parse'}
(Get-Item -LiteralPath $included).LastWriteTimeUtc=[datetime]::UtcNow.AddSeconds(2)
Clear-SacxDependencyCache
if(Test-SacxObjectFresh $object $source){throw 'Actual Clang include dependency did not invalidate'}
Write-Output 'PASS: compiler dependency cache handles spaces, line continuations, included .cc files and extensionless C++ headers'
