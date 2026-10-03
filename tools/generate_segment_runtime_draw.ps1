param(
    [string]$DataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [int]$SegmentId = 0,
    # "" => S###.SDR/RDR ; "L" => S###L.SDR/RDR (mid/far design mesh)
    [string]$AssetTag = "",
    [switch]$AllSegments = $false,
    [string]$LightingDir = "",
    [switch]$RequireBakedLighting = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-U16([byte[]]$Bytes, [int]$Offset) { return [uint16][System.BitConverter]::ToUInt16($Bytes, $Offset) }
function Read-U32([byte[]]$Bytes, [int]$Offset) { return [uint32][System.BitConverter]::ToUInt32($Bytes, $Offset) }
function Read-I32([byte[]]$Bytes, [int]$Offset) { return [int32][System.BitConverter]::ToInt32($Bytes, $Offset) }
function Write-U8([System.IO.BinaryWriter]$Bw, [byte]$Value) { $Bw.Write($Value) }
function Write-U16([System.IO.BinaryWriter]$Bw, [uint16]$Value) { $Bw.Write($Value) }
function Write-U32([System.IO.BinaryWriter]$Bw, [uint32]$Value) { $Bw.Write($Value) }
function Write-I32([System.IO.BinaryWriter]$Bw, [int32]$Value) { $Bw.Write($Value) }
function Align-4([uint32]$Value) { return [uint32](($Value + 3) -band (-bnot 3)) }

$CL32KRGB = 40
$CL_Half = 2
$CL_Trans = 3
$MESHon = 256
$UseLight = 8
$UseGouraud = 128
$FUNC_Polygon = 4
$ECdis = 128
$SPdis = 64
$sprPolygon = [uint16]$FUNC_Polygon

function Resolve-SegmentList([string]$BaseDir, [int]$SingleId, [bool]$UseAll, [string]$Tag) {
    if ($UseAll) {
        $ids = New-Object System.Collections.Generic.List[int]
        $filter = if ($Tag -eq "L") { "S???L.SDR" } else { "S???.SDR" }
        $files = @(Get-ChildItem -LiteralPath $BaseDir -File -Filter $filter -ErrorAction SilentlyContinue | Sort-Object Name)
        foreach ($file in $files) {
            if ($Tag -eq "L") {
                if ($file.BaseName -match '^S(\d{3})L$') {
                    $ids.Add([int]$Matches[1]) | Out-Null
                }
            }
            else {
                if ($file.BaseName -match '^S(\d{3})$') {
                    $ids.Add([int]$Matches[1]) | Out-Null
                }
            }
        }
        return @($ids.ToArray())
    }

    if ($SingleId -le 0) {
        throw "Informe -SegmentId ou use -AllSegments."
    }
    return @($SingleId)
}

function Load-Sdr([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "SDR nao encontrado: $Path"
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 80) { throw "SDR pequeno demais: $Path" }

    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    if ($magic -ne 0x31524453) { throw "Magic SDR invalido em $Path" }
    if ($version -ne 1) { throw "Versao SDR invalida em $Path" }

    return [pscustomobject]@{
        bytes = $bytes
        segmentId = [int](Read-U16 $bytes 8)
        vertexCount = [int](Read-U32 $bytes 12)
        faceCount = [int](Read-U32 $bytes 16)
        centerX = [int32](Read-I32 $bytes 20)
        centerY = [int32](Read-I32 $bytes 24)
        centerZ = [int32](Read-I32 $bytes 28)
        minX = [int32](Read-I32 $bytes 32)
        minY = [int32](Read-I32 $bytes 36)
        minZ = [int32](Read-I32 $bytes 40)
        maxX = [int32](Read-I32 $bytes 44)
        maxY = [int32](Read-I32 $bytes 48)
        maxZ = [int32](Read-I32 $bytes 52)
        verticesOffset = [int](Read-U32 $bytes 56)
        facesOffset = [int](Read-U32 $bytes 60)
        attrsOffset = [int](Read-U32 $bytes 64)
        familyIdsOffset = [int](Read-U32 $bytes 68)
    }
}

function Load-Lit([string]$Path, [int]$ExpectedSegmentId, [int]$ExpectedFaceCount) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 32) { throw "LIT pequeno demais: $Path" }
    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    $headerSize = Read-U16 $bytes 6
    $segmentId = Read-U16 $bytes 8
    $faceCount = Read-U32 $bytes 12
    $entriesOffset = Read-U32 $bytes 16
    $entrySize = Read-U16 $bytes 20
    if ($magic -ne 0x3154494C) { throw "Magic LIT invalido em $Path" }
    if ($version -ne 1 -or $headerSize -lt 32) { throw "Versao/header LIT invalido em $Path" }
    if ([int]$segmentId -ne $ExpectedSegmentId) { throw "segmentId LIT divergente em $Path" }
    if ([int]$faceCount -ne $ExpectedFaceCount) { throw "faceCount LIT divergente em $Path" }
    if ($entrySize -ne 4) { throw "entrySize LIT invalido em $Path" }
    $payloadBytes = [uint32]($faceCount * $entrySize)
    if ([uint64]$entriesOffset + [uint64]$payloadBytes -gt [uint64]$bytes.Length) {
        throw "payload LIT fora do arquivo em $Path"
    }
    for ($i = 0; $i -lt $payloadBytes; $i++) {
        if ($bytes[[int]$entriesOffset + $i] -gt 31) {
            throw "nivel LIT fora de 0..31 em $Path offset=$i"
        }
    }
    return [pscustomobject]@{
        bytes = $bytes
        entriesOffset = [int]$entriesOffset
        payloadBytes = [int]$payloadBytes
        faceCount = [int]$faceCount
    }
}

function Clamp-SortMode([uint16]$Raw) {
    if ($Raw -gt 3) { return [byte]0 }
    return [byte](3 - $Raw)
}

function Build-RuntimeAttr([byte[]]$Bytes, [int]$Offset) {
    $visibility = [uint16](Read-U16 $Bytes ($Offset + 0))
    $sortMode = [uint16](Read-U16 $Bytes ($Offset + 2))
    $baseColor = [uint16](Read-U16 $Bytes ($Offset + 4))
    $colorMode = [uint16](Read-U16 $Bytes ($Offset + 6))
    $gouraudMode = [uint16](Read-U16 $Bytes ($Offset + 8))
    $spriteMode = [uint16](Read-U16 $Bytes ($Offset + 10))
    $useLight = [uint16](Read-U16 $Bytes ($Offset + 12))
    $flags = [uint16](Read-U16 $Bytes ($Offset + 14))

    $sortFieldBase = Clamp-SortMode $sortMode
    $resolvedSprite = if ($spriteMode -ne 0) { [uint16]$spriteMode } else { [uint16]$sprPolygon }
    $resolvedLight = if ($useLight -ne 0) { [uint16]$UseLight } else { [uint16]$UseGouraud }
    $keepFlags = [uint16]($flags -band ($CL_Trans -bor $CL_Half -bor $MESHon))
    $resolvedColorMode = if ($colorMode -ne 0) { [uint16]$colorMode } else { [uint16]$CL32KRGB }
    $resolvedDisplay = [uint16]($resolvedColorMode -bor $keepFlags)
    $resolvedGouraud = if ($gouraudMode -ne 0) { [uint16]$gouraudMode } else { [uint16]$CL32KRGB }
    $resolvedVisibility = if ($visibility -ne 0) { [byte]1 } else { [byte]0 }

    return [pscustomobject]@{
        visibility = $resolvedVisibility
        sort = [byte]([uint32]$sortFieldBase -bor $resolvedLight)
        texture = [uint16]0
        display = [uint16]$resolvedDisplay
        colorMode = [uint16]$baseColor
        gouraud = [uint16]$resolvedGouraud
        direction = [uint16]($resolvedSprite -band 0x003f)
    }
}

function Write-Rdr([object]$Sdr, [object]$Lit, [string]$TargetPath) {
    $headerSize = [uint32]80
    $verticesBytes = [uint32]($Sdr.vertexCount * 12)
    $facesBytes = [uint32]($Sdr.faceCount * 24)
    $attrsBytes = [uint32]($Sdr.faceCount * 12)
    $familyBytes = [uint32]($Sdr.faceCount * 2)

    $verticesOffset = Align-4 $headerSize
    $facesOffset = Align-4 ($verticesOffset + $verticesBytes)
    $attrsOffset = Align-4 ($facesOffset + $facesBytes)
    $familyOffset = Align-4 ($attrsOffset + $attrsBytes)
    $lightingOffset = [uint32]0
    $lightingCount = [uint32]0
    $headerFlags = [uint16]0
    if ($null -ne $Lit) {
        $lightingOffset = Align-4 ($familyOffset + $familyBytes)
        $lightingCount = [uint32]$Sdr.faceCount
        $headerFlags = [uint16]($headerFlags -bor 0x0001)
    }

    $targetDir = Split-Path -Parent $TargetPath
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    }

    $fs = [System.IO.File]::Open($TargetPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)

        Write-U32 $bw 0x31524452
        Write-U16 $bw 1
        Write-U16 $bw 80
        Write-U16 $bw ([uint16]$Sdr.segmentId)
        Write-U16 $bw $headerFlags
        Write-U32 $bw ([uint32]$Sdr.vertexCount)
        Write-U32 $bw ([uint32]$Sdr.faceCount)
        Write-I32 $bw ([int32]$Sdr.centerX)
        Write-I32 $bw ([int32]$Sdr.centerY)
        Write-I32 $bw ([int32]$Sdr.centerZ)
        Write-I32 $bw ([int32]$Sdr.minX)
        Write-I32 $bw ([int32]$Sdr.minY)
        Write-I32 $bw ([int32]$Sdr.minZ)
        Write-I32 $bw ([int32]$Sdr.maxX)
        Write-I32 $bw ([int32]$Sdr.maxY)
        Write-I32 $bw ([int32]$Sdr.maxZ)
        Write-U32 $bw $verticesOffset
        Write-U32 $bw $facesOffset
        Write-U32 $bw $attrsOffset
        Write-U32 $bw $familyOffset
        Write-U32 $bw $lightingOffset
        Write-U32 $bw $lightingCount

        while ($fs.Position -lt $verticesOffset) { $bw.Write([byte]0) }
        $bw.Write($Sdr.bytes, $Sdr.verticesOffset, $verticesBytes)

        while ($fs.Position -lt $facesOffset) { $bw.Write([byte]0) }
        $bw.Write($Sdr.bytes, $Sdr.facesOffset, $facesBytes)

        while ($fs.Position -lt $attrsOffset) { $bw.Write([byte]0) }
        for ($fi = 0; $fi -lt $Sdr.faceCount; $fi++) {
            $attr = Build-RuntimeAttr $Sdr.bytes ($Sdr.attrsOffset + ($fi * 16))
            Write-U8 $bw $attr.visibility
            Write-U8 $bw $attr.sort
            Write-U16 $bw $attr.texture
            Write-U16 $bw $attr.display
            Write-U16 $bw $attr.colorMode
            Write-U16 $bw $attr.gouraud
            Write-U16 $bw $attr.direction
        }

        while ($fs.Position -lt $familyOffset) { $bw.Write([byte]0) }
        $bw.Write($Sdr.bytes, $Sdr.familyIdsOffset, $familyBytes)
        if ($null -ne $Lit) {
            while ($fs.Position -lt $lightingOffset) { $bw.Write([byte]0) }
            $bw.Write($Lit.bytes, $Lit.entriesOffset, $Lit.payloadBytes)
        }
        $bw.Flush()
    }
    finally {
        $fs.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $DataDir)) { throw "DataDir nao encontrado: $DataDir" }
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$tag = if ([string]::IsNullOrWhiteSpace($AssetTag)) { "" } else { $AssetTag.Trim().ToUpperInvariant() }
if ($tag -ne "" -and $tag -ne "L") { throw "AssetTag invalido '$AssetTag' (use '' ou 'L')." }

$segmentIds = Resolve-SegmentList -BaseDir $DataDir -SingleId $SegmentId -UseAll:$AllSegments -Tag $tag
$written = 0
$resolvedLightingDir = if ([string]::IsNullOrWhiteSpace($LightingDir)) { $DataDir } else { $LightingDir }

foreach ($id in $segmentIds) {
    $sdrPath = Join-Path $DataDir ("S{0:D3}{1}.SDR" -f $id, $tag)
    $outPath = Join-Path $OutDir ("S{0:D3}{1}.RDR" -f $id, $tag)
    $litPath = Join-Path $resolvedLightingDir ("S{0:D3}{1}.LIT" -f $id, $tag)
    $sdr = Load-Sdr -Path $sdrPath
    $lit = Load-Lit -Path $litPath -ExpectedSegmentId $sdr.segmentId -ExpectedFaceCount $sdr.faceCount
    if ($RequireBakedLighting -and $null -eq $lit) {
        throw "Iluminacao baked obrigatoria ausente: $litPath"
    }
    Write-Rdr -Sdr $sdr -Lit $lit -TargetPath $outPath
    $written++
    Write-Host ("RDR ok: {0} lighting={1}" -f $outPath, ($null -ne $lit))
}

Write-Host ("RDR gerados: {0}" -f $written)
