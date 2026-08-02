param(
    [string]$ResultDir,
    [string]$OutJsonPath,
    [string]$SourceJsonPath = "",
    [string]$Shading,
    [int]$TexWidth,
    [int]$TexHeight,
    [int]$TexPadWidth
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-Rle([int[]]$Values) {
    $out = New-Object System.Collections.Generic.List[object]
    if (-not $Values -or $Values.Count -eq 0) { return @() }

    $current = [int]$Values[0]
    $count = 1
    for ($i = 1; $i -lt $Values.Count; $i++) {
        $v = [int]$Values[$i]
        if ($v -eq $current) {
            $count++
            continue
        }
        $out.Add([pscustomobject]@{ familyId = $current; count = $count }) | Out-Null
        $current = $v
        $count = 1
    }
    $out.Add([pscustomobject]@{ familyId = $current; count = $count }) | Out-Null
    return @($out.ToArray())
}

function Read-MatBindings([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20) {
        throw "MAT invalido (muito pequeno): $Path"
    }

    $magic = [System.BitConverter]::ToUInt32($bytes, 0)
    $version = [System.BitConverter]::ToUInt16($bytes, 4)
    $segmentId = [System.BitConverter]::ToUInt32($bytes, 8)
    $faceCount = [System.BitConverter]::ToUInt32($bytes, 16)
    if ($magic -ne 0x3154414D) {
        throw "MAT invalido (magic inesperado) em $Path"
    }
    if ($version -ne 1) {
        throw "MAT invalido (version inesperada) em $Path"
    }

    $available = [int][Math]::Floor(($bytes.Length - 20) / 4)
    $readCount = [int][Math]::Min([int]$faceCount, $available)
    $families = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -lt $readCount; $i++) {
        $families.Add([int][System.BitConverter]::ToUInt32($bytes, 20 + ($i * 4))) | Out-Null
    }
    while ($families.Count -lt [int]$faceCount) {
        $families.Add(0) | Out-Null
    }

    return [pscustomobject]@{
        segmentId = [int]$segmentId
        faceCount = [int]$faceCount
        families = @($families.ToArray())
    }
}

function Copy-IfPresent([System.Collections.Specialized.OrderedDictionary]$Target, $Source, [string]$Name) {
    if ($null -ne $Source -and $Source.PSObject.Properties.Name -contains $Name) {
        $Target[$Name] = $Source.$Name
    }
}

$sourceJson = if (-not [string]::IsNullOrWhiteSpace($SourceJsonPath)) {
    $SourceJsonPath
} else {
    Join-Path $ResultDir "segments_map.json"
}
if (-not (Test-Path -LiteralPath $sourceJson)) {
    throw "Arquivo segments_map.json nao encontrado em $ResultDir"
}

$outDir = Split-Path -Parent $OutJsonPath
if ([string]::IsNullOrWhiteSpace($outDir)) {
    $outDir = "."
}

$json = Get-Content -LiteralPath $sourceJson -Raw | ConvertFrom-Json
if (-not $json) {
    throw "Nao foi possivel carregar JSON base de $sourceJson"
}

$segmentById = @{}
foreach ($seg in @($json.segments)) {
    if ($null -eq $seg) { continue }
    $segmentById[[int]$seg.id] = $seg
}

function Find-PreferredMatPath {
    param(
        [string]$BaseDir,
        [int]$SegmentId
    )
    # Prefer dense design mesh (lod_0) MATs: 64 then 32.
    # Legacy M16/M8 often still sit on disk from old builds (16 faces + stale family ids).
    foreach ($lod in @(64, 32, 16, 8)) {
        $p = Join-Path $BaseDir ("S{0:D3}M{1}.MAT" -f $SegmentId, $lod)
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

$matById = @{}
$matSourceById = @{}
$matFilesUsed = New-Object System.Collections.Generic.List[string]

# Prefer MAT from dense GEO pipeline; do NOT only scan M8 (stale after 3-LOD redesign).
$candidateIds = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($id in $segmentById.Keys) { [void]$candidateIds.Add([int]$id) }
foreach ($matFile in @(Get-ChildItem -LiteralPath $outDir -File -Filter "S???M*.MAT" -ErrorAction SilentlyContinue)) {
    if ($matFile.BaseName -match '^S(\d{3})M(8|16|32|64)$') {
        [void]$candidateIds.Add([int]$Matches[1])
    }
}

foreach ($id in (@($candidateIds) | Sort-Object)) {
    $matPath = Find-PreferredMatPath -BaseDir $outDir -SegmentId $id
    if ([string]::IsNullOrWhiteSpace($matPath)) { continue }
    $mat = Read-MatBindings -Path $matPath
    $matById[[int]$mat.segmentId] = $mat
    $matSourceById[[int]$mat.segmentId] = [System.IO.Path]::GetFileName($matPath)
    $matFilesUsed.Add($matPath) | Out-Null
}

$allIds = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($id in $segmentById.Keys) { [void]$allIds.Add([int]$id) }
foreach ($id in $matById.Keys) { [void]$allIds.Add([int]$id) }

$usedFamilyIds = New-Object 'System.Collections.Generic.HashSet[int]'
$rebuiltSegments = New-Object System.Collections.Generic.List[object]
foreach ($id in (@($allIds) | Sort-Object)) {
    $existing = $null
    if ($segmentById.ContainsKey($id)) { $existing = $segmentById[$id] }

    if ($matById.ContainsKey($id)) {
        $mat = $matById[$id]
        $faces = New-Object System.Collections.Generic.List[object]
        for ($i = 0; $i -lt $mat.families.Count; $i++) {
            $fid = [int]$mat.families[$i]
            if ($fid -gt 0) { [void]$usedFamilyIds.Add($fid) }
            $faces.Add([pscustomobject]@{
                index = $i
                familyId = $fid
            }) | Out-Null
        }

        $segNode = [ordered]@{}
        $segNode["id"] = $id
        if ($null -ne $existing -and $existing.PSObject.Properties.Name -contains "name") {
            $segNode["name"] = [string]$existing.name
        }
        $segNode["nya"] = if ($null -ne $existing -and $existing.PSObject.Properties.Name -contains "nya" -and -not [string]::IsNullOrWhiteSpace([string]$existing.nya)) {
            [string]$existing.nya
        } else {
            "SEG_{0:D3}.NYA" -f $id
        }
        $segNode["faceCount"] = $mat.faceCount
        $segNode["faceTextureFamily"] = @($mat.families)
        $segNode["faceTextureFamilyRle"] = @(New-Rle $mat.families)
        $segNode["faces"] = @($faces.ToArray())
        if ($null -ne $existing -and $existing.PSObject.Properties.Name -contains "meshes") {
            $segNode["meshes"] = @($existing.meshes)
        } else {
            $segNode["meshes"] = @()
        }

        Copy-IfPresent -Target $segNode -Source $existing -Name "geo"
        Copy-IfPresent -Target $segNode -Source $existing -Name "mat"
        if ($matSourceById.ContainsKey($id)) {
            $segNode["matSource"] = [string]$matSourceById[$id]
        }
        $rebuiltSegments.Add([pscustomobject]$segNode) | Out-Null
        continue
    }

    if ($null -ne $existing) {
        # Keep existing face families in the used set for textureFamilies repair.
        if ($existing.PSObject.Properties.Name -contains "faceTextureFamily") {
            foreach ($fid in @($existing.faceTextureFamily)) {
                $f = [int]$fid
                if ($f -gt 0) { [void]$usedFamilyIds.Add($f) }
            }
        }
        $rebuiltSegments.Add($existing) | Out-Null
    }
}

# textureFamilies: keep source catalog, then ensure every MAT-referenced id exists.
$familyById = @{}
foreach ($fam in @($json.textureFamilies)) {
    if ($null -eq $fam) { continue }
    if (-not ($fam.PSObject.Properties.Name -contains "id")) { continue }
    $familyById[[int]$fam.id] = $fam
}

$missingFamilyIds = @($usedFamilyIds | Where-Object { -not $familyById.ContainsKey([int]$_) } | Sort-Object)
if ($missingFamilyIds.Count -gt 0) {
    Write-Host ("Aviso: {0} familyId(s) usados no MAT sem entrada em textureFamilies; criando stubs." -f $missingFamilyIds.Count)
    foreach ($fid in $missingFamilyIds) {
        $stub = [pscustomobject]@{
            id = [int]$fid
            name = ("family_{0}" -f $fid)
            sourceStem = ("family_{0}" -f $fid)
            variants = [pscustomobject]@{}
            imageFiles = [pscustomobject]@{}
            surfaceTypeId = 0
            surfaceType = "unknown"
        }
        $familyById[[int]$fid] = $stub
    }
}

$textureFamiliesOut = @($familyById.Values | Sort-Object { [int]$_.id })

$outJson = [ordered]@{}
Copy-IfPresent -Target $outJson -Source $json -Name "version"
if (-not $outJson.Contains("version")) {
    $outJson["version"] = 1
}
Copy-IfPresent -Target $outJson -Source $json -Name "generatedAtUtc"
$outJson["generatedAtUtc"] = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
Copy-IfPresent -Target $outJson -Source $json -Name "exporter"
if (-not $outJson.Contains("exporter")) {
    $outJson["exporter"] = [pscustomobject]@{
        format = "NyaExport"
        shading = $Shading
        texWidth = $TexWidth
        texHeight = $TexHeight
        texPadWidth = $TexPadWidth
        texHeaderV2 = $true
        texColorMode = "Paletted16"
    }
}
$outJson["textureFamilies"] = @($textureFamiliesOut)
$outJson["segments"] = @($rebuiltSegments.ToArray() | Sort-Object id)

[pscustomobject]$outJson | ConvertTo-Json -Depth 12 -Compress | Set-Content -LiteralPath $OutJsonPath -Encoding UTF8
Write-Host ("segments_map.json reconstruido a partir de {0} com {1} segmentos e {2} MATs (prefer M64>M32>M16>M8)" -f $sourceJson, $rebuiltSegments.Count, $matFilesUsed.Count)
if ($missingFamilyIds.Count -gt 0) {
    Write-Host ("family stubs adicionados: {0}" -f ($missingFamilyIds -join ","))
}
