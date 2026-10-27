# Compiler depfiles include headers and amalgamated .c/.cc inputs. Keeping this
# separate lets the host test exercise the production cache with spaced paths.
function Clear-SacxDependencyCache {
  $script:SacxDependencyTimes=[Collections.Generic.Dictionary[string,datetime]]::new([StringComparer]::OrdinalIgnoreCase)
}
Clear-SacxDependencyCache
function Test-SacxObjectFresh([string]$Object,[string]$Source) {
  if(!(Test-Path -LiteralPath $Object) -or !(Test-Path -LiteralPath ($Object+'.d'))){return $false}
  $built=(Get-Item -LiteralPath $Object).LastWriteTimeUtc
  try {$raw=[IO.File]::ReadAllText($Object+'.d') -replace '\\(?:\r?\n)',' '}
  catch {return $false}
  $separator=[regex]::Match($raw,':\s+')
  if(!$separator.Success){return $false}
  $dependencies=@($Source)
  foreach($token in [regex]::Matches($raw.Substring($separator.Index+$separator.Length),'(?:\\.|[^\s])+')) {
    $dependencies+=($token.Value -replace '\\([ #:\\])','$1' -replace '\$\$','$')
  }
  foreach($dependency in $dependencies) {
    if(!$script:SacxDependencyTimes.ContainsKey($dependency)) {
      $item=Get-Item -LiteralPath $dependency -ErrorAction SilentlyContinue
      if(!$item -or $item.PSIsContainer){return $false}
      $script:SacxDependencyTimes[$dependency]=$item.LastWriteTimeUtc
    }
    if($script:SacxDependencyTimes[$dependency] -gt $built){return $false}
  }
  return $true
}
