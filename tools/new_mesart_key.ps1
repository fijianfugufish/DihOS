param(
  [ValidateSet("Development", "Release")]
  [string]$Purpose = "Development",

  [string]$PrivateKeyDirectory = (Join-Path $env:USERPROFILE "Documents\DihOS Keys")
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

function ConvertTo-Hex([byte[]]$Bytes) {
  $chars = New-Object System.Text.StringBuilder
  foreach ($byte in $Bytes) { [void]$chars.AppendFormat("{0:X2}", $byte) }
  return $chars.ToString()
}

function Format-CBytes([byte[]]$Bytes) {
  $parts = @()
  foreach ($byte in $Bytes) { $parts += ("0x{0:X2}" -f $byte) }
  return ($parts -join ", ")
}

$curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
$key = [System.Security.Cryptography.ECDsa]::Create()
$key.GenerateKey($curve)
try {
  $parameters = $key.ExportParameters($true)
  $public = New-Object byte[] 65
  $public[0] = 0x04
  [Array]::Copy($parameters.Q.X, 0, $public, 1, 32)
  [Array]::Copy($parameters.Q.Y, 0, $public, 33, 32)

  New-Item -ItemType Directory -Force -Path $PrivateKeyDirectory | Out-Null
  $privatePath = Join-Path $PrivateKeyDirectory ("mesart-{0}-p256-private.hex" -f $Purpose.ToLowerInvariant())
  # Explicit ASCII keeps the private scalar's on-disk representation stable
  # across Windows PowerShell and PowerShell 7.  The packer also accepts the
  # older UTF-16LE form so existing development keys remain usable.
  Set-Content -LiteralPath $privatePath -Value (ConvertTo-Hex $parameters.D) -NoNewline -Encoding Ascii

  $headerName = if ($Purpose -eq "Development") { "mesart_development_root.h" } else { "mesart_release_root.h" }
  $macroName = if ($Purpose -eq "Development") { "MESART_DEVELOPMENT_ROOT" } else { "MESART_RELEASE_ROOT" }
  $headerPath = Join-Path $projectRoot ("kernel\include\mesart\" + $headerName)
  $header = @"
#pragma once

/* Public P-256 root generated locally on $(Get-Date -Format o). */
#define ${macroName}_CONFIGURED 1
#define ${macroName}_BYTES { $(Format-CBytes $public) }
"@
  Set-Content -LiteralPath $headerPath -Value $header -NoNewline

  Write-Host "Created $Purpose Mesa Runtime key." -ForegroundColor Green
  Write-Host "Private scalar (keep secret): $privatePath"
  Write-Host "Public kernel root: $headerPath"
  Write-Host "Review and commit only the public header; never commit the private scalar."
}
finally {
  $key.Dispose()
}
