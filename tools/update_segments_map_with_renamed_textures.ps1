param(
    [string]$SegmentsMapPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\segments_map.json",
    [string]$RenManifestPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\ren_textures_copy_map.json",
    [string]$ResultDir = "C:\Models\png\sectors\result"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SegmentsMapPath)) { throw "Segments map nao encontrado: $SegmentsMapPath" }
if (-not (Test-Path -LiteralPath $RenManifestPath)) { throw "Manifest de texturas renomeadas nao encontrado: $RenManifestPath" }

$json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath $RenManifestPath -Raw | ConvertFrom-Json

$familyLookup = @{}
$sourceLookup = @{}
foreach ($item in $manifest.items) {
    $family = [string]$item.family
    if ([string]::IsNullOrWhiteSpace($family)) { continue }
    $family = $family.ToLowerInvariant()
    if (-not $familyLookup.ContainsKey($family)) { $familyLookup[$family] = @{} }
    $lodKey = [string]$item.lod
    $familyLookup[$family][$lodKey] = [string]$item.target_name

    $sourceStem = [System.IO.Path]::GetFileNameWithoutExtension([string]$item.source_name)
    if (-not [string]::IsNullOrWhiteSpace($sourceStem)) {
        $sourceKey = $sourceStem.ToLowerInvariant()
        if (-not $sourceLookup.ContainsKey($sourceKey)) { $sourceLookup[$sourceKey] = @{} }
        $sourceLookup[$sourceKey][$lodKey] = [string]$item.target_name
    }
}

function Normalize-MaterialFamilyName([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name)) { return "" }
    $n = $Name.Trim()
    $n = [regex]::Replace($n, '_(8|16|32|64)(\.[^\\\/]+)?$', '', 'IgnoreCase')
    return $n
}

function Get-TextureStem([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    return [System.IO.Path]::GetFileNameWithoutExtension($Path).ToLowerInvariant()
}

$knownFamilies = @{}
foreach ($family in @($json.textureFamilies)) {
    if ($null -eq $family -or -not $family.name) { continue }
    $knownFamilies[[string]$family.name.ToLowerInvariant()] = $true
}

$mtlFamilyLookup = @{}
$mtlAliases = @{}
$texStemToFamilyName = @{}
$obj64Dir = Join-Path $ResultDir "obj_64"
if (Test-Path -LiteralPath $obj64Dir) {
    $mtlFiles = @(Get-ChildItem -LiteralPath $obj64Dir -File -Filter *.mtl)
    $mtlEntries = New-Object System.Collections.Generic.List[object]
    foreach ($mtl in $mtlFiles) {
        $currentFamily = ""
        foreach ($line in Get-Content -LiteralPath $mtl.FullName) {
            $t = $line.Trim()
            if ($t.StartsWith("newmtl ")) {
                $currentFamily = (Normalize-MaterialFamilyName ($t.Substring(7).Trim())).ToLowerInvariant()
                continue
            }
            if ([string]::IsNullOrWhiteSpace($currentFamily)) { continue }
            if ($t -notmatch '^(?i)map_Kd\s+(.+)$') { continue }

            $texStem = Get-TextureStem $Matches[1].Trim()
            if ([string]::IsNullOrWhiteSpace($texStem)) { continue }
            $mtlEntries.Add([pscustomobject]@{
                material = $currentFamily
                texStem = $texStem
            }) | Out-Null
        }
    }

    foreach ($e in $mtlEntries) {
        if (-not $knownFamilies.ContainsKey($e.material)) { continue }
        if (-not $sourceLookup.ContainsKey($e.texStem)) { continue }
        $texStemToFamilyName[$e.texStem] = $e.material
        $mtlFamilyLookup[$e.material] = $sourceLookup[$e.texStem]
    }

    foreach ($e in $mtlEntries) {
        if ($knownFamilies.ContainsKey($e.material)) { continue }
        if (-not $texStemToFamilyName.ContainsKey($e.texStem)) { continue }
        $logicalFamily = [string]$texStemToFamilyName[$e.texStem]
        if (-not $mtlAliases.ContainsKey($logicalFamily)) {
            $mtlAliases[$logicalFamily] = New-Object System.Collections.Generic.List[string]
        }
        if (-not ($mtlAliases[$logicalFamily] -contains $e.material)) {
            $mtlAliases[$logicalFamily].Add($e.material) | Out-Null
        }
        if (-not $mtlFamilyLookup.ContainsKey($logicalFamily) -and $sourceLookup.ContainsKey($e.texStem)) {
            $mtlFamilyLookup[$logicalFamily] = $sourceLookup[$e.texStem]
        }
    }
}

foreach ($family in $json.textureFamilies) {
    if (-not $family.name) { continue }
    $lookupKey = $family.name.ToLowerInvariant()
    $lodMap = $null
    if ($familyLookup.ContainsKey($lookupKey)) {
        $lodMap = $familyLookup[$lookupKey]
    }
    elseif ($mtlFamilyLookup.ContainsKey($lookupKey)) {
        $lodMap = $mtlFamilyLookup[$lookupKey]
    }
    if ($null -eq $lodMap) {
        if ($mtlAliases.ContainsKey($lookupKey) -and $mtlAliases[$lookupKey].Count -gt 0) {
            $family | Add-Member -NotePropertyName aliases -NotePropertyValue @($mtlAliases[$lookupKey].ToArray()) -Force
        }
        continue
    }
    $variants = [ordered]@{}
    $imageFiles = [ordered]@{}
    foreach ($lod in $lodMap.Keys | Sort-Object {[int]$_}) {
        $variants[$lod] = $lodMap[$lod]
        $imageFiles[$lod] = $lodMap[$lod]
    }
    if ($variants.Count -gt 0) { $family.variants = $variants }
    if ($imageFiles.Count -gt 0) { $family.imageFiles = $imageFiles }
    if ($mtlAliases.ContainsKey($lookupKey) -and $mtlAliases[$lookupKey].Count -gt 0) {
        $family | Add-Member -NotePropertyName aliases -NotePropertyValue @($mtlAliases[$lookupKey].ToArray()) -Force
    }
}

if (-not $json.textureFamilies -or $json.textureFamilies.Count -eq 0) {
    throw "textureFamilies vazio apos update"
}

$json | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8
Write-Host "segments_map.json atualizado com nomes renomeados: $SegmentsMapPath"
