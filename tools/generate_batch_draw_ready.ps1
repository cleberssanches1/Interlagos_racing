param(
    [string]$DataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [int]$BatchSize = 3,
    [switch]$AllBatches = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [uint16][System.BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32][System.BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-I32([byte[]]$Bytes, [int]$Offset) {
    return [int32][System.BitConverter]::ToInt32($Bytes, $Offset)
}

function Write-U16([System.IO.BinaryWriter]$Bw, [uint16]$Value) { $Bw.Write($Value) }
function Write-U32([System.IO.BinaryWriter]$Bw, [uint32]$Value) { $Bw.Write($Value) }
function Write-I32([System.IO.BinaryWriter]$Bw, [int32]$Value) { $Bw.Write($Value) }

function Align-4([uint32]$Value) {
    return [uint32](($Value + 3) -band (-bnot 3))
}

function Load-Sdr([string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 80) { throw "SDR pequeno demais: $Path" }

    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    if ($magic -ne 0x31524453) { throw "Magic SDR invalido em $Path" }
    if ($version -ne 1) { throw "Versao SDR invalida em $Path" }

    $segmentId = [int](Read-U16 $bytes 8)
    $vertexCount = [int](Read-U32 $bytes 12)
    $faceCount = [int](Read-U32 $bytes 16)
    $centerX = [int32](Read-I32 $bytes 20)
    $centerY = [int32](Read-I32 $bytes 24)
    $centerZ = [int32](Read-I32 $bytes 28)
    $minX = [int32](Read-I32 $bytes 32)
    $minY = [int32](Read-I32 $bytes 36)
    $minZ = [int32](Read-I32 $bytes 40)
    $maxX = [int32](Read-I32 $bytes 44)
    $maxY = [int32](Read-I32 $bytes 48)
    $maxZ = [int32](Read-I32 $bytes 52)
    $verticesOffset = [int](Read-U32 $bytes 56)
    $facesOffset = [int](Read-U32 $bytes 60)
    $attrsOffset = [int](Read-U32 $bytes 64)
    $familyIdsOffset = [int](Read-U32 $bytes 68)

    return [pscustomobject]@{
        bytes = $bytes
        segmentId = $segmentId
        vertexCount = $vertexCount
        faceCount = $faceCount
        centerX = $centerX
        centerY = $centerY
        centerZ = $centerZ
        minX = $minX
        minY = $minY
        minZ = $minZ
        maxX = $maxX
        maxY = $maxY
        maxZ = $maxZ
        verticesOffset = $verticesOffset
        facesOffset = $facesOffset
        attrsOffset = $attrsOffset
        familyIdsOffset = $familyIdsOffset
    }
}

function Get-SdrFiles([string]$BaseDir) {
    return @(Get-ChildItem -LiteralPath $BaseDir -File -Filter "S???.SDR" -ErrorAction SilentlyContinue | Sort-Object Name)
}

function Write-Bdr([int]$BatchId, [object[]]$Segments, [string]$TargetPath) {
    if ($Segments.Count -eq 0) { return }

    $logicalCount = [uint16]$Segments.Count
    $vertexCount = 0
    $faceCount = 0

    $minX = [int32]0x7FFFFFFF
    $minY = [int32]0x7FFFFFFF
    $minZ = [int32]0x7FFFFFFF
    $maxX = [int32]([int]0x80000000)
    $maxY = [int32]([int]0x80000000)
    $maxZ = [int32]([int]0x80000000)

    foreach ($seg in $Segments) {
        $vertexCount += $seg.vertexCount
        $faceCount += $seg.faceCount
        $minX = [Math]::Min($minX, $seg.minX)
        $minY = [Math]::Min($minY, $seg.minY)
        $minZ = [Math]::Min($minZ, $seg.minZ)
        $maxX = [Math]::Max($maxX, $seg.maxX)
        $maxY = [Math]::Max($maxY, $seg.maxY)
        $maxZ = [Math]::Max($maxZ, $seg.maxZ)
    }

    $centerX = [int32](([int64]$minX + [int64]$maxX) / 2)
    $centerY = [int32](([int64]$minY + [int64]$maxY) / 2)
    $centerZ = [int32](([int64]$minZ + [int64]$maxZ) / 2)

    $headerSize = [uint32]92
    $segmentIdsBytes = [uint32]($logicalCount * 2)
    $verticesBytes = [uint32]($vertexCount * 12)
    $facesBytes = [uint32]($faceCount * 24)
    $attrsBytes = [uint32]($faceCount * 16)
    $familyBytes = [uint32]($faceCount * 2)
    $rankBytes = [uint32]$faceCount

    $segmentIdsOffset = Align-4 $headerSize
    $verticesOffset = Align-4 ($segmentIdsOffset + $segmentIdsBytes)
    $facesOffset = Align-4 ($verticesOffset + $verticesBytes)
    $attrsOffset = Align-4 ($facesOffset + $facesBytes)
    $familyOffset = Align-4 ($attrsOffset + $attrsBytes)
    $rankOffset = Align-4 ($familyOffset + $familyBytes)

    $outFs = [System.IO.File]::Open($TargetPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($outFs)

        Write-U32 $bw 0x31524442
        Write-U16 $bw 1
        Write-U16 $bw 92
        Write-U16 $bw ([uint16]$BatchId)
        Write-U16 $bw 0
        Write-U16 $bw $logicalCount
        Write-U16 $bw 0
        Write-U32 $bw ([uint32]$vertexCount)
        Write-U32 $bw ([uint32]$faceCount)
        Write-I32 $bw $centerX
        Write-I32 $bw $centerY
        Write-I32 $bw $centerZ
        Write-I32 $bw $minX
        Write-I32 $bw $minY
        Write-I32 $bw $minZ
        Write-I32 $bw $maxX
        Write-I32 $bw $maxY
        Write-I32 $bw $maxZ
        Write-U32 $bw $segmentIdsOffset
        Write-U32 $bw $verticesOffset
        Write-U32 $bw $facesOffset
        Write-U32 $bw $attrsOffset
        Write-U32 $bw $familyOffset
        Write-U32 $bw $rankOffset
        Write-U32 $bw 0
        Write-U32 $bw 0

        while ($bw.BaseStream.Position -lt $segmentIdsOffset) { $bw.Write([byte]0) }

        foreach ($seg in $Segments) {
            Write-U16 $bw ([uint16]$seg.segmentId)
        }

        while ($bw.BaseStream.Position -lt $verticesOffset) { $bw.Write([byte]0) }

        $orderedIndices = @()
        for ($si = $Segments.Count - 1; $si -ge 0; $si--) {
            $orderedIndices += $si
        }

        $vertexBase = 0
        foreach ($segIndex in $orderedIndices) {
            $seg = $Segments[$segIndex]
            $src = $seg.bytes
            $count = $seg.vertexCount * 12
            $bw.Write($src, $seg.verticesOffset, $count)
        }

        while ($bw.BaseStream.Position -lt $facesOffset) { $bw.Write([byte]0) }

        $vertexBase = 0
        foreach ($segIndex in $orderedIndices) {
            $seg = $Segments[$segIndex]
            $src = $seg.bytes
            for ($fi = 0; $fi -lt $seg.faceCount; $fi++) {
                $off = $seg.facesOffset + ($fi * 24)
                $srcV0 = [uint32](Read-U16 $src ($off + 0))
                $srcV1 = [uint32](Read-U16 $src ($off + 2))
                $srcV2 = [uint32](Read-U16 $src ($off + 4))
                $srcV3 = [uint32](Read-U16 $src ($off + 6))
                $v0 = [uint16]($srcV0 + [uint32]$vertexBase)
                $v1 = [uint16]($srcV1 + [uint32]$vertexBase)
                $v2 = [uint16]($srcV2 + [uint32]$vertexBase)
                $v3 = [uint16]($srcV3 + [uint32]$vertexBase)
                Write-U16 $bw $v0
                Write-U16 $bw $v1
                Write-U16 $bw $v2
                Write-U16 $bw $v3
                $bw.Write($src, $off + 8, 16)
            }
            $vertexBase += $seg.vertexCount
        }

        while ($bw.BaseStream.Position -lt $attrsOffset) { $bw.Write([byte]0) }

        foreach ($segIndex in $orderedIndices) {
            $seg = $Segments[$segIndex]
            $count = $seg.faceCount * 16
            $bw.Write($seg.bytes, $seg.attrsOffset, $count)
        }

        while ($bw.BaseStream.Position -lt $familyOffset) { $bw.Write([byte]0) }

        foreach ($segIndex in $orderedIndices) {
            $seg = $Segments[$segIndex]
            $count = $seg.faceCount * 2
            $bw.Write($seg.bytes, $seg.familyIdsOffset, $count)
        }

        while ($bw.BaseStream.Position -lt $rankOffset) { $bw.Write([byte]0) }

        foreach ($segIndex in $orderedIndices) {
            $seg = $Segments[$segIndex]
            for ($fi = 0; $fi -lt $seg.faceCount; $fi++) {
                $bw.Write([byte]$segIndex)
            }
        }

        $bw.Flush()
    }
    finally {
        $outFs.Close()
    }
}

if (-not (Test-Path -LiteralPath $DataDir)) { throw "DataDir nao encontrado: $DataDir" }
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
if ($BatchSize -le 0) { throw "BatchSize deve ser maior que zero." }

$files = Get-SdrFiles -BaseDir $DataDir
if ($files.Count -eq 0) { throw "Nenhum S???.SDR encontrado em $DataDir" }

$segments = @()
foreach ($file in $files) {
    $segments += (Load-Sdr -Path $file.FullName)
}

$batchId = 1
for ($i = 0; $i -lt $segments.Count; $i += $BatchSize) {
    $end = [Math]::Min($i + $BatchSize - 1, $segments.Count - 1)
    $slice = @($segments[$i..$end])
    $firstId = [int]$slice[0].segmentId
    $lastId = [int]$slice[$slice.Count - 1].segmentId
    $name = ("B{0:D3}_{1:D3}.BDR" -f $firstId, $lastId)
    $outPath = Join-Path $OutDir $name
    Write-Bdr -BatchId $batchId -Segments $slice -TargetPath $outPath
    Write-Host ("OK BDR {0} segs:{1} -> {2}" -f $batchId, $slice.Count, $name)
    $batchId++
}
