[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BundleRoot,

  [Parameter(Mandatory = $true)]
  [string]$PrivateKeyPath,

  [Parameter(Mandatory = $true)]
  [string]$RendererPath,

  [string]$PublicKeyHeader = "",
  [uint32]$RuntimeAbi = 1,
  [string]$GmuFirmwarePath = "",
  [string]$SqeFirmwarePath = "",
  [string]$ZapFirmwarePath = "",
  [string[]]$KernelShaderPath = @(),

  [string[]]$KernelPipelinePath = @()
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot is not reliably populated while PowerShell evaluates a
# parameter default for `powershell -File`. Resolve this default inside the
# script body so direct invocation and the documented command line agree.
if (!$PublicKeyHeader) {
  $PublicKeyHeader = Join-Path (Split-Path -Parent $PSScriptRoot) 'kernel\include\mesart\mesart_development_root.h'
}

$ManifestMagic = [uint32]0x5452534d # "MSRT", little-endian
$ManifestVersion = [uint16]1
$HeaderBytes = [uint16]40
$EntryPathBytes = 160
$SignatureBytes = 64

function Get-Sha256 {
  param([byte[]]$Bytes)
  $hash = [System.Security.Cryptography.SHA256]::Create()
  try { return ,([byte[]]$hash.ComputeHash($Bytes)) }
  finally { $hash.Dispose() }
}

function Convert-HexToBytes {
  param([string]$Hex, [string]$Name)
  # The key generator writes bare ASCII hex, but editors sometimes prepend a
  # UTF-8 BOM or leave zero-width whitespace.  Those bytes are formatting,
  # not secret material; remove only whitespace/BOM markers before checking
  # the exact 32-byte scalar below.  Do not echo the key or the normalized
  # value from this signing path.
  $text = ($Hex -replace '[\s\uFEFF\u200B]', '')
  if ($text.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
    $text = $text.Substring(2)
  }
  if ($text.Length % 2 -ne 0 -or $text -notmatch '^[0-9a-fA-F]+$') {
    # A locally archived key may have a human-readable label around the
    # scalar. Accept it only when it contains exactly one isolated P-256
    # scalar; ambiguity is rejected rather than guessing which secret to use.
    $candidates = [regex]::Matches($Hex, '(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])')
    if ($candidates.Count -ne 1) { throw "$Name is not hexadecimal." }
    $text = $candidates[0].Value
  }
  $bytes = [byte[]]::new($text.Length / 2)
  for ($i = 0; $i -lt $bytes.Length; ++$i) {
    $bytes[$i] = [Convert]::ToByte($text.Substring($i * 2, 2), 16)
  }
  return $bytes
}

function Read-P256PrivateScalar {
  param([string]$Path)
  # Accept the original Windows PowerShell generator's UTF-16LE output as
  # well as the current ASCII contract.  Both represent exactly the same
  # 64 hex characters.  Decode locally and never place the scalar in an
  # error message or output stream.
  $raw = [IO.File]::ReadAllBytes($Path)
  if (!$raw.Length) { throw 'Private key is empty.' }
  if ($raw.Length -ge 2 -and $raw[0] -eq 0xff -and $raw[1] -eq 0xfe) {
    $text = [Text.Encoding]::Unicode.GetString($raw)
  } elseif ($raw.Length -ge 2 -and $raw[0] -eq 0xfe -and $raw[1] -eq 0xff) {
    $text = [Text.Encoding]::BigEndianUnicode.GetString($raw)
  } else {
    # Decode bare UTF-8/ASCII keys as text.  Validation below accepts only a
    # single exact scalar, so this cannot reinterpret arbitrary bytes as key
    # material; it merely avoids PowerShell's output-array coercion.
    $text = [Text.Encoding]::UTF8.GetString($raw)
  }
  $normalized = ($text -replace '[\s\uFEFF\u200B]', '')
  Write-Host ("[mesart] private key input: bytes={0} normalized-chars={1} hex={2}" -f $raw.Length, $normalized.Length, ($normalized -match '^[0-9a-fA-F]+$')) -ForegroundColor DarkGray
  $out = Convert-HexToBytes $text 'Private key'
  if ($out.Length -ne 32) { throw 'Private key is not a 32-byte scalar.' }
  return ,$out
}

function Get-PublicPointFromHeader {
  param([string]$HeaderPath)
  if (!(Test-Path -LiteralPath $HeaderPath -PathType Leaf)) {
    throw "Public root header not found: $HeaderPath"
  }
  $matches = [regex]::Matches((Get-Content -LiteralPath $HeaderPath -Raw), '0x([0-9a-fA-F]{2})')
  if ($matches.Count -ne 65) {
    throw 'Public root header does not contain exactly one uncompressed P-256 point.'
  }
  $point = [byte[]]::new(65)
  for ($i = 0; $i -lt $point.Length; ++$i) {
    $point[$i] = [Convert]::ToByte($matches[$i].Groups[1].Value, 16)
  }
  if ($point[0] -ne 0x04) { throw 'Public root must use SEC1 uncompressed form.' }
  return ,$point
}

function Get-RelativeBundlePath {
  param([string]$Path)
  $relative = $Path.Replace('\', '/')
  if (!$relative -or $relative.StartsWith('/') -or $relative.StartsWith('.') -or
      $relative.Contains(':') -or $relative.Length -ge $EntryPathBytes -or
      $relative -notmatch '^[A-Za-z0-9._/-]+$') {
    throw "Invalid bundle-relative path: $Path"
  }
  foreach ($part in $relative.Split('/')) {
    if (!$part -or $part -eq '..') { throw "Invalid bundle-relative path: $Path" }
  }
  return $relative
}

function Convert-DerEcdsaToRawP256 {
  param([byte[]]$Der)
  if ($Der.Length -eq $SignatureBytes) { return ,$Der }
  $at = 0
  if ($Der.Length -lt 8 -or $Der[$at++] -ne 0x30) { throw 'ECDSA signature is not DER.' }
  $sequenceLength = [int]$Der[$at++]
  if (($sequenceLength -band 0x80) -ne 0) { throw 'Unexpected long DER signature length.' }
  if ($sequenceLength -ne $Der.Length - $at) { throw 'Malformed DER signature length.' }
  $raw = [byte[]]::new($SignatureBytes)
  foreach ($field in 0..1) {
    if ($at + 2 -gt $Der.Length -or $Der[$at++] -ne 0x02) { throw 'Malformed DER integer.' }
    $length = [int]$Der[$at++]
    if ($length -le 0 -or $length -gt 33 -or $at + $length -gt $Der.Length) {
      throw 'Invalid P-256 DER integer length.'
    }
    $integer = $Der[$at..($at + $length - 1)]
    $at += $length
    if ($integer.Length -eq 33) {
      if ($integer[0] -ne 0) { throw 'DER integer exceeds P-256 width.' }
      $integer = $integer[1..32]
    }
    [Array]::Copy($integer, 0, $raw, $field * 32 + 32 - $integer.Length, $integer.Length)
  }
  if ($at -ne $Der.Length) { throw 'Unexpected bytes after ECDSA signature.' }
  return $raw
}

$root = (Resolve-Path -LiteralPath $BundleRoot).Path
$private = Read-P256PrivateScalar -Path $PrivateKeyPath
if ($private.Length -ne 32) { throw 'The development/release P-256 private scalar must be 32 bytes.' }
$public = Get-PublicPointFromHeader $PublicKeyHeader

$entries = [System.Collections.Generic.List[object]]::new()
function Add-ManifestEntry {
  param([uint32]$Role, [string]$RelativePath)
  if (!$RelativePath) { return }
  $relative = Get-RelativeBundlePath $RelativePath
  $full = Join-Path $root $relative.Replace('/', [IO.Path]::DirectorySeparatorChar)
  if (!(Test-Path -LiteralPath $full -PathType Leaf)) { throw "Bundle file is missing: $relative" }
  $file = Get-Item -LiteralPath $full
  if ($file.Length -le 0) { throw "Bundle file is empty: $relative" }
  [void]$entries.Add([pscustomobject]@{
    Role = $Role; Path = $relative; Bytes = [uint64]$file.Length
    Hash = Get-Sha256 ([IO.File]::ReadAllBytes($full))
  })
}

Add-ManifestEntry 1 $RendererPath
Add-ManifestEntry 2 $GmuFirmwarePath
Add-ManifestEntry 3 $SqeFirmwarePath
Add-ManifestEntry 4 $ZapFirmwarePath
foreach ($shader in $KernelShaderPath) { Add-ManifestEntry 5 $shader }
foreach ($pipeline in $KernelPipelinePath) { Add-ManifestEntry 6 $pipeline }
if ($entries.Count -eq 0 -or $entries.Count -gt 32) { throw 'A bundle needs 1 through 32 files.' }
if (@($entries | Where-Object { $_.Role -eq 1 }).Count -ne 1) { throw 'Exactly one renderer ELF is required.' }
if (@($entries.Path | Select-Object -Unique).Count -ne $entries.Count) { throw 'Bundle paths must be unique.' }

$stream = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::UTF8, $true)
$writer.Write($ManifestMagic)
$writer.Write($ManifestVersion)
$writer.Write($HeaderBytes)
$writer.Write($RuntimeAbi)
$writer.Write([uint32]$entries.Count)
$writer.Write([uint64]0) # Capability grants come only from the kernel, not a bundle.
$writer.Write([guid]::NewGuid().ToByteArray())
foreach ($entry in $entries) {
  $pathBytes = [Text.Encoding]::ASCII.GetBytes($entry.Path)
  $fixedPath = [byte[]]::new($EntryPathBytes)
  [Array]::Copy($pathBytes, $fixedPath, $pathBytes.Length)
  $writer.Write([uint32]$entry.Role)
  $writer.Write([uint32]0)
  $writer.Write([uint64]$entry.Bytes)
  $writer.Write([byte[]]$entry.Hash)
  $writer.Write($fixedPath)
}
$writer.Flush()
$signed = $stream.ToArray()

$curve = [Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
$publicPoint = [Security.Cryptography.ECPoint]::new()
$publicPoint.X = [byte[]]$public[1..32]
$publicPoint.Y = [byte[]]$public[33..64]
$parameters = [Security.Cryptography.ECParameters]::new()
$parameters.Curve = $curve
$parameters.D = $private
$parameters.Q = $publicPoint
$ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
  $ecdsa.ImportParameters($parameters)
  $digest = Get-Sha256 $signed
  $signature = Convert-DerEcdsaToRawP256 ($ecdsa.SignHash($digest))
} finally {
  $ecdsa.Dispose()
}

$manifest = [byte[]]::new($signed.Length + $signature.Length)
[Array]::Copy($signed, 0, $manifest, 0, $signed.Length)
[Array]::Copy($signature, 0, $manifest, $signed.Length, $signature.Length)
$outPath = Join-Path $root 'manifest.msrt'
[IO.File]::WriteAllBytes($outPath, $manifest)
Write-Host "Wrote signed Mesa runtime manifest: $outPath" -ForegroundColor Green
Write-Host "Files signed: $($entries.Count); runtime ABI: $RuntimeAbi" -ForegroundColor Cyan
Write-Host 'Private key was read in place and was not copied into the bundle or repository.' -ForegroundColor Yellow
