param(
    [string]$JsonPath = ".\cd\data\segments_map.json",
    [string]$OutPath = ".\cd\data\S001FAM.BIN"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $JsonPath)) {
    throw "Arquivo nao encontrado: $JsonPath"
}

$jsonText = Get-Content -LiteralPath $JsonPath -Raw
$root = $jsonText | ConvertFrom-Json
if (-not $root -or -not $root.segments) {
    throw "JSON invalido: campo 'segments' nao encontrado."
}

$seg = $root.segments | Where-Object { $_.id -eq 1 } | Select-Object -First 1
if (-not $seg) {
    throw "Segmento id=1 nao encontrado em $JsonPath"
}
if (-not $seg.faceTextureFamily) {
    throw "Campo 'faceTextureFamily' nao encontrado no segmento 1."
}

$families = @($seg.faceTextureFamily)
$faceCount = $families.Count
if ($faceCount -le 0) {
    throw "faceTextureFamily vazio para segmento 1."
}
if ($faceCount -gt 65535) {
    throw "faceTextureFamily excede limite de 65535 faces."
}

$outDir = Split-Path -Parent $OutPath
if (-not [string]::IsNullOrWhiteSpace($outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$fs = [System.IO.File]::Open($OutPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $bw = New-Object System.IO.BinaryWriter($fs)

    # Header (little-endian):
    # magic[4] = 'S','1','F','M'
    # version u16 = 1
    # reserved u16 = 0
    # segmentId u16 = 1
    # faceCount u16
    $bw.Write([byte][char]'S')
    $bw.Write([byte][char]'1')
    $bw.Write([byte][char]'F')
    $bw.Write([byte][char]'M')
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]$faceCount)

    foreach ($f in $families) {
        $v = [int]$f
        if ($v -lt 0) { $v = 0 }
        if ($v -gt 65535) { $v = 65535 }
        $bw.Write([UInt16]$v)
    }

    $bw.Flush()
}
finally {
    if ($bw) { $bw.Dispose() }
    $fs.Dispose()
}

$bytes = (Get-Item -LiteralPath $OutPath).Length
Write-Host ("OK S001FAM.BIN faces:{0} bytes:{1} -> {2}" -f $faceCount, $bytes, (Resolve-Path -LiteralPath $OutPath).Path)
