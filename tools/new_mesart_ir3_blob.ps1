[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RawIr3Path,

  [Parameter(Mandatory = $true)]
  [string]$OutputPath,

  [Parameter(Mandatory = $true)]
  [ValidateSet('Vertex', 'Fragment')]
  [string]$Stage,

  [UInt64]$ChipId = 0xffff43050c01
)

$ErrorActionPreference = 'Stop'
$rawPath = (Resolve-Path -LiteralPath $RawIr3Path).Path
$raw = [IO.File]::ReadAllBytes($rawPath)
if ($raw.Length -eq 0 -or $raw.Length -gt 4MB -or ($raw.Length % 8) -ne 0) {
  throw 'Raw IR3 must be non-empty, at most 4 MiB, and consist of whole 64-bit instructions.'
}

$stageValue = if ($Stage -eq 'Vertex') { [uint32]1 } else { [uint32]2 }
$header = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($header, [Text.Encoding]::UTF8, $true)
try {
  $writer.Write([uint32]0x3352494d) # "MIR3", little-endian
  $writer.Write([uint16]2)
  $writer.Write([uint16]64)
  $writer.Write([uint64]$ChipId)
  $writer.Write($stageValue)
  $writer.Write([uint32]$raw.Length)
  $writer.Write([uint32]0) # flags: no optional behavior asserted by raw wrapper
  $writer.Write([uint32]0) # instruction_groups: unavailable from raw payload
  $writer.Write([uint32]0) # const_vec4s
  $writer.Write([uint16]0) # sampler_count
  $writer.Write([uint16]0) # app_ubo_count
  $writer.Write([uint16]0) # input_count
  $writer.Write([uint16]0) # output_count
  $writer.Write([uint32]0) # output_dwords
  $writer.Write([uint32]0) # reserved0
  $writer.Write([uint32]0) # reserved1
  $writer.Write([uint32]0) # reserved2
  $writer.Write([uint32]0) # reserved3
  $writer.Flush()
  $bytes = $header.ToArray() + $raw
} finally {
  $writer.Dispose()
  $header.Dispose()
}

[IO.File]::WriteAllBytes($OutputPath, $bytes)
Write-Host "Wrote signed-Mesart-ready $Stage IR3 blob: $OutputPath" -ForegroundColor Green
Write-Host "Payload: $($raw.Length) bytes; chip: 0x$($ChipId.ToString('X'))" -ForegroundColor Cyan
