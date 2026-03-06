param(
    [int]$SegmentId = 1,
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$OutJsonPath = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing\segments_map.json",
    [ValidateSet("Smooth","Flat")]
    [string]$Shading = "Smooth",
    [int]$TexWidth = 8,
    [int]$TexHeight = 8,
    [int]$TexPadWidth = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-Token([string]$Token) {
    if ([string]::IsNullOrWhiteSpace($Token)) { return "" }
    $t = $Token.Trim()
    return [System.IO.Path]::GetFileName($t)
}

function Parse-TextureFamily([string]$Token) {
    $t = Normalize-Token $Token
    # Exemplos:
    # asfalto_64.001
    # asfalto_64.tga
    # asfalto_64.png
    $m = [regex]::Match($t, '^(?<name>.+)_(?<lod>8|16|32|64)(?<suffix>\..+)?$')
    if ($m.Success) {
        $name = $m.Groups['name'].Value
        $suffix = $m.Groups['suffix'].Value
        if ([string]::IsNullOrWhiteSpace($suffix)) { $suffix = ".001" }
        return [pscustomobject]@{
            FamilyName = $name
            LOD = [int]$m.Groups['lod'].Value
            Suffix = $suffix
            Token = $t
        }
    }

    return [pscustomobject]@{
        FamilyName = $t
        LOD = $null
        Suffix = ".001"
        Token = $t
    }
}

function New-Rle([int[]]$Values) {
    $runs = New-Object System.Collections.Generic.List[object]
    if (-not $Values -or $Values.Count -eq 0) { return @() }

    $start = 0
    $curr = $Values[0]
    $count = 1
    for ($i = 1; $i -lt $Values.Count; $i++) {
        if ($Values[$i] -eq $curr) {
            $count++
            continue
        }
        $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $curr }) | Out-Null
        $start = $i
        $curr = $Values[$i]
        $count = 1
    }
    $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $curr }) | Out-Null
    return @($runs.ToArray())
}

if (-not (Test-Path $ResultDir)) { throw "ResultDir nao encontrado: $ResultDir" }
$outDir = Split-Path -Parent $OutJsonPath
if (-not (Test-Path $outDir)) { New-Item -Path $outDir -ItemType Directory -Force | Out-Null }

$base = ("SEG_{0:D3}" -f $SegmentId)
$nyaName = "$base.NYA"
$mapPath = Join-Path $ResultDir "$base.map"
$meshtexPath = Join-Path $ResultDir "$base.meshtex"

if (-not (Test-Path $mapPath)) {
    throw "Arquivo .map nao encontrado: $mapPath"
}

$familyIdByName = @{}
$families = New-Object System.Collections.Generic.List[object]
$faceFamilies = New-Object System.Collections.Generic.List[int]

Get-Content -Path $mapPath | ForEach-Object {
    $token = Normalize-Token $_
    if ([string]::IsNullOrWhiteSpace($token)) { return }
    $info = Parse-TextureFamily $token
    $familyName = $info.FamilyName

    if (-not $familyIdByName.ContainsKey($familyName)) {
        $newId = $families.Count + 1
        $familyIdByName[$familyName] = $newId
        $families.Add([pscustomobject]@{
            id = $newId
            name = $familyName
            variants = [ordered]@{
                "8"  = ("{0}_8{1}" -f $familyName, $info.Suffix)
                "16" = ("{0}_16{1}" -f $familyName, $info.Suffix)
                "32" = ("{0}_32{1}" -f $familyName, $info.Suffix)
                "64" = ("{0}_64{1}" -f $familyName, $info.Suffix)
            }
            imageFiles = [ordered]@{
                "8"  = ("{0}_8.tga" -f $familyName)
                "16" = ("{0}_16.tga" -f $familyName)
                "32" = ("{0}_32.tga" -f $familyName)
                "64" = ("{0}_64.tga" -f $familyName)
            }
        }) | Out-Null
    }

    $fid = [int]$familyIdByName[$familyName]
    $faceFamilies.Add($fid) | Out-Null
}

$meshes = New-Object System.Collections.Generic.List[object]
if (Test-Path $meshtexPath) {
    Get-Content -Path $meshtexPath | ForEach-Object {
        $line = $_.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) { return }
        $parts = $line -split ';', 3
        if ($parts.Count -lt 3) { return }
        $meshId = [int]$parts[0]
        $meshName = $parts[1]
        $tokens = @()
        if (-not [string]::IsNullOrWhiteSpace($parts[2])) {
            $tokens = $parts[2].Split(',') | ForEach-Object { Normalize-Token $_ } | Where-Object { $_ -ne "" }
        }

        $meshFamilyIds = New-Object System.Collections.Generic.List[int]
        foreach ($t in ($tokens | Select-Object -Unique)) {
            $info = Parse-TextureFamily $t
            if ($familyIdByName.ContainsKey($info.FamilyName)) {
                $meshFamilyIds.Add([int]$familyIdByName[$info.FamilyName]) | Out-Null
            }
        }

        $meshes.Add([pscustomobject]@{
            meshId = $meshId
            name = $meshName
            textureFamilies = @($meshFamilyIds.ToArray())
        }) | Out-Null
    }
}

$faceArray = @($faceFamilies.ToArray())
$segmentNode = [pscustomobject]@{
    id = $SegmentId
    nya = $nyaName
    faceCount = $faceArray.Count
    faceTextureFamily = $faceArray
    faceTextureFamilyRle = @(New-Rle $faceArray)
    meshes = @($meshes)
}

$jsonObj = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    exporter = [pscustomobject]@{
        format = "NyaExport"
        shading = $Shading
        texWidth = $TexWidth
        texHeight = $TexHeight
        texPadWidth = $TexPadWidth
        texHeaderV2 = $true
        texColorMode = "Paletted16"
    }
    textureFamilies = @($families | Sort-Object id)
    segments = @($segmentNode)
}

$jsonObj | ConvertTo-Json -Depth 12 | Set-Content -Path $OutJsonPath -Encoding UTF8
Write-Host "OK: $OutJsonPath"
Write-Host ("Segment:{0} Faces:{1} Families:{2}" -f $SegmentId, $faceArray.Count, $families.Count)
