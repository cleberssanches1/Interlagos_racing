param(
    [string]$DataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [int]$Lod = 8,
    [int]$SegmentId = 0,
    [switch]$AllSegments = $false,
    [switch]$CanonicalizeQuadUvOrder = $false,
    [int]$QuadUvEdgeTolerance = 192,
    [int]$QuadUvHighTolerance = 704,
    [switch]$CanonicalizeHighToleranceAllFamilies = $true,
    [string]$SegmentsMapPath = "",
    [string[]]$CanonicalizeExcludeSourceStems = @(),
    [string[]]$CanonicalizeHighToleranceSourceStems = @("f02164", "f01764", "f04764", "f00764", "f00964", "f03764", "f00864", "f03464")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:canonicalizeExcludeFamilyId = @{}
$script:canonicalizeHighToleranceFamilyId = @{}
$script:manualOrientationFixBySegmentFamily = @{}

function Normalize-SourceStem([string]$Stem) {
    if ([string]::IsNullOrWhiteSpace($Stem)) { return "" }
    return ([regex]::Replace($Stem.Trim().ToLowerInvariant(), '[^a-z0-9]+', ''))
}

function Build-CanonicalizeExcludeFamilyIdMap(
    [string]$MapPath,
    [string[]]$ExcludeStems
) {
    $out = @{}
    if ([string]::IsNullOrWhiteSpace($MapPath)) { return $out }
    if (-not (Test-Path -LiteralPath $MapPath)) { return $out }

    $exclude = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($rawStem in @($ExcludeStems)) {
        $stem = Normalize-SourceStem ([string]$rawStem)
        if ([string]::IsNullOrWhiteSpace($stem)) { continue }
        [void]$exclude.Add($stem)
    }
    if ($exclude.Count -eq 0) { return $out }

    try {
        $json = Get-Content -LiteralPath $MapPath -Raw | ConvertFrom-Json
        foreach ($family in @($json.textureFamilies)) {
            if ($null -eq $family) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "id")) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "sourceStem")) { continue }
            $stem = Normalize-SourceStem ([string]$family.sourceStem)
            if ([string]::IsNullOrWhiteSpace($stem)) { continue }
            if (-not $exclude.Contains($stem)) { continue }
            $out[[uint32]$family.id] = $true
        }
    }
    catch {
        return @{}
    }

    return $out
}

function Build-CanonicalizeFamilyIdMapBySourceStems(
    [string]$MapPath,
    [string[]]$SourceStems
) {
    $out = @{}
    if ([string]::IsNullOrWhiteSpace($MapPath)) { return $out }
    if (-not (Test-Path -LiteralPath $MapPath)) { return $out }

    $targetStems = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($rawStem in @($SourceStems)) {
        $stem = Normalize-SourceStem ([string]$rawStem)
        if ([string]::IsNullOrWhiteSpace($stem)) { continue }
        [void]$targetStems.Add($stem)
    }
    if ($targetStems.Count -eq 0) { return $out }

    try {
        $json = Get-Content -LiteralPath $MapPath -Raw | ConvertFrom-Json
        foreach ($family in @($json.textureFamilies)) {
            if ($null -eq $family) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "id")) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "sourceStem")) { continue }
            $stem = Normalize-SourceStem ([string]$family.sourceStem)
            if ([string]::IsNullOrWhiteSpace($stem)) { continue }
            if (-not $targetStems.Contains($stem)) { continue }
            $out[[uint32]$family.id] = $true
        }
    }
    catch {
        return @{}
    }

    return $out
}

function Build-FamilyIdBySourceStemMap(
    [string]$MapPath
) {
    $out = @{}
    if ([string]::IsNullOrWhiteSpace($MapPath)) { return $out }
    if (-not (Test-Path -LiteralPath $MapPath)) { return $out }

    try {
        $json = Get-Content -LiteralPath $MapPath -Raw | ConvertFrom-Json
        foreach ($family in @($json.textureFamilies)) {
            if ($null -eq $family) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "id")) { continue }
            if (-not ($family.PSObject.Properties.Name -contains "sourceStem")) { continue }
            $stem = Normalize-SourceStem ([string]$family.sourceStem)
            if ([string]::IsNullOrWhiteSpace($stem)) { continue }
            $out[$stem] = [uint32]$family.id
        }
    }
    catch {
        return @{}
    }

    return $out
}

function Add-ManualOrientationRule(
    [hashtable]$Dst,
    [hashtable]$FamilyByStem,
    [int]$StartSeg,
    [int]$EndSeg,
    [string[]]$Stems,
    [string]$Op
) {
    if ($StartSeg -gt $EndSeg) { return }
    for ($sid = $StartSeg; $sid -le $EndSeg; $sid++) {
        if (-not $Dst.ContainsKey($sid)) {
            $Dst[$sid] = @{}
        }
        foreach ($rawStem in @($Stems)) {
            $stem = Normalize-SourceStem $rawStem
            if ([string]::IsNullOrWhiteSpace($stem)) { continue }
            if (-not $FamilyByStem.ContainsKey($stem)) { continue }
            $fid = [uint32]$FamilyByStem[$stem]
            $Dst[$sid][$fid] = $Op
        }
    }
}

function Build-ManualOrientationFixMap(
    [string]$MapPath
) {
    $out = @{}
    $familyByStem = Build-FamilyIdBySourceStemMap -MapPath $MapPath
    if ($familyByStem.Count -eq 0) { return $out }

    # Casos reportados em 2026-04-03:
    # "girada" -> rot90
    # "de cabeça para baixo" -> rot180
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 44  -EndSeg 44  -Stems @("f03164") -Op "rot90"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 53  -EndSeg 53  -Stems @("f02764") -Op "rot90"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 54  -EndSeg 54  -Stems @("f00664") -Op "rot90"

    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 56  -EndSeg 97  -Stems @("f00864") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 78  -EndSeg 78  -Stems @("f00764") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 81  -EndSeg 81  -Stems @("f00764") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 102 -EndSeg 102 -Stems @("f00764") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 112 -EndSeg 112 -Stems @("f03164") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 120 -EndSeg 120 -Stems @("f00664") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 127 -EndSeg 127 -Stems @("f02764") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 129 -EndSeg 129 -Stems @("f00664", "f00764") -Op "rot180"
    Add-ManualOrientationRule -Dst $out -FamilyByStem $familyByStem -StartSeg 270 -EndSeg 305 -Stems @("f00864") -Op "rot180"

    return $out
}

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [uint16][System.BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32][System.BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-I16([byte[]]$Bytes, [int]$Offset) {
    return [int16][System.BitConverter]::ToInt16($Bytes, $Offset)
}

function Read-I32([byte[]]$Bytes, [int]$Offset) {
    return [int32][System.BitConverter]::ToInt32($Bytes, $Offset)
}

function Write-U16([System.IO.BinaryWriter]$Bw, [uint16]$Value) {
    $Bw.Write($Value)
}

function Write-U32([System.IO.BinaryWriter]$Bw, [uint32]$Value) {
    $Bw.Write($Value)
}

function Write-I32([System.IO.BinaryWriter]$Bw, [int32]$Value) {
    $Bw.Write($Value)
}

function Align-4([uint32]$Value) {
    return [uint32](($Value + 3) -band (-bnot 3))
}

function Resolve-SegmentList([string]$BaseDir, [int]$SingleId, [bool]$UseAll) {
    if ($UseAll) {
        $ids = New-Object System.Collections.Generic.List[int]
        $geoFiles = @(Get-ChildItem -LiteralPath $BaseDir -File -Filter "S???.GEO" -ErrorAction SilentlyContinue | Sort-Object Name)
        foreach ($file in $geoFiles) {
            if ($file.BaseName -match '^S(\d{3})$') {
                $ids.Add([int]$Matches[1]) | Out-Null
            }
        }
        return @($ids.ToArray())
    }

    if ($SingleId -le 0) {
        throw "Informe -SegmentId ou use -AllSegments."
    }

    return @($SingleId)
}

function Load-Geo([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "GEO nao encontrado: $Path"
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 24) {
        throw "GEO pequeno demais: $Path"
    }

    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    if ($magic -ne 0x314F4547) { throw "Magic GEO invalido em $Path" }
    if ($version -ne 1) { throw "Versao GEO invalida em $Path" }

    $segmentId = [int](Read-U32 $bytes 8)
    $payloadBytes = [int](Read-U32 $bytes 12)
    if (($payloadBytes + 16) -gt $bytes.Length) {
        throw "Payload GEO invalido em $Path"
    }

    $vertexCount = [int](Read-U32 $bytes 16)
    $faceCount = [int](Read-U32 $bytes 20)
    $vertexOffset = 24
    $faceOffset = $vertexOffset + ($vertexCount * 12)
    $needBytes = $faceOffset + ($faceCount * 28)
    if ($needBytes -gt $bytes.Length) {
        throw "Tabela GEO truncada em $Path"
    }

    $verts = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $vertexCount; $i++) {
        $off = $vertexOffset + ($i * 12)
        $verts.Add([pscustomobject]@{
            x = [int32](Read-I32 $bytes ($off + 0))
            y = [int32](Read-I32 $bytes ($off + 4))
            z = [int32](Read-I32 $bytes ($off + 8))
        }) | Out-Null
    }

    $faces = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $faceCount; $i++) {
        $off = $faceOffset + ($i * 28)
        $face = [pscustomobject]@{
            vertex = @(
                [uint16](Read-U16 $bytes ($off + 0)),
                [uint16](Read-U16 $bytes ($off + 2)),
                [uint16](Read-U16 $bytes ($off + 4)),
                [uint16](Read-U16 $bytes ($off + 6))
            )
            u = @(
                [int16](Read-I16 $bytes ($off + 8)),
                [int16](Read-I16 $bytes ($off + 10)),
                [int16](Read-I16 $bytes ($off + 12)),
                [int16](Read-I16 $bytes ($off + 14))
            )
            v = @(
                [int16](Read-I16 $bytes ($off + 16)),
                [int16](Read-I16 $bytes ($off + 18)),
                [int16](Read-I16 $bytes ($off + 20)),
                [int16](Read-I16 $bytes ($off + 22))
            )
            kind = [byte]$bytes[$off + 24]
        }
        $faces.Add($face) | Out-Null
    }

    return [pscustomobject]@{
        segmentId = $segmentId
        vertexCount = $vertexCount
        faceCount = $faceCount
        verts = $verts
        faces = $faces
    }
}

function Load-Mat([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "MAT nao encontrado: $Path"
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20) {
        throw "MAT pequeno demais: $Path"
    }

    $magic = Read-U32 $bytes 0
    $version = Read-U16 $bytes 4
    if ($magic -ne 0x3154414D) { throw "Magic MAT invalido em $Path" }
    if ($version -ne 1) { throw "Versao MAT invalida em $Path" }

    $segmentId = [int](Read-U32 $bytes 8)
    $payloadBytes = [int](Read-U32 $bytes 12)
    if (($payloadBytes + 16) -gt $bytes.Length) {
        throw "Payload MAT invalido em $Path"
    }

    $faceCount = [int](Read-U32 $bytes 16)
    $bindingOffset = 20
    $needBytes = $bindingOffset + ($faceCount * 4)
    if ($needBytes -gt $bytes.Length) {
        throw "Tabela MAT truncada em $Path"
    }

    $families = New-Object System.Collections.Generic.List[uint32]
    for ($i = 0; $i -lt $faceCount; $i++) {
        $off = $bindingOffset + ($i * 4)
        $families.Add([uint32](Read-U32 $bytes $off)) | Out-Null
    }

    return [pscustomobject]@{
        segmentId = $segmentId
        faceCount = $faceCount
        families = $families
    }
}

function Reorder-QuadVerticesFromUv([object]$Face, [int]$EdgeTolerance) {
    $result = @([uint16]$Face.vertex[0], [uint16]$Face.vertex[1], [uint16]$Face.vertex[2], [uint16]$Face.vertex[3])

    $minU = [int]$Face.u[0]
    $maxU = [int]$Face.u[0]
    $minV = [int]$Face.v[0]
    $maxV = [int]$Face.v[0]
    for ($i = 1; $i -lt 4; $i++) {
        $minU = [Math]::Min($minU, [int]$Face.u[$i])
        $maxU = [Math]::Max($maxU, [int]$Face.u[$i])
        $minV = [Math]::Min($minV, [int]$Face.v[$i])
        $maxV = [Math]::Max($maxV, [int]$Face.v[$i])
    }

    if ($minU -eq $maxU -or $minV -eq $maxV) {
        return ,$result
    }

    # Candidate permutations:
    # - first 4: rotations (preserve winding)
    # - last 4: mirrored variants (flip)
    $permutations = @(
        @(0, 1, 2, 3),
        @(1, 2, 3, 0),
        @(2, 3, 0, 1),
        @(3, 0, 1, 2),
        @(0, 3, 2, 1),
        @(3, 2, 1, 0),
        @(2, 1, 0, 3),
        @(1, 0, 3, 2)
    )

    # Two V conventions:
    # - vMinTop = false: (minV at top)
    # - vMinTop = true : (maxV at top)
    $targetSets = @(
        [pscustomobject]@{
            u = @($minU, $maxU, $maxU, $minU)
            v = @($minV, $minV, $maxV, $maxV)
        },
        [pscustomobject]@{
            u = @($minU, $maxU, $maxU, $minU)
            v = @($maxV, $maxV, $minV, $minV)
        }
    )

    $scale = [double][Math]::Max(1, ($maxU - $minU) + ($maxV - $minV))
    $tol = [double][Math]::Max(0, $EdgeTolerance)

    $bestAllScore = [double]::PositiveInfinity
    $bestAllMaxCorner = [double]::PositiveInfinity
    $bestAllPermIndex = -1
    $bestAllTargetIndex = -1

    $bestRotScore = [double]::PositiveInfinity
    $bestRotMaxCorner = [double]::PositiveInfinity
    $bestRotPermIndex = -1
    $bestRotTargetIndex = -1

    for ($pi = 0; $pi -lt $permutations.Count; $pi++) {
        $perm = $permutations[$pi]
        for ($ti = 0; $ti -lt $targetSets.Count; $ti++) {
            $target = $targetSets[$ti]
            $sumScore = [double]0.0
            $maxCorner = [double]0.0
            for ($corner = 0; $corner -lt 4; $corner++) {
                $src = [int]$perm[$corner]
                $du = [Math]::Abs(([double][int]$Face.u[$src]) - [double][int]$target.u[$corner])
                $dv = [Math]::Abs(([double][int]$Face.v[$src]) - [double][int]$target.v[$corner])
                $cornerScore = [double]($du + $dv)
                $sumScore += $cornerScore
                if ($cornerScore -gt $maxCorner) { $maxCorner = $cornerScore }
            }

            if ($sumScore -lt $bestAllScore) {
                $bestAllScore = $sumScore
                $bestAllMaxCorner = $maxCorner
                $bestAllPermIndex = $pi
                $bestAllTargetIndex = $ti
            }

            if ($pi -lt 4 -and $sumScore -lt $bestRotScore) {
                $bestRotScore = $sumScore
                $bestRotMaxCorner = $maxCorner
                $bestRotPermIndex = $pi
                $bestRotTargetIndex = $ti
            }
        }
    }

    if ($bestAllPermIndex -lt 0) {
        return ,$result
    }

    # Prefer rotation-only unless mirrored mapping is clearly better.
    $useRotationOnly = $false
    if ($bestRotPermIndex -ge 0) {
        $rotationBias = $scale * 0.05
        if ($bestRotScore -le ($bestAllScore + $rotationBias)) {
            $useRotationOnly = $true
        }
    }

    $chosenPermIndex = if ($useRotationOnly) { $bestRotPermIndex } else { $bestAllPermIndex }
    $chosenMaxCorner = if ($useRotationOnly) { $bestRotMaxCorner } else { $bestAllMaxCorner }

    # Confidence gate: avoid changing faces with ambiguous/non-rectangular UV layout.
    $maxAllowed = [double][Math]::Max($tol, ($scale * 0.35))
    if ($chosenMaxCorner -gt $maxAllowed) {
        return ,$result
    }

    $chosenPerm = $permutations[$chosenPermIndex]
    return @(
        [uint16]$Face.vertex[[int]$chosenPerm[0]],
        [uint16]$Face.vertex[[int]$chosenPerm[1]],
        [uint16]$Face.vertex[[int]$chosenPerm[2]],
        [uint16]$Face.vertex[[int]$chosenPerm[3]]
    )
}

function Build-FaceNormal([object[]]$Verts, [uint16[]]$Indices) {
    $ia = [int]$Indices[0]
    $ib = [int]$Indices[1]
    $ic = [int]$Indices[2]
    if ($ia -lt 0 -or $ib -lt 0 -or $ic -lt 0) {
        return @(0, 0, 0)
    }
    if ($ia -ge $Verts.Count -or $ib -ge $Verts.Count -or $ic -ge $Verts.Count) {
        return @(0, 0, 0)
    }

    $a = $Verts[$ia]
    $b = $Verts[$ib]
    $c = $Verts[$ic]

    $abx = [int64]$b.x - [int64]$a.x
    $aby = [int64]$b.y - [int64]$a.y
    $abz = [int64]$b.z - [int64]$a.z
    $acx = [int64]$c.x - [int64]$a.x
    $acy = [int64]$c.y - [int64]$a.y
    $acz = [int64]$c.z - [int64]$a.z

    $rawNx = [int64]((($aby * $acz) - ($abz * $acy)) -shr 16)
    $rawNy = [int64]((($abz * $acx) - ($abx * $acz)) -shr 16)
    $rawNz = [int64]((($abx * $acy) - ($aby * $acx)) -shr 16)

    if ($rawNx -lt [int64][int]::MinValue) { $rawNx = [int64][int]::MinValue }
    if ($rawNx -gt [int64][int]::MaxValue) { $rawNx = [int64][int]::MaxValue }
    if ($rawNy -lt [int64][int]::MinValue) { $rawNy = [int64][int]::MinValue }
    if ($rawNy -gt [int64][int]::MaxValue) { $rawNy = [int64][int]::MaxValue }
    if ($rawNz -lt [int64][int]::MinValue) { $rawNz = [int64][int]::MinValue }
    if ($rawNz -gt [int64][int]::MaxValue) { $rawNz = [int64][int]::MaxValue }

    $nx = [int32]$rawNx
    $ny = [int32]$rawNy
    $nz = [int32]$rawNz

    return @($nx, $ny, $nz)
}

function Build-BaseColor([uint32]$FamilyId) {
    $m = [uint16]($FamilyId -band 0x1F)
    if ($m -eq 0) { $m = 0x1F }
    return [uint16](0x8400 -bor $m)
}

function Apply-QuadOrientationOp([uint16[]]$Indices, [string]$Op) {
    if ($null -eq $Indices -or $Indices.Count -lt 4) {
        return $Indices
    }
    switch ($Op) {
        "rot90" {
            return @(
                [uint16]$Indices[1],
                [uint16]$Indices[2],
                [uint16]$Indices[3],
                [uint16]$Indices[0]
            )
        }
        "rot180" {
            return @(
                [uint16]$Indices[2],
                [uint16]$Indices[3],
                [uint16]$Indices[0],
                [uint16]$Indices[1]
            )
        }
        "rot270" {
            return @(
                [uint16]$Indices[3],
                [uint16]$Indices[0],
                [uint16]$Indices[1],
                [uint16]$Indices[2]
            )
        }
        default {
            return $Indices
        }
    }
}

function Write-Sdr([int]$Id, [object]$Geo, [object]$Mat, [string]$TargetPath) {
    if ($Geo.segmentId -ne $Mat.segmentId) {
        throw ("SegmentId divergente GEO/MAT no segmento {0}" -f $Id)
    }
    if ($Geo.faceCount -ne $Mat.faceCount) {
        throw ("FaceCount divergente GEO/MAT no segmento {0}" -f $Id)
    }

    $vertexCount = [uint32]$Geo.vertexCount
    $faceCount = [uint32]$Geo.faceCount

    $minX = [int32]0x7FFFFFFF
    $minY = [int32]0x7FFFFFFF
    $minZ = [int32]0x7FFFFFFF
    $maxX = [int32]([int]0x80000000)
    $maxY = [int32]([int]0x80000000)
    $maxZ = [int32]([int]0x80000000)

    foreach ($v in $Geo.verts) {
        $minX = [Math]::Min($minX, [int32]$v.x)
        $minY = [Math]::Min($minY, [int32]$v.y)
        $minZ = [Math]::Min($minZ, [int32]$v.z)
        $maxX = [Math]::Max($maxX, [int32]$v.x)
        $maxY = [Math]::Max($maxY, [int32]$v.y)
        $maxZ = [Math]::Max($maxZ, [int32]$v.z)
    }

    $centerX = [int32](([int64]$minX + [int64]$maxX) / 2)
    $centerY = [int32](([int64]$minY + [int64]$maxY) / 2)
    $centerZ = [int32](([int64]$minZ + [int64]$maxZ) / 2)

    $headerSize = [uint32]80
    $verticesBytes = [uint32]($vertexCount * 12)
    $facesBytes = [uint32]($faceCount * 24)
    $attrsBytes = [uint32]($faceCount * 16)
    $familyBytes = [uint32]($faceCount * 2)

    $verticesOffset = Align-4 $headerSize
    $facesOffset = Align-4 ($verticesOffset + $verticesBytes)
    $attrsOffset = Align-4 ($facesOffset + $facesBytes)
    $familyOffset = Align-4 ($attrsOffset + $attrsBytes)

    $targetDir = Split-Path -Parent $TargetPath
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    }

    $fs = [System.IO.File]::Open($TargetPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)

        # HeaderV1
        Write-U32 $bw 0x31524453
        Write-U16 $bw 1
        Write-U16 $bw 80
        Write-U16 $bw ([uint16]$Id)
        Write-U16 $bw 0
        Write-U32 $bw $vertexCount
        Write-U32 $bw $faceCount
        Write-I32 $bw $centerX
        Write-I32 $bw $centerY
        Write-I32 $bw $centerZ
        Write-I32 $bw $minX
        Write-I32 $bw $minY
        Write-I32 $bw $minZ
        Write-I32 $bw $maxX
        Write-I32 $bw $maxY
        Write-I32 $bw $maxZ
        Write-U32 $bw $verticesOffset
        Write-U32 $bw $facesOffset
        Write-U32 $bw $attrsOffset
        Write-U32 $bw $familyOffset
        Write-U32 $bw 0
        Write-U32 $bw 0

        while ($fs.Position -lt $verticesOffset) { $bw.Write([byte]0) }

        # Vertex[]
        foreach ($v in $Geo.verts) {
            Write-I32 $bw ([int32]$v.x)
            Write-I32 $bw ([int32]$v.y)
            Write-I32 $bw ([int32]$v.z)
        }

        while ($fs.Position -lt $facesOffset) { $bw.Write([byte]0) }

        # Face[]
        for ($fi = 0; $fi -lt $Geo.faces.Count; $fi++) {
            $srcFace = $Geo.faces[$fi]
            $familyId = [uint32]$Mat.families[$fi]

            [uint16[]]$indices = @(
                [uint16]$srcFace.vertex[0],
                [uint16]$srcFace.vertex[1],
                [uint16]$srcFace.vertex[2],
                [uint16]$srcFace.vertex[3]
            )

            if ([int]$srcFace.kind -eq 4) {
                $allowCanonicalize = $CanonicalizeQuadUvOrder
                if ($allowCanonicalize -and $script:canonicalizeExcludeFamilyId.ContainsKey($familyId)) {
                    $allowCanonicalize = $false
                }
                if ($allowCanonicalize) {
                    $effectiveTol = [int][Math]::Max(0, $QuadUvEdgeTolerance)
                    $useHighTol = $CanonicalizeHighToleranceAllFamilies
                    if (-not $useHighTol -and $script:canonicalizeHighToleranceFamilyId.ContainsKey($familyId)) {
                        $useHighTol = $true
                    }
                    if ($useHighTol) {
                        $effectiveTol = [int][Math]::Max($effectiveTol, [int][Math]::Max(0, $QuadUvHighTolerance))
                    }
                    $indices = [uint16[]](Reorder-QuadVerticesFromUv $srcFace $effectiveTol)
                }

                if ($script:manualOrientationFixBySegmentFamily.ContainsKey($Id)) {
                    $segFix = $script:manualOrientationFixBySegmentFamily[$Id]
                    if ($segFix -and $segFix.ContainsKey($familyId)) {
                        $op = [string]$segFix[$familyId]
                        $indices = [uint16[]](Apply-QuadOrientationOp $indices $op)
                    }
                }
            } else {
                $indices[3] = $indices[2]
            }

            for ($i = 0; $i -lt 4; $i++) {
                if ([int]$indices[$i] -ge $Geo.verts.Count) {
                    throw ("Indice de vertice invalido no segmento {0} face {1}" -f $Id, $fi)
                }
            }

            $normal = Build-FaceNormal $Geo.verts $indices

            Write-U16 $bw $indices[0]
            Write-U16 $bw $indices[1]
            Write-U16 $bw $indices[2]
            Write-U16 $bw $indices[3]
            Write-I32 $bw ([int32]$normal[0])
            Write-I32 $bw ([int32]$normal[1])
            Write-I32 $bw ([int32]$normal[2])
            $kindValue = if ([int]$srcFace.kind -eq 3) { [byte]3 } else { [byte]4 }
            $bw.Write($kindValue)
            $bw.Write([byte]0)
            Write-U16 $bw 0
        }

        while ($fs.Position -lt $attrsOffset) { $bw.Write([byte]0) }

        # AttrBase[]
        for ($fi = 0; $fi -lt $Geo.faces.Count; $fi++) {
            $familyId = [uint32]$Mat.families[$fi]
            Write-U16 $bw 1 # VisibilityMode::DoubleSided
            Write-U16 $bw 0 # SortMode::Center
            Write-U16 $bw (Build-BaseColor $familyId)
            Write-U16 $bw 0
            Write-U16 $bw 0
            Write-U16 $bw 0
            Write-U16 $bw 1
            Write-U16 $bw 0
        }

        while ($fs.Position -lt $familyOffset) { $bw.Write([byte]0) }

        # uint16_t faceFamilyIds[]
        foreach ($familyId in $Mat.families) {
            Write-U16 $bw ([uint16]([uint32]$familyId -band 0xFFFF))
        }

        $bw.Flush()
    }
    finally {
        $fs.Dispose()
    }
}

$segmentIds = Resolve-SegmentList -BaseDir $DataDir -SingleId $SegmentId -UseAll:$AllSegments
$written = 0

if ([string]::IsNullOrWhiteSpace($SegmentsMapPath)) {
    $SegmentsMapPath = Join-Path $DataDir "segments_map.json"
}
$script:canonicalizeExcludeFamilyId = Build-CanonicalizeExcludeFamilyIdMap `
    -MapPath $SegmentsMapPath `
    -ExcludeStems $CanonicalizeExcludeSourceStems
if (-not $CanonicalizeHighToleranceAllFamilies) {
    $script:canonicalizeHighToleranceFamilyId = Build-CanonicalizeFamilyIdMapBySourceStems `
        -MapPath $SegmentsMapPath `
        -SourceStems $CanonicalizeHighToleranceSourceStems
}
$script:manualOrientationFixBySegmentFamily = Build-ManualOrientationFixMap -MapPath $SegmentsMapPath

foreach ($id in $segmentIds) {
    $geoPath = Join-Path $DataDir ("S{0:D3}.GEO" -f $id)
    $matPath = Join-Path $DataDir ("S{0:D3}M{1}.MAT" -f $id, $Lod)
    $outPath = Join-Path $OutDir ("S{0:D3}.SDR" -f $id)

    $geo = Load-Geo $geoPath
    $mat = Load-Mat $matPath
    Write-Sdr -Id $id -Geo $geo -Mat $mat -TargetPath $outPath
    $written++
    Write-Host ("SDR ok: {0}" -f $outPath)
}

Write-Host ("SDR gerados: {0}" -f $written)
