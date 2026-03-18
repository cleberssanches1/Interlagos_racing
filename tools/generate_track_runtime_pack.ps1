param(
    [string]$DataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$OutPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\TRKRDR.BIN"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-U16([byte[]]$Bytes, [int]$Offset) { return [uint16][System.BitConverter]::ToUInt16($Bytes, $Offset) }
function Read-U32([byte[]]$Bytes, [int]$Offset) { return [uint32][System.BitConverter]::ToUInt32($Bytes, $Offset) }
function Align-4([uint32]$Value) { return [uint32](($Value + 3) -band (-bnot 3)) }
function Write-U16([System.IO.BinaryWriter]$Bw, [uint16]$Value) { $Bw.Write([uint16]$Value) }
function Write-U32([System.IO.BinaryWriter]$Bw, [uint32]$Value) { $Bw.Write([uint32]$Value) }

if (-not (Test-Path -LiteralPath $DataDir)) {
    throw "DataDir nao encontrado: $DataDir"
}

$files = @(Get-ChildItem -LiteralPath $DataDir -File -Filter "S???.RDR" | Sort-Object Name)
if ($files.Count -eq 0) {
    throw "Nenhum S???.RDR encontrado em $DataDir"
}

$entries = New-Object System.Collections.Generic.List[object]
$maxSegmentId = 0
$maxBlobSize = [uint32]0
$maxVertexCount = [uint32]0
$maxFaceCount = [uint32]0
$maxFamilyCount = [uint32]0

foreach ($file in $files) {
    if ($file.BaseName -notmatch '^S(\d{3})$') { continue }

    $segmentId = [int]$Matches[1]
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -lt 80) { throw "RDR pequeno demais: $($file.FullName)" }

    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    if ($magic -ne 0x31524452) { throw "Magic RDR invalido em $($file.FullName)" }
    if ($version -ne 1) { throw "Versao RDR invalida em $($file.FullName)" }

    $vertexCount = [uint32](Read-U32 $bytes 12)
    $faceCount = [uint32](Read-U32 $bytes 16)
    $familyIdsOffset = [int](Read-U32 $bytes 68)
    $familyBytes = [int]($faceCount * 2)
    if (($familyIdsOffset + $familyBytes) -gt $bytes.Length) {
        throw "Tabela de familias invalida em $($file.FullName)"
    }

    $familySet = New-Object 'System.Collections.Generic.HashSet[uint16]'
    for ($i = 0; $i -lt $faceCount; $i++) {
        $familyId = [uint16](Read-U16 $bytes ($familyIdsOffset + ($i * 2)))
        if ($familyId -ne 0) { [void]$familySet.Add($familyId) }
    }

    $entry = [pscustomobject]@{
        segmentId = [uint16]$segmentId
        path = [string]$file.FullName
        size = [uint32]$bytes.Length
        vertexCount = [uint32]$vertexCount
        faceCount = [uint32]$faceCount
        familyCount = [uint16]$familySet.Count
        offset = [uint32]0
    }
    $entries.Add($entry) | Out-Null

    if ($segmentId -gt $maxSegmentId) { $maxSegmentId = $segmentId }
    if ($bytes.Length -gt $maxBlobSize) { $maxBlobSize = [uint32]$bytes.Length }
    if ($vertexCount -gt $maxVertexCount) { $maxVertexCount = $vertexCount }
    if ($faceCount -gt $maxFaceCount) { $maxFaceCount = $faceCount }
    if ($familySet.Count -gt $maxFamilyCount) { $maxFamilyCount = [uint32]$familySet.Count }
}

if ($maxSegmentId -le 0) {
    throw "Nao foi possivel determinar maxSegmentId dos RDR"
}

$headerSize = [uint32]44
$entrySize = [uint32]24
$directoryOffset = $headerSize
$directoryBytes = [uint32]($maxSegmentId * $entrySize)
$dataOffset = Align-4 ($directoryOffset + $directoryBytes)
$cursor = $dataOffset

$entryById = @{}
foreach ($entry in $entries) {
    $entry.offset = $cursor
    $entryById[[int]$entry.segmentId] = $entry
    $cursor = Align-4 ([uint32]($cursor + $entry.size))
}

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$fs = [System.IO.File]::Open($OutPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $bw = New-Object System.IO.BinaryWriter($fs)

    Write-U32 $bw 0x314B5254
    Write-U16 $bw 1
    Write-U16 $bw 44
    Write-U16 $bw ([uint16]$entries.Count)
    Write-U16 $bw ([uint16]$maxSegmentId)
    Write-U32 $bw $directoryOffset
    Write-U32 $bw $dataOffset
    Write-U32 $bw $maxBlobSize
    Write-U32 $bw $maxVertexCount
    Write-U32 $bw $maxFaceCount
    Write-U32 $bw $maxFamilyCount
    Write-U32 $bw 0
    Write-U32 $bw 0

    for ($segmentId = 1; $segmentId -le $maxSegmentId; $segmentId++) {
        $entry = $entryById[$segmentId]
        if ($null -eq $entry) {
            Write-U32 $bw 0
            Write-U32 $bw 0
            Write-U32 $bw 0
            Write-U32 $bw 0
            Write-U16 $bw 0
            Write-U16 $bw 0
            Write-U32 $bw 0
            continue
        }

        Write-U32 $bw ([uint32]$entry.offset)
        Write-U32 $bw ([uint32]$entry.size)
        Write-U32 $bw ([uint32]$entry.vertexCount)
        Write-U32 $bw ([uint32]$entry.faceCount)
        Write-U16 $bw ([uint16]$entry.familyCount)
        Write-U16 $bw 0
        Write-U32 $bw 0
    }

    while ($fs.Position -lt $dataOffset) { $bw.Write([byte]0) }

    foreach ($entry in $entries) {
        while ($fs.Position -lt $entry.offset) { $bw.Write([byte]0) }
        [byte[]]$bytes = [System.IO.File]::ReadAllBytes($entry.path)
        $bw.Write($bytes)
        $aligned = Align-4 ([uint32]$fs.Position)
        while ($fs.Position -lt $aligned) { $bw.Write([byte]0) }
    }

    $bw.Flush()
}
finally {
    $fs.Dispose()
}

Write-Host ("TRKRDR ok: {0} segs:{1} maxId:{2} maxBlob:{3} maxV:{4} maxF:{5} maxFam:{6}" -f `
    $OutPath,
    $entries.Count,
    $maxSegmentId,
    $maxBlobSize,
    $maxVertexCount,
    $maxFaceCount,
    $maxFamilyCount)
