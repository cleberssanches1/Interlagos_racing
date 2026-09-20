param(
    [Parameter(Mandatory = $true)]
    [string]$SegmentsMapPath,
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$Pattern = "seg_*.obj",
    [string]$UvFaceUnwrapRulesPath = "",
    [switch]$SkipMaterialsWithoutImages = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($UvFaceUnwrapRulesPath)) {
    $UvFaceUnwrapRulesPath = Join-Path $PSScriptRoot "uv_face_unwrap_rules.json"
}

function Normalize-MaterialFamilyName([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name)) { return "" }
    $n = $Name.Trim()
    $n = [regex]::Replace($n, '_(8|16|32|64)(\.[^\\\/]+)?$', '', 'IgnoreCase')
    return $n.ToLowerInvariant()
}

function Get-SegmentId([string]$BaseName) {
    if ($BaseName -match '^(?i)seg_(\d+)$') { return [int]$Matches[1] }
    return $null
}

function Get-TgaIndex([string]$Dir) {
    if (-not (Test-Path -LiteralPath $Dir)) {
        throw "Pasta de textura obrigatoria ausente: $Dir"
    }
    $out = @{}
    foreach ($file in @(Get-ChildItem -LiteralPath $Dir -File -ErrorAction Stop |
            Where-Object { $_.Extension -ieq ".tga" } |
            Sort-Object Name)) {
        $key = $file.Name.ToLowerInvariant()
        if ($out.ContainsKey($key)) {
            throw "Nome TGA duplicado (case-insensitive) em ${Dir}: $($file.Name)"
        }
        $out[$key] = $file
    }
    return $out
}

function Read-MtlTextureMap([string]$MtlPath) {
    $out = @{}
    if (-not (Test-Path -LiteralPath $MtlPath)) { return $out }
    $current = ""
    foreach ($raw in [System.IO.File]::ReadLines($MtlPath)) {
        $line = $raw.Trim()
        if ($line.StartsWith("newmtl ", [System.StringComparison]::OrdinalIgnoreCase)) {
            $current = $line.Substring(7).Trim()
            continue
        }
        if ([string]::IsNullOrWhiteSpace($current)) { continue }
        if ($line -notmatch '^(?i)map_Kd\s+(.+)$') { continue }
        $fileName = [System.IO.Path]::GetFileName($Matches[1].Trim().Trim('"'))
        if ([string]::IsNullOrWhiteSpace($fileName)) { continue }
        $out[$current.ToLowerInvariant()] = $fileName
    }
    return $out
}

function Read-ObjGeneratedFaceUvRecords([string]$ObjPath) {
    if (-not (Test-Path -LiteralPath $ObjPath)) { throw "OBJ ausente: $ObjPath" }

    $mtlMap = Read-MtlTextureMap ([System.IO.Path]::ChangeExtension($ObjPath, ".mtl"))
    $uvs = New-Object System.Collections.Generic.List[object]
    foreach ($raw in [System.IO.File]::ReadLines($ObjPath)) {
        $line = $raw.Trim()
        if (-not $line.StartsWith("vt ", [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        $parts = @($line.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries))
        if ($parts.Count -lt 3) { continue }
        $uvs.Add([pscustomobject]([ordered]@{ u = [double]$parts[1]; v = [double]$parts[2] })) | Out-Null
    }

    $faces = New-Object System.Collections.Generic.List[object]
    $currentMaterial = ""
    $generatedFaceIndex = 0
    foreach ($raw in [System.IO.File]::ReadLines($ObjPath)) {
        $line = $raw.Trim()
        if ($line.StartsWith("usemtl ", [System.StringComparison]::OrdinalIgnoreCase)) {
            $currentMaterial = $line.Substring(7).Trim()
            continue
        }
        if (-not $line.StartsWith("f ", [System.StringComparison]::OrdinalIgnoreCase)) { continue }

        $parts = @($line.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries))
        $sourceCorners = New-Object System.Collections.Generic.List[object]
        for ($i = 1; $i -lt $parts.Count; $i++) {
            $indices = @($parts[$i].Split('/'))
            $textureIndex = if ($indices.Count -ge 2 -and -not [string]::IsNullOrWhiteSpace($indices[1])) { [int]$indices[1] } else { 0 }
            if ($textureIndex -gt 0) { $textureIndex-- }
            elseif ($textureIndex -lt 0) { $textureIndex = $uvs.Count + $textureIndex }
            else { $textureIndex = -1 }
            if ($textureIndex -lt 0 -or $textureIndex -ge $uvs.Count) {
                throw "Indice UV invalido '$($parts[$i])' em $ObjPath"
            }
            $sourceCorners.Add($uvs[$textureIndex]) | Out-Null
        }

        $generatedFaces = New-Object System.Collections.Generic.List[object]
        if ($sourceCorners.Count -gt 4) {
            for ($i = 1; $i -lt ($sourceCorners.Count - 1); $i++) {
                $generatedFaces.Add(@($sourceCorners[0], $sourceCorners[$i], $sourceCorners[$i + 1])) | Out-Null
            }
        }
        else {
            $generatedFaces.Add(@($sourceCorners.ToArray())) | Out-Null
        }

        $materialKey = $currentMaterial.ToLowerInvariant()
        $textureName = if ($mtlMap.ContainsKey($materialKey)) { [string]$mtlMap[$materialKey] } else { "" }
        foreach ($generated in $generatedFaces) {
            $faces.Add([pscustomobject]([ordered]@{
                index = [int]$generatedFaceIndex
                material = $currentMaterial
                textureName = $textureName
                textureStem = [System.IO.Path]::GetFileNameWithoutExtension($textureName).ToLowerInvariant()
                uv = @($generated | ForEach-Object { [pscustomobject]([ordered]@{ u = [double]$_.u; v = [double]$_.v }) })
            })) | Out-Null
            $generatedFaceIndex++
        }
    }
    return @($faces.ToArray())
}

function Get-TargetTextureName([string]$SourceStem, [int]$Lod) {
    $base = [regex]::Replace($SourceStem, '_(8|16|32|64)$', '', 'IgnoreCase')
    $base = [regex]::Replace($base, '(8|16|32|64)$', '', 'IgnoreCase')
    if ([string]::IsNullOrWhiteSpace($base)) { throw "Stem de textura invalido: $SourceStem" }
    return ("{0}_{1}.TGA" -f $base.ToUpperInvariant(), $Lod)
}

function New-Rle([int[]]$Values) {
    $runs = New-Object System.Collections.Generic.List[object]
    if ($null -eq $Values -or $Values.Count -eq 0) { return @() }
    $start = 0
    $current = [int]$Values[0]
    $count = 1
    for ($i = 1; $i -lt $Values.Count; $i++) {
        $value = [int]$Values[$i]
        if ($value -eq $current) { $count++; continue }
        $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $current }) | Out-Null
        $start = $i
        $current = $value
        $count = 1
    }
    $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $current }) | Out-Null
    return @($runs.ToArray())
}

if (-not (Test-Path -LiteralPath $SegmentsMapPath)) {
    throw "segments_map nao encontrado: $SegmentsMapPath"
}

$lodDirs = [ordered]@{
    lod_0 = (Join-Path $ResultDir "lod_0")
    lod_1 = (Join-Path $ResultDir "lod_1")
    lod_2 = (Join-Path $ResultDir "lod_2")
}
$textureDirs = [ordered]@{
    lod_0 = (Join-Path $lodDirs.lod_0 "ARQ_TGA")
    lod_1 = (Join-Path $lodDirs.lod_1 "ARQ_TGA")
    lod_2 = (Join-Path $lodDirs.lod_2 "ARQ_TGA")
}
foreach ($dir in $lodDirs.Values) {
    if (-not (Test-Path -LiteralPath $dir)) { throw "Agrupamento LOD obrigatorio ausente: $dir" }
}

$tgaIndex = @{
    lod_0 = Get-TgaIndex $textureDirs.lod_0
    lod_1 = Get-TgaIndex $textureDirs.lod_1
    lod_2 = Get-TgaIndex $textureDirs.lod_2
}

# First pass: collect only material/TGA pairs referenced by OBJ files from the
# three current design groups. No previous family catalog participates here.
$records = New-Object System.Collections.Generic.List[object]
$materialToTextures = @{}
foreach ($lodName in @("lod_0", "lod_1", "lod_2")) {
    $objFiles = @(Get-ChildItem -LiteralPath $lodDirs[$lodName] -File -Filter $Pattern -ErrorAction Stop | Sort-Object Name)
    if ($objFiles.Count -eq 0) { throw "Nenhum OBJ em $($lodDirs[$lodName])" }
    foreach ($obj in $objFiles) {
        $segmentId = Get-SegmentId $obj.BaseName
        if ($null -eq $segmentId) { continue }
        $mtlMap = Read-MtlTextureMap ([System.IO.Path]::ChangeExtension($obj.FullName, ".mtl"))
        $currentMaterial = ""
        foreach ($raw in [System.IO.File]::ReadLines($obj.FullName)) {
            $line = $raw.Trim()
            if ($line.StartsWith("usemtl ", [System.StringComparison]::OrdinalIgnoreCase)) {
                $currentMaterial = $line.Substring(7).Trim()
                continue
            }
            if (-not $line.StartsWith("f ", [System.StringComparison]::OrdinalIgnoreCase)) { continue }
            if ([string]::IsNullOrWhiteSpace($currentMaterial)) {
                throw "Face sem usemtl em $($obj.FullName)"
            }
            $materialKey = $currentMaterial.ToLowerInvariant()
            $textureName = if ($mtlMap.ContainsKey($materialKey)) { [string]$mtlMap[$materialKey] } else { "" }
            $cornerCount = @($line.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)).Count - 1
            $generatedFaceCount = if ($cornerCount -gt 4) { $cornerCount - 2 } else { 1 }
            $record = [pscustomobject]@{
                lod = $lodName
                segmentId = [int]$segmentId
                material = $currentMaterial
                normalizedMaterial = Normalize-MaterialFamilyName $currentMaterial
                textureName = $textureName
                generatedFaceCount = [int]$generatedFaceCount
            }
            $records.Add($record) | Out-Null
            if (-not [string]::IsNullOrWhiteSpace($textureName)) {
                $norm = [string]$record.normalizedMaterial
                if (-not $materialToTextures.ContainsKey($norm)) {
                    $materialToTextures[$norm] = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
                }
                [void]$materialToTextures[$norm].Add($textureName)
            }
        }
    }
}

# Preserve one family slot per generated face. When build_all requests skipping
# image-less materials, the empty texture is retained here and becomes familyId
# zero below; this keeps OBJ/GEO/MAT face indices aligned without adding a fake
# texture family.
$skippedMaterialKeys = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$skippedGeneratedFaceCount = 0
foreach ($record in $records) {
    if (-not [string]::IsNullOrWhiteSpace([string]$record.textureName)) { continue }
    if ($SkipMaterialsWithoutImages) {
        [void]$skippedMaterialKeys.Add(("{0}:seg_{1:D3}:{2}" -f $record.lod, $record.segmentId, $record.material))
        $skippedGeneratedFaceCount += [int]$record.generatedFaceCount
        continue
    }

    $fallbackMaterial = [string]$record.normalizedMaterial
    if (-not ($materialToTextures.ContainsKey($fallbackMaterial) -and
              $materialToTextures[$fallbackMaterial].Count -eq 1)) {
        $fallbackMaterial = switch -Regex ([string]$record.normalizedMaterial) {
            '^branco(?:\.\d+)?$' { "teto"; break }
            default { "" }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($fallbackMaterial) -and
        $materialToTextures.ContainsKey($fallbackMaterial) -and
        $materialToTextures[$fallbackMaterial].Count -eq 1) {
        $record.textureName = [string]@($materialToTextures[$fallbackMaterial])[0]
        continue
    }
    throw ("Material sem map_Kd resolvivel: lod={0} seg={1} material={2}" -f $record.lod, $record.segmentId, $record.material)
}

$familyMetaByStem = @{}
foreach ($record in $records) {
    if ([string]::IsNullOrWhiteSpace([string]$record.textureName)) { continue }
    $textureKey = ([string]$record.textureName).ToLowerInvariant()
    foreach ($lodName in @("lod_0", "lod_1", "lod_2")) {
        if (-not $tgaIndex[$lodName].ContainsKey($textureKey)) {
            throw ("Textura {0} usada por seg {1} ausente no agrupamento {2}: {3}" -f
                $record.textureName, $record.segmentId, $lodName, $textureDirs[$lodName])
        }
    }

    $stem = [System.IO.Path]::GetFileNameWithoutExtension([string]$record.textureName).ToLowerInvariant()
    if (-not $familyMetaByStem.ContainsKey($stem)) {
        $familyMetaByStem[$stem] = [pscustomobject]@{
            stem = $stem
            names = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
            aliases = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
            sourceName = [string]$record.textureName
        }
    }
    [void]$familyMetaByStem[$stem].names.Add([string]$record.normalizedMaterial)
    [void]$familyMetaByStem[$stem].aliases.Add([string]$record.normalizedMaterial)
}

$familyIdByStem = @{}
$families = New-Object System.Collections.Generic.List[object]
$nextId = 1
foreach ($stem in @($familyMetaByStem.Keys | Sort-Object)) {
    $meta = $familyMetaByStem[$stem]
    $familyIdByStem[$stem] = $nextId
    $familyName = [string](@($meta.names | Sort-Object)[0])
    $file0 = $tgaIndex.lod_0[([string]$meta.sourceName).ToLowerInvariant()]
    $file1 = $tgaIndex.lod_1[([string]$meta.sourceName).ToLowerInvariant()]
    $file2 = $tgaIndex.lod_2[([string]$meta.sourceName).ToLowerInvariant()]
    $target64 = Get-TargetTextureName $stem 64
    $target32 = Get-TargetTextureName $stem 32
    $families.Add([pscustomobject]([ordered]@{
        id = $nextId
        name = $familyName
        sourceStem = $stem
        aliases = @($meta.aliases | Sort-Object)
        sourceFiles = [pscustomobject]([ordered]@{
            lod_0 = $file0.Name
            lod_1 = $file1.Name
            lod_2 = $file2.Name
        })
        bankFiles = [pscustomobject]([ordered]@{
            lod_0 = $target64
            lod_1 = $target64
            lod_2 = $target32
        })
        variants = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
        imageFiles = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
    })) | Out-Null
    $nextId++
}

# VDP1 applies one complete rectangular texture to each distorted sprite.  When
# Blender spreads a single UV island over multiple faces, the runtime therefore
# needs a pre-unwrapped texture for each participating face.  Keep this opt-in:
# applying it to every shared OBJ edge would turn ordinary tiled track surfaces
# into thousands of families and exhaust Saturn texture memory.
$uvVariantFamilyIdBySegmentFace = @{}
$uvRuleAudit = New-Object System.Collections.Generic.List[object]
$uvRules = @()
if (Test-Path -LiteralPath $UvFaceUnwrapRulesPath) {
    $ruleJson = Get-Content -LiteralPath $UvFaceUnwrapRulesPath -Raw | ConvertFrom-Json
    if ($null -ne $ruleJson -and $ruleJson.PSObject.Properties.Name -contains "rules") {
        $uvRules = @($ruleJson.rules)
    }
}

$uvFaceCache = @{}
foreach ($rule in @($uvRules | Sort-Object { [int]$_.segmentId }, { [string]$_.textureStem })) {
    $segmentId = [int]$rule.segmentId
    $baseStem = ([string]$rule.textureStem).Trim().ToLowerInvariant()
    if ($segmentId -lt 0 -or [string]::IsNullOrWhiteSpace($baseStem)) {
        throw "Regra UV invalida em $UvFaceUnwrapRulesPath"
    }
    if (-not $familyIdByStem.ContainsKey($baseStem)) {
        throw "Regra UV referencia textura sem family base: seg=$segmentId stem=$baseStem"
    }
    $baseFamilyId = [int]$familyIdByStem[$baseStem]
    $baseFamily = @($families | Where-Object { [int]$_.id -eq $baseFamilyId })[0]

    foreach ($faceIndexValue in @($rule.faceIndices | Sort-Object -Unique)) {
        $faceIndex = [int]$faceIndexValue
        $variantKey = "{0}:{1}" -f $segmentId, $faceIndex
        if ($uvVariantFamilyIdBySegmentFace.ContainsKey($variantKey)) {
            throw "Regra UV duplicada para segmento $segmentId face $faceIndex"
        }

        $uvByLod = [ordered]@{}
        foreach ($lodName in @("lod_0", "lod_1", "lod_2")) {
            $objPath = Join-Path $lodDirs[$lodName] ("seg_{0:D3}.obj" -f $segmentId)
            $cacheKey = $objPath.ToLowerInvariant()
            if (-not $uvFaceCache.ContainsKey($cacheKey)) {
                $uvFaceCache[$cacheKey] = @(Read-ObjGeneratedFaceUvRecords $objPath)
            }
            $faceRecord = @($uvFaceCache[$cacheKey] | Where-Object { [int]$_.index -eq $faceIndex }) | Select-Object -First 1
            if ($null -eq $faceRecord) {
                throw "Regra UV referencia face ausente: lod=$lodName seg=$segmentId face=$faceIndex"
            }
            if ([string]$faceRecord.textureStem -ne $baseStem) {
                throw ("Regra UV encontrou textura inesperada: lod={0} seg={1} face={2} esperada={3} atual={4}" -f
                    $lodName, $segmentId, $faceIndex, $baseStem, $faceRecord.textureStem)
            }
            if (@($faceRecord.uv).Count -ne 4) {
                throw "Regra UV suporta apenas quad: lod=$lodName seg=$segmentId face=$faceIndex"
            }
            $uvByLod[$lodName] = @($faceRecord.uv)
        }

        $assetStem = "U{0:D3}F{1:D3}" -f $segmentId, $faceIndex
        $target64 = "${assetStem}_64.TGA"
        $target32 = "${assetStem}_32.TGA"
        $derivedId = $nextId
        $families.Add([pscustomobject]([ordered]@{
            id = $derivedId
            name = ("{0}__uv_s{1:D3}_f{2:D3}" -f [string]$baseFamily.name, $segmentId, $faceIndex)
            sourceStem = ("{0}_uv_s{1:D3}_f{2:D3}" -f $baseStem, $segmentId, $faceIndex)
            baseSourceStem = $baseStem
            baseFamilyId = $baseFamilyId
            aliases = @()
            sourceFiles = [pscustomobject]([ordered]@{
                lod_0 = [string]$baseFamily.sourceFiles.lod_0
                lod_1 = [string]$baseFamily.sourceFiles.lod_1
                lod_2 = [string]$baseFamily.sourceFiles.lod_2
            })
            bankFiles = [pscustomobject]([ordered]@{
                lod_0 = $target64
                lod_1 = $target64
                lod_2 = $target32
            })
            variants = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
            imageFiles = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
            uvUnwrap = [pscustomobject]([ordered]@{
                segmentId = $segmentId
                faceIndex = $faceIndex
                sourceStem = $baseStem
                uvByLod = [pscustomobject]$uvByLod
            })
        })) | Out-Null
        $uvVariantFamilyIdBySegmentFace[$variantKey] = $derivedId
        $uvRuleAudit.Add([pscustomobject]@{ segmentId = $segmentId; faceIndex = $faceIndex; baseStem = $baseStem; familyId = $derivedId }) | Out-Null
        $nextId++
    }
}

$oldJson = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
$oldSegmentById = @{}
foreach ($seg in @($oldJson.segments)) { $oldSegmentById[[int]$seg.id] = $seg }

$highRecordsBySegment = @{}
foreach ($record in @($records | Where-Object { $_.lod -eq "lod_0" })) {
    if (-not $highRecordsBySegment.ContainsKey([int]$record.segmentId)) {
        $highRecordsBySegment[[int]$record.segmentId] = New-Object System.Collections.Generic.List[object]
    }
    $highRecordsBySegment[[int]$record.segmentId].Add($record) | Out-Null
}

$segments = New-Object System.Collections.Generic.List[object]
foreach ($segmentId in @($highRecordsBySegment.Keys | Sort-Object)) {
    $faceFamilies = New-Object System.Collections.Generic.List[int]
    $generatedFaceIndex = 0
    foreach ($record in $highRecordsBySegment[$segmentId]) {
        $baseFamilyId = 0
        if (-not [string]::IsNullOrWhiteSpace([string]$record.textureName)) {
            $stem = [System.IO.Path]::GetFileNameWithoutExtension([string]$record.textureName).ToLowerInvariant()
            $baseFamilyId = [int]$familyIdByStem[$stem]
        }
        for ($i = 0; $i -lt [int]$record.generatedFaceCount; $i++) {
            $variantKey = "{0}:{1}" -f $segmentId, $generatedFaceIndex
            $familyId = if ($uvVariantFamilyIdBySegmentFace.ContainsKey($variantKey)) {
                [int]$uvVariantFamilyIdBySegmentFace[$variantKey]
            }
            else {
                $baseFamilyId
            }
            $faceFamilies.Add($familyId) | Out-Null
            $generatedFaceIndex++
        }
    }
    $faceArray = [int[]]$faceFamilies.ToArray()
    $old = if ($oldSegmentById.ContainsKey([int]$segmentId)) { $oldSegmentById[[int]$segmentId] } else { $null }
    $faces = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $faceArray.Count; $i++) {
        $faces.Add([pscustomobject]@{ index = $i; familyId = [int]$faceArray[$i] }) | Out-Null
    }
    $segments.Add([pscustomobject]([ordered]@{
        id = [int]$segmentId
        nya = if ($null -ne $old -and $old.nya) { [string]$old.nya } else { "SEG_{0:D3}.NYA" -f $segmentId }
        faceCount = $faceArray.Count
        faceTextureFamily = $faceArray
        faceTextureFamilyRle = @(New-Rle $faceArray)
        faces = @($faces.ToArray())
        # Converter mesh-level ids belong to the discarded temporary catalog.
        # Face bindings below are the authoritative runtime mapping.
        meshes = @()
    })) | Out-Null
}

$outJson = [pscustomobject]([ordered]@{
    version = 3
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    exporter = $oldJson.exporter
    familyBuild = [pscustomobject]@{
        mode = "fresh"
        sourceGroups = @("lod_0", "lod_1", "lod_2")
        textureBanks = @("TBKLOD0.BIN", "TBKLOD1.BIN", "TBKLOD2.BIN")
        textureRoots = @($textureDirs.Values)
        uvFaceUnwrapRulesPath = if (Test-Path -LiteralPath $UvFaceUnwrapRulesPath) { [System.IO.Path]::GetFullPath($UvFaceUnwrapRulesPath) } else { "" }
        uvFaceUnwrapCount = $uvRuleAudit.Count
    }
    textureFamilies = @($families.ToArray())
    segments = @($segments.ToArray())
})
$outJson | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8

Write-Host ("Fresh segments_map: segments={0} families={1} ids=1..{1}" -f $segments.Count, $families.Count)
Write-Host ("Texture roots: {0}" -f ($textureDirs.Values -join "; "))
if ($SkipMaterialsWithoutImages -and $skippedMaterialKeys.Count -gt 0) {
    Write-Host ("Materiais sem imagem ignorados: ocorrencias={0} faces={1} (familyId=0)" -f
        $skippedMaterialKeys.Count, $skippedGeneratedFaceCount)
}
if ($uvRuleAudit.Count -gt 0) {
    Write-Host ("UV face unwrap: regras={0} variantes={1}" -f $uvRules.Count, $uvRuleAudit.Count)
}
