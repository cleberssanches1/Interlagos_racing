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
$familyNodeByName = @{}
$nextFamilyId = 1
foreach ($family in @($json.textureFamilies)) {
    if ($null -eq $family -or -not $family.name) { continue }
    $key = [string]$family.name.ToLowerInvariant()
    $knownFamilies[$key] = $true
    $familyNodeByName[$key] = $family
    $familyId = [int]$family.id
    if ($familyId -ge $nextFamilyId) {
        $nextFamilyId = $familyId + 1
    }
}

function Set-FamilyLodMap($FamilyNode, [hashtable]$LodMap, [string]$SourceStem) {
    if ($null -eq $FamilyNode -or $null -eq $LodMap) { return }
    $variants = [ordered]@{}
    $imageFiles = [ordered]@{}
    foreach ($lod in $LodMap.Keys | Sort-Object {[int]$_}) {
        $variants[$lod] = $LodMap[$lod]
        $imageFiles[$lod] = $LodMap[$lod]
    }
    if ($FamilyNode -is [System.Collections.IDictionary]) {
        if ($variants.Count -gt 0) { $FamilyNode["variants"] = $variants }
        if ($imageFiles.Count -gt 0) { $FamilyNode["imageFiles"] = $imageFiles }
        if (-not [string]::IsNullOrWhiteSpace($SourceStem)) {
            $FamilyNode["sourceStem"] = $SourceStem
        }
        return
    }

    if ($variants.Count -gt 0) { $FamilyNode.variants = $variants }
    if ($imageFiles.Count -gt 0) { $FamilyNode.imageFiles = $imageFiles }
    if (-not [string]::IsNullOrWhiteSpace($SourceStem)) {
        $FamilyNode | Add-Member -NotePropertyName sourceStem -NotePropertyValue $SourceStem -Force
    }
}

$familyStems = @{}
$mtlAliases = @{}
$resolvedFamilyByStem = @{}
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
        if (-not $familyStems.ContainsKey($e.material)) {
            $familyStems[$e.material] = New-Object System.Collections.Generic.List[string]
        }
        if (-not ($familyStems[$e.material] -contains $e.texStem)) {
            $familyStems[$e.material].Add($e.texStem) | Out-Null
        }
    }

    $familiesToAppend = New-Object System.Collections.Generic.List[object]
    foreach ($lookupKey in $familyStems.Keys) {
        $baseFamily = $familyNodeByName[$lookupKey]
        $stems = @($familyStems[$lookupKey].ToArray())
        if ($stems.Count -eq 0) { continue }

        $baseStem = [string]$stems[0]
        Set-FamilyLodMap -FamilyNode $baseFamily -LodMap $sourceLookup[$baseStem] -SourceStem $baseStem
        $resolvedFamilyByStem[$baseStem] = [int]$baseFamily.id

        for ($i = 1; $i -lt $stems.Count; $i++) {
            $stem = [string]$stems[$i]
            if ($resolvedFamilyByStem.ContainsKey($stem)) { continue }

            $clone = [ordered]@{}
            $clone["id"] = $nextFamilyId
            $clone["name"] = ("{0}__{1}" -f [string]$baseFamily.name, $stem)
            $clone["sourceFamilyName"] = [string]$baseFamily.name
            $clone["sourceStem"] = $stem
            Set-FamilyLodMap -FamilyNode $clone -LodMap $sourceLookup[$stem] -SourceStem $stem
            $familiesToAppend.Add([pscustomobject]$clone) | Out-Null
            $resolvedFamilyByStem[$stem] = $nextFamilyId
            $nextFamilyId++
        }
    }

    foreach ($extra in $familiesToAppend) {
        $json.textureFamilies += $extra
    }

    foreach ($e in $mtlEntries) {
        if ($knownFamilies.ContainsKey($e.material)) { continue }
        if (-not $resolvedFamilyByStem.ContainsKey($e.texStem)) { continue }
        $familyId = [int]$resolvedFamilyByStem[$e.texStem]
        $familyNode = $json.textureFamilies | Where-Object { [int]$_.id -eq $familyId } | Select-Object -First 1
        if ($null -eq $familyNode) { continue }

        $aliasOwner = if ($familyNode.PSObject.Properties.Name -contains "sourceFamilyName") {
            [string]$familyNode.sourceFamilyName.ToLowerInvariant()
        } else {
            [string]$familyNode.name.ToLowerInvariant()
        }
        if (-not $mtlAliases.ContainsKey($aliasOwner)) {
            $mtlAliases[$aliasOwner] = New-Object System.Collections.Generic.List[string]
        }
        if (-not ($mtlAliases[$aliasOwner] -contains $e.material)) {
            $mtlAliases[$aliasOwner].Add($e.material) | Out-Null
        }
    }
}

foreach ($family in $json.textureFamilies) {
    if (-not $family.name) { continue }
    $lookupKey = $family.name.ToLowerInvariant()
    if ($family.PSObject.Properties.Name -contains "sourceStem") {
        $stem = [string]$family.sourceStem
        if ($sourceLookup.ContainsKey($stem)) {
            Set-FamilyLodMap -FamilyNode $family -LodMap $sourceLookup[$stem] -SourceStem $stem
        }
    }
    elseif ($familyLookup.ContainsKey($lookupKey)) {
        Set-FamilyLodMap -FamilyNode $family -LodMap $familyLookup[$lookupKey] -SourceStem ""
    }

    $aliasKey = if ($family.PSObject.Properties.Name -contains "sourceFamilyName") {
        [string]$family.sourceFamilyName.ToLowerInvariant()
    } else {
        $lookupKey
    }
    if ($mtlAliases.ContainsKey($aliasKey) -and $mtlAliases[$aliasKey].Count -gt 0) {
        $family | Add-Member -NotePropertyName aliases -NotePropertyValue @($mtlAliases[$aliasKey].ToArray()) -Force
    }
}

if (-not $json.textureFamilies -or $json.textureFamilies.Count -eq 0) {
    throw "textureFamilies vazio apos update"
}

$json | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8
Write-Host "segments_map.json atualizado com nomes renomeados: $SegmentsMapPath"
