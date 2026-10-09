param(
    [string]$ObjSourceDir = "C:\Models\png\sectors\source",
    [string]$LodRootDir = "C:\Models\png\sectors\result",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$Pattern = "SEG_*.NYA",
    [switch]$WriteLongCompatNames
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-U16([System.IO.BinaryWriter]$bw, [uint16]$v) { $bw.Write($v) }
function Write-I16([System.IO.BinaryWriter]$bw, [int16]$v) { $bw.Write($v) }
function Write-U32([System.IO.BinaryWriter]$bw, [uint32]$v) { $bw.Write($v) }
function Write-I32([System.IO.BinaryWriter]$bw, [int32]$v) { $bw.Write($v) }

function To-Fxp32([double]$v) {
    return [int32][Math]::Round($v * 65536.0)
}

function To-I16Uv([double]$v) {
    $scaled = [int][Math]::Round($v * 32767.0)
    if ($scaled -lt -32768) { $scaled = -32768 }
    if ($scaled -gt 32767) { $scaled = 32767 }
    return [int16]$scaled
}

function Get-SegmentIdFromName([string]$name) {
    if ($name -match '^(?i)SEG_(\d{3})(?:_L(8|16|32|64))?\.(NYA|OBJ|MAP)$') { return [int]$Matches[1] }
    if ($name -match '^(?i)seg_(\d{3})(?:_l(8|16|32|64))?\.(nya|obj|map)$') { return [int]$Matches[1] }
    if ($name -match '^(?i)S(\d{3})L(8|16|32|64)\.(NYA|OBJ|MAP)$') { return [int]$Matches[1] }
    if ($name -match '^(?i)pista_seg\.(\d{3})\.(obj|map|nya)$') { return [int]$Matches[1] }
    return $null
}

function Get-MapFilePath([string]$dir, [int]$id) {
    $cands = @(
        (Join-Path $dir ("SEG_{0:D3}.map" -f $id)),
        (Join-Path $dir ("seg_{0:D3}.map" -f $id)),
        (Join-Path $dir ("SEG_{0:D3}.MAP" -f $id)),
        (Join-Path $dir ("seg_{0:D3}.MAP" -f $id))
    )
    foreach ($p in $cands) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

function Get-LodObjPath([string]$dir, [int]$id) {
    $cands = @(
        (Join-Path $dir ("seg_{0:D3}.obj" -f $id)),
        (Join-Path $dir ("SEG_{0:D3}.OBJ" -f $id)),
        (Join-Path $dir ("SEG_{0:D3}.obj" -f $id)),
        (Join-Path $dir ("seg_{0:D3}.OBJ" -f $id)),
        (Join-Path $dir ("pista_seg.{0:D3}.obj" -f $id)),
        (Join-Path $dir ("PISTA_SEG.{0:D3}.OBJ" -f $id))
    )
    foreach ($p in $cands) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

function Normalize-TextureToken([string]$token) {
    if ([string]::IsNullOrWhiteSpace($token)) { return "" }
    $t = $token.Trim()
    $t = [System.IO.Path]::GetFileName($t)
    return $t
}

function FamilyFromToken([string]$token) {
    $t = Normalize-TextureToken $token
    if ([string]::IsNullOrWhiteSpace($t)) { return "" }

    # Ex: asfalto_64.tga / area_escape.006_16.001
    $m = [regex]::Match($t, '^(?<base>.+)_(?<lod>8|16|32|64)(?<rest>\.[^\\\/]+)$', 'IgnoreCase')
    if ($m.Success) { return $m.Groups['base'].Value.ToLowerInvariant() }

    # Fallback: remove extension only
    return ([System.IO.Path]::GetFileNameWithoutExtension($t)).ToLowerInvariant()
}

function Parse-ObjGeometry([string]$objPath) {
    $verts = New-Object System.Collections.Generic.List[object]
    $uvs = New-Object System.Collections.Generic.List[object]
    $faces = New-Object System.Collections.Generic.List[object]

    $lines = Get-Content -LiteralPath $objPath
    foreach ($line in $lines) {
        $t = $line.Trim()
        if ($t.Length -eq 0 -or $t.StartsWith("#")) { continue }

        if ($t.StartsWith("v ")) {
            $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($p.Count -ge 4) {
                $verts.Add([pscustomobject]@{
                    x = [double]$p[1]
                    y = [double]$p[2]
                    z = [double]$p[3]
                }) | Out-Null
            }
            continue
        }

        if ($t.StartsWith("vt ")) {
            $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($p.Count -ge 3) {
                $uvs.Add([pscustomobject]@{
                    u = [double]$p[1]
                    v = [double]$p[2]
                }) | Out-Null
            }
            continue
        }

        if ($t.StartsWith("f ")) {
            $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($p.Count -lt 4) { continue }
            $corners = @()
            for ($i = 1; $i -lt $p.Count; $i++) {
                $tok = $p[$i]
                $parts = $tok.Split("/")
                $vi = if ($parts.Count -ge 1 -and $parts[0].Length -gt 0) { [int]$parts[0] } else { 0 }
                $ti = if ($parts.Count -ge 2 -and $parts[1].Length -gt 0) { [int]$parts[1] } else { 0 }
                if ($vi -le 0) { throw ("Face com vertice invalido em {0}: '{1}'" -f $objPath, $tok) }
                $corners += ,([pscustomobject]@{ vi = $vi - 1; ti = [Math]::Max(0, $ti - 1) })
            }
            if ($corners.Count -gt 4) {
                for ($i = 1; $i -lt ($corners.Count - 1); $i++) {
                    $faces.Add(@($corners[0], $corners[$i], $corners[$i + 1])) | Out-Null
                }
            } else {
                $faces.Add($corners) | Out-Null
            }
        }
    }

    if ($verts.Count -eq 0 -or $faces.Count -eq 0) {
        throw "OBJ sem dados suficientes: $objPath"
    }
    return [pscustomobject]@{
        verts = $verts
        uvs = $uvs
        faces = $faces
    }
}

function Parse-ObjFaceFamilies([string]$objPath, [hashtable]$familyIdByName, [ref]$nextFamilyIdRef) {
    $faceFamilies = New-Object System.Collections.Generic.List[int]
    $currentFamilyName = ""

    $lines = Get-Content -LiteralPath $objPath
    foreach ($line in $lines) {
        $t = $line.Trim()
        if ($t.Length -eq 0 -or $t.StartsWith("#")) { continue }

        if ($t.StartsWith("usemtl ", [System.StringComparison]::OrdinalIgnoreCase)) {
            $mtlName = $t.Substring(7).Trim()
            $currentFamilyName = FamilyFromToken $mtlName
            continue
        }

        if ($t.StartsWith("f ")) {
            $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($p.Count -lt 4) { continue }

            $fid = 0
            if (-not [string]::IsNullOrWhiteSpace($currentFamilyName)) {
                if (-not $familyIdByName.ContainsKey($currentFamilyName)) {
                    $familyIdByName[$currentFamilyName] = [int]$nextFamilyIdRef.Value
                    $nextFamilyIdRef.Value = [int]$nextFamilyIdRef.Value + 1
                }
                $fid = [int]$familyIdByName[$currentFamilyName]
            }

            $cornerCount = $p.Count - 1
            if ($cornerCount -gt 4) {
                # Fan triangulation: n-gon generates (n-2) faces
                for ($i = 1; $i -lt ($cornerCount - 1); $i++) {
                    $faceFamilies.Add($fid) | Out-Null
                }
            } else {
                $faceFamilies.Add($fid) | Out-Null
            }
        }
    }

    return @($faceFamilies.ToArray())
}

function Write-Geo([int]$id, [object]$geoData, [string]$outDir) {
    $geoPath = Join-Path $outDir ("S{0:D3}.GEO" -f $id)
    $geoLongPath = Join-Path $outDir ("SEG_{0:D3}.GEO" -f $id)
    $verts = $geoData.verts
    $uvs = $geoData.uvs
    $faces = $geoData.faces

    $fs = [System.IO.File]::Open($geoPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        $geoPayloadBytes = [uint32](8 + ($verts.Count * 12) + ($faces.Count * 28))
        Write-U32 $bw 0x314F4547
        Write-U16 $bw 1
        Write-U16 $bw 0
        Write-U32 $bw ([uint32]$id)
        Write-U32 $bw $geoPayloadBytes
        Write-U32 $bw ([uint32]$verts.Count)
        Write-U32 $bw ([uint32]$faces.Count)

        foreach ($v in $verts) {
            Write-I32 $bw (To-Fxp32 $v.x)
            Write-I32 $bw (To-Fxp32 $v.y)
            Write-I32 $bw (To-Fxp32 $v.z)
        }

        foreach ($f in $faces) {
            $kind = [byte]$f.Count
            if ($kind -ne 3 -and $kind -ne 4) { $kind = 3 }
            $vi = @(0,0,0,0)
            $uu = @(0,0,0,0)
            $vv = @(0,0,0,0)
            for ($i = 0; $i -lt $f.Count; $i++) {
                $c = $f[$i]
                $vi[$i] = [uint16]$c.vi
                if ($c.ti -ge 0 -and $c.ti -lt $uvs.Count) {
                    $u = [double]$uvs[$c.ti].u
                    $v = [double]$uvs[$c.ti].v
                    $uu[$i] = To-I16Uv $u
                    $vv[$i] = To-I16Uv $v
                }
            }
            for ($i = 0; $i -lt 4; $i++) { Write-U16 $bw ([uint16]$vi[$i]) }
            for ($i = 0; $i -lt 4; $i++) { Write-I16 $bw ([int16]$uu[$i]) }
            for ($i = 0; $i -lt 4; $i++) { Write-I16 $bw ([int16]$vv[$i]) }
            $bw.Write([byte]$kind)
            $bw.Write([byte]0)
            Write-U16 $bw 0
        }
        $bw.Flush()
    }
    finally {
        $fs.Close()
    }
    if ($WriteLongCompatNames) {
        Copy-Item -LiteralPath $geoPath -Destination $geoLongPath -Force
    }
    return [pscustomobject]@{ path = $geoPath; faceCount = $faces.Count; vertCount = $verts.Count }
}

function Write-Mat([int]$id, [int]$lod, [int[]]$faceFamilyIds, [string]$outDir) {
    $matPath = Join-Path $outDir ("S{0:D3}M{1}.MAT" -f $id, $lod)
    $matLongPath = Join-Path $outDir ("SEG_{0:D3}_M{1}.MAT" -f $id, $lod)
    $faceCount = $faceFamilyIds.Count
    $fs = [System.IO.File]::Open($matPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        $matPayloadBytes = [uint32](4 + ($faceCount * 4))
        Write-U32 $bw 0x3154414D
        Write-U16 $bw 1
        Write-U16 $bw 0
        Write-U32 $bw ([uint32]$id)
        Write-U32 $bw $matPayloadBytes
        Write-U32 $bw ([uint32]$faceCount)
        foreach ($fid in $faceFamilyIds) {
            Write-U32 $bw ([uint32][Math]::Max(0, $fid))
        }
        $bw.Flush()
    }
    finally {
        $fs.Close()
    }
    if ($WriteLongCompatNames) {
        Copy-Item -LiteralPath $matPath -Destination $matLongPath -Force
    }
    return $matPath
}

if (-not (Test-Path -LiteralPath $ObjSourceDir)) { throw "ObjSourceDir nao encontrado: $ObjSourceDir" }
if (-not (Test-Path -LiteralPath $LodRootDir)) { throw "LodRootDir nao encontrado: $LodRootDir" }
New-Item -Path $OutDir -ItemType Directory -Force | Out-Null

function Resolve-LodDir([string]$Root, [string[]]$Candidates) {
    foreach ($c in $Candidates) {
        $p = Join-Path $Root $c
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

$lodDirs = [ordered]@{}
$dir64 = Resolve-LodDir $LodRootDir @("lod_0", "lod_1", "obj_64")
$dir32 = Resolve-LodDir $LodRootDir @("lod_2", "obj_32")
$dir16 = Resolve-LodDir $LodRootDir @("obj_16")
$dir8  = Resolve-LodDir $LodRootDir @("obj_8")
if ($null -eq $dir64) { throw "Pasta LOD 64/lod_0 ausente em $LodRootDir" }
if ($null -eq $dir32) { throw "Pasta LOD 32/lod_2 ausente em $LodRootDir" }
$lodDirs["64"] = $dir64
$lodDirs["32"] = $dir32
if ($null -ne $dir16) { $lodDirs["16"] = $dir16 } else { Write-Host "Aviso: obj_16 ausente (ok no layout 3-LOD)." }
if ($null -ne $dir8)  { $lodDirs["8"]  = $dir8  } else { Write-Host "Aviso: obj_8 ausente (ok no layout 3-LOD)." }

# Segment ids discovered from dense design folder (lod_0 / 64).
$ids = New-Object System.Collections.Generic.List[int]
Get-ChildItem -LiteralPath $lodDirs["64"] -File | Sort-Object Name | ForEach-Object {
    $id = Get-SegmentIdFromName $_.Name
    if ($null -ne $id) { $ids.Add($id) | Out-Null }
}
$ids = @($ids | Sort-Object -Unique)
if ($ids.Count -eq 0) { throw "Nenhum segmento encontrado em $($lodDirs["64"]). Esperado SEG_###.(NYA|OBJ|MAP), seg_###.* ou S###L##.*" }

# Global family registry across all LOD maps.
$familyIdByName = @{}
$nextFamilyId = 1
$faceFamilyBySegLod = @{}

foreach ($id in $ids) {
    foreach ($lod in @(8,16,32,64)) {
        $mapPath = Get-MapFilePath -dir $lodDirs["$lod"] -id $id
        if ($mapPath) {
            $faceIds = New-Object System.Collections.Generic.List[int]
            Get-Content -LiteralPath $mapPath | ForEach-Object {
                $fam = FamilyFromToken $_
                if ([string]::IsNullOrWhiteSpace($fam)) { return }
                if (-not $familyIdByName.ContainsKey($fam)) {
                    $familyIdByName[$fam] = $nextFamilyId
                    $nextFamilyId++
                }
                $faceIds.Add([int]$familyIdByName[$fam]) | Out-Null
            }
            $faceFamilyBySegLod["$id|$lod"] = @($faceIds.ToArray())
        } else {
            $lodObjPath = Get-LodObjPath -dir $lodDirs["$lod"] -id $id
            if (-not $lodObjPath) {
                throw ("OBJ LOD nao encontrado para SEG_{0:D3} LOD {1} em {2}" -f $id, $lod, $lodDirs["$lod"])
            }
            $nextRef = [ref]$nextFamilyId
            $faceFamilyBySegLod["$id|$lod"] = @(Parse-ObjFaceFamilies -objPath $lodObjPath -familyIdByName $familyIdByName -nextFamilyIdRef $nextRef)
            $nextFamilyId = [int]$nextRef.Value
        }
    }
}

$okGeo = 0
$okMat = 0
$warnings = New-Object System.Collections.Generic.List[string]

foreach ($id in $ids) {
    $objPath = Get-LodObjPath -dir $ObjSourceDir -id $id
    if (-not $objPath) {
        # Fallback: geometry from LOD64 folder
        $objPath = Get-LodObjPath -dir $lodDirs["64"] -id $id
    }
    if (-not (Test-Path -LiteralPath $objPath)) {
        $warnings.Add(("OBJ ausente para SEG_{0:D3} em ObjSourceDir e obj_64" -f $id)) | Out-Null
        continue
    }

    $geoData = Parse-ObjGeometry -objPath $objPath
    $geoOut = Write-Geo -id $id -geoData $geoData -outDir $OutDir
    $okGeo++

    foreach ($lod in @(8,16,32,64)) {
        $faceFamily = @($faceFamilyBySegLod["$id|$lod"])
        if ($faceFamily.Count -ne $geoOut.faceCount) {
            $warnings.Add(("Face count mismatch SEG_{0:D3} LOD {1}: GEO={2} MAP={3}" -f $id, $lod, $geoOut.faceCount, $faceFamily.Count)) | Out-Null
            if ($faceFamily.Count -gt $geoOut.faceCount) {
                $faceFamily = @($faceFamily[0..($geoOut.faceCount - 1)])
            } else {
                while ($faceFamily.Count -lt $geoOut.faceCount) { $faceFamily += 0 }
            }
        }
        [void](Write-Mat -id $id -lod $lod -faceFamilyIds $faceFamily -outDir $OutDir)
        $okMat++
    }
}

# Manifest
$manifest = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    objSourceDir = $ObjSourceDir
    lodRootDir = $LodRootDir
    outDir = $OutDir
    segmentCount = $ids.Count
    familyCount = $familyIdByName.Count
    geoCount = $okGeo
    matCount = $okMat
    lods = @(8,16,32,64)
}
$manifestPath = Join-Path $OutDir "components_lod_manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ("OK GEO:{0} MAT:{1} SEG:{2} FAM:{3}" -f $okGeo, $okMat, $ids.Count, $familyIdByName.Count)
Write-Host ("Manifest: {0}" -f $manifestPath)
Write-Host ("Nome de saida: {0}" -f ($(if ($WriteLongCompatNames) { "curto + compat longo" } else { "curto (8.3)" })))
if ($warnings.Count -gt 0) {
    Write-Host ("AVISOS: {0}" -f $warnings.Count)
    $warnings | ForEach-Object { Write-Host (" - " + $_) }
}
