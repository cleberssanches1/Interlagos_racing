param(
    [Parameter(Mandatory = $true)]
    [string]$SegmentsMapPath,
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$Pattern = "seg_*.obj"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

# Resolve the known helper material with no map_Kd through its canonical family.
foreach ($record in $records) {
    if (-not [string]::IsNullOrWhiteSpace([string]$record.textureName)) { continue }
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
    $hash0 = (Get-FileHash -LiteralPath $file0.FullName -Algorithm SHA256).Hash
    $hash1 = (Get-FileHash -LiteralPath $file1.FullName -Algorithm SHA256).Hash
    if ($hash0 -ne $hash1) {
        throw "lod_0 e lod_1 possuem conteudo divergente para $($meta.sourceName); um unico TBK64 nao pode representar ambos."
    }
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
        variants = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
        imageFiles = [pscustomobject]([ordered]@{ "32" = $target32; "64" = $target64 })
    })) | Out-Null
    $nextId++
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
    foreach ($record in $highRecordsBySegment[$segmentId]) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension([string]$record.textureName).ToLowerInvariant()
        $familyId = [int]$familyIdByStem[$stem]
        for ($i = 0; $i -lt [int]$record.generatedFaceCount; $i++) { $faceFamilies.Add($familyId) | Out-Null }
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
    version = 2
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    exporter = $oldJson.exporter
    familyBuild = [pscustomobject]@{
        mode = "fresh"
        sourceGroups = @("lod_0", "lod_1", "lod_2")
        textureRoots = @($textureDirs.Values)
    }
    textureFamilies = @($families.ToArray())
    segments = @($segments.ToArray())
})
$outJson | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8

Write-Host ("Fresh segments_map: segments={0} families={1} ids=1..{1}" -f $segments.Count, $families.Count)
Write-Host ("Texture roots: {0}" -f ($textureDirs.Values -join "; "))
