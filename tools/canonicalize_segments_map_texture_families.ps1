param(
    [string]$SegmentsMapPath = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing\segments_map.json"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SegmentsMapPath)) {
    throw "Segments map nao encontrado: $SegmentsMapPath"
}

function Normalize-StemKey([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return "" }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Value.Trim())
    if ([string]::IsNullOrWhiteSpace($name)) { return "" }
    return ([regex]::Replace($name.ToLowerInvariant(), '[^a-z0-9]+', ''))
}

function Get-FamilyCanonicalKey($FamilyNode) {
    if ($null -eq $FamilyNode) { return "" }

    if ($FamilyNode.PSObject.Properties.Name -contains "sourceStem") {
        $stemKey = Normalize-StemKey ([string]$FamilyNode.sourceStem)
        if (-not [string]::IsNullOrWhiteSpace($stemKey)) {
            return "stem:$stemKey"
        }
    }

    $lodPairs = New-Object System.Collections.Generic.List[string]
    foreach ($propName in @("variants", "imageFiles")) {
        if (-not ($FamilyNode.PSObject.Properties.Name -contains $propName)) { continue }
        $node = $FamilyNode.$propName
        if ($null -eq $node) { continue }
        foreach ($lod in @("8", "16", "32", "64")) {
            if (-not ($node.PSObject.Properties.Name -contains $lod)) { continue }
            $stemKey = Normalize-StemKey ([string]$node.$lod)
            if ([string]::IsNullOrWhiteSpace($stemKey)) { continue }
            $lodPairs.Add(("{0}:{1}" -f $lod, $stemKey)) | Out-Null
        }
        if ($lodPairs.Count -gt 0) {
            return "variants:$([string]::Join('|', @($lodPairs.ToArray())))"
        }
    }

    return ""
}

function Add-AliasName([System.Collections.Generic.HashSet[string]]$AliasSet, [string]$Alias) {
    if ($null -eq $AliasSet) { return }
    if ([string]::IsNullOrWhiteSpace($Alias)) { return }
    [void]$AliasSet.Add($Alias.Trim())
}

function Get-CanonicalNameRank($FamilyNode) {
    if ($null -eq $FamilyNode) { return 2 }
    if (-not ($FamilyNode.PSObject.Properties.Name -contains "name")) { return 2 }
    $name = [string]$FamilyNode.name
    if ([string]::IsNullOrWhiteSpace($name)) { return 2 }
    if ($name -match '\.\d+$') { return 1 }
    return 0
}

function Get-SourceFamilyRank($FamilyNode) {
    if ($null -eq $FamilyNode) { return 1 }
    if ($FamilyNode.PSObject.Properties.Name -contains "sourceFamilyName" -and
        -not [string]::IsNullOrWhiteSpace([string]$FamilyNode.sourceFamilyName)) {
        return 1
    }
    return 0
}

function Select-CanonicalFamily([object[]]$Group) {
    $best = $null
    foreach ($family in @($Group)) {
        if ($null -eq $family) { continue }
        if ($null -eq $best) {
            $best = $family
            continue
        }

        $familySourceRank = Get-SourceFamilyRank $family
        $bestSourceRank = Get-SourceFamilyRank $best
        if ($familySourceRank -lt $bestSourceRank) {
            $best = $family
            continue
        }
        if ($familySourceRank -gt $bestSourceRank) {
            continue
        }

        $familyNameRank = Get-CanonicalNameRank $family
        $bestNameRank = Get-CanonicalNameRank $best
        if ($familyNameRank -lt $bestNameRank) {
            $best = $family
            continue
        }
        if ($familyNameRank -gt $bestNameRank) {
            continue
        }

        if ([int]$family.id -lt [int]$best.id) {
            $best = $family
        }
    }
    return $best
}

function Rebuild-Rle([int[]]$Values) {
    $out = New-Object System.Collections.Generic.List[object]
    if ($null -eq $Values -or $Values.Count -eq 0) { return @() }

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

$json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
if ($null -eq $json -or $null -eq $json.textureFamilies) {
    throw "segments_map invalido ou sem textureFamilies: $SegmentsMapPath"
}

$families = @($json.textureFamilies)
$groupByKey = @{}
foreach ($family in $families) {
    if ($null -eq $family) { continue }
    $key = Get-FamilyCanonicalKey $family
    if ([string]::IsNullOrWhiteSpace($key)) { continue }
    if (-not $groupByKey.ContainsKey($key)) {
        $groupByKey[$key] = New-Object System.Collections.Generic.List[object]
    }
    $groupByKey[$key].Add($family) | Out-Null
}

$canonicalById = @{}
$aliasNamesByCanonicalId = @{}
$groupCount = 0
$removedFamilyCount = 0

foreach ($key in $groupByKey.Keys) {
    $group = @($groupByKey[$key].ToArray())
    if ($group.Count -le 1) { continue }
    $groupCount++

    $canonical = Select-CanonicalFamily $group

    if ($null -eq $canonical) { continue }
    $canonicalId = [int]$canonical.id
    $aliasSet = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::OrdinalIgnoreCase)

    if ($canonical.PSObject.Properties.Name -contains "aliases" -and $canonical.aliases) {
        foreach ($alias in @($canonical.aliases)) {
            Add-AliasName $aliasSet ([string]$alias)
        }
    }

    foreach ($family in $group) {
        $familyId = [int]$family.id
        $canonicalById[$familyId] = $canonicalId
        if ($familyId -eq $canonicalId) { continue }
        $removedFamilyCount++

        if ($family.PSObject.Properties.Name -contains "name") {
            Add-AliasName $aliasSet ([string]$family.name)
        }
        if ($family.PSObject.Properties.Name -contains "aliases" -and $family.aliases) {
            foreach ($alias in @($family.aliases)) {
                Add-AliasName $aliasSet ([string]$alias)
            }
        }

        if (-not ($canonical.PSObject.Properties.Name -contains "sourceStem") -and
            $family.PSObject.Properties.Name -contains "sourceStem" -and
            -not [string]::IsNullOrWhiteSpace([string]$family.sourceStem)) {
            $canonical | Add-Member -NotePropertyName sourceStem -NotePropertyValue ([string]$family.sourceStem) -Force
        }
    }

    if ($aliasSet.Count -gt 0) {
        $aliasNamesByCanonicalId[$canonicalId] = @($aliasSet | Sort-Object)
    }
}

if ($removedFamilyCount -eq 0) {
    Write-Host "Canonizacao: nenhum alias de textura para consolidar."
    exit 0
}

foreach ($family in $families) {
    if ($null -eq $family) { continue }
    $familyId = [int]$family.id
    if ($aliasNamesByCanonicalId.ContainsKey($familyId)) {
        $family | Add-Member -NotePropertyName aliases -NotePropertyValue $aliasNamesByCanonicalId[$familyId] -Force
    }
}

$keptFamilies = New-Object System.Collections.Generic.List[object]
foreach ($family in $families | Sort-Object { [int]$_.id }) {
    if ($null -eq $family) { continue }
    $familyId = [int]$family.id
    if ($canonicalById.ContainsKey($familyId) -and $canonicalById[$familyId] -ne $familyId) {
        continue
    }
    $keptFamilies.Add($family) | Out-Null
}
$json.textureFamilies = @($keptFamilies.ToArray())

if ($json.PSObject.Properties.Name -contains "segments" -and $json.segments) {
    foreach ($segment in @($json.segments)) {
        if ($null -eq $segment) { continue }

        if ($segment.PSObject.Properties.Name -contains "faceTextureFamily" -and $segment.faceTextureFamily) {
            $remappedFaceFamilies = @(
                $segment.faceTextureFamily | ForEach-Object {
                    $fid = [int]$_
                    if ($canonicalById.ContainsKey($fid)) { $canonicalById[$fid] } else { $fid }
                }
            )
            $segment.faceTextureFamily = $remappedFaceFamilies
            $segment | Add-Member -NotePropertyName faceTextureFamilyRle -NotePropertyValue @(Rebuild-Rle $remappedFaceFamilies) -Force
        }

        if ($segment.PSObject.Properties.Name -contains "faces" -and $segment.faces) {
            foreach ($face in @($segment.faces)) {
                if ($null -eq $face) { continue }
                if (-not ($face.PSObject.Properties.Name -contains "familyId")) { continue }
                $fid = [int]$face.familyId
                if ($canonicalById.ContainsKey($fid)) {
                    $face.familyId = [int]$canonicalById[$fid]
                }
            }
        }

        if ($segment.PSObject.Properties.Name -contains "faceTextureFamilyRle" -and
            $segment.PSObject.Properties.Name -contains "faceTextureFamily" -and
            $segment.faceTextureFamily) {
            $segment.faceTextureFamilyRle = @(Rebuild-Rle @($segment.faceTextureFamily | ForEach-Object { [int]$_ }))
        }

        if ($segment.PSObject.Properties.Name -contains "meshes" -and $segment.meshes) {
            foreach ($mesh in @($segment.meshes)) {
                if ($null -eq $mesh) { continue }
                if (-not ($mesh.PSObject.Properties.Name -contains "textureFamilies")) { continue }
                $seen = New-Object System.Collections.Generic.HashSet[int]
                $remapped = New-Object System.Collections.Generic.List[int]
                foreach ($raw in @($mesh.textureFamilies)) {
                    $fid = [int]$raw
                    if ($canonicalById.ContainsKey($fid)) { $fid = [int]$canonicalById[$fid] }
                    if ($fid -le 0) { continue }
                    if ($seen.Add($fid)) {
                        $remapped.Add($fid) | Out-Null
                    }
                }
                $mesh.textureFamilies = @($remapped.ToArray())
            }
        }
    }
}

$json | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8
Write-Host ("Canonizacao concluida: grupos={0} familias_removidas={1} familias_finais={2}" -f
    $groupCount,
    $removedFamilyCount,
    @($json.textureFamilies).Count)
