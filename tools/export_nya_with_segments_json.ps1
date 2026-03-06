param(
    [string]$ConverterDir = "C:\saturn\tools\ModelConverter-linux-main\BuildDrop",
    [string]$SourceObjDir = "C:\Models\png\sectors\result",
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$CdDataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$Pattern = "seg_*.obj",
    [ValidateSet("Smooth", "Flat")]
    [string]$Shading = "Smooth",
    [int]$TexWidth = 32,
    [int]$TexHeight = 32,
    [int]$TexPadWidth = 8,
    [switch]$PreserveSidecars
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$Path) {
    New-Item -Path $Path -ItemType Directory -Force | Out-Null
}

function Get-SegmentIdFromFile([string]$BaseName) {
    if ($BaseName -match '^seg_(\d+)$') {
        return [int]$Matches[1]
    }
    return $null
}

function Normalize-Token([string]$Token) {
    if ([string]::IsNullOrWhiteSpace($Token)) { return "" }
    $t = $Token.Trim()
    $t = [System.IO.Path]::GetFileName($t)
    return $t
}

function Parse-TextureFamily([string]$Token) {
    $t = Normalize-Token $Token
    # Ex: asfalto_64.001 / asfalto_64.tga / asfalto_64.png
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

function New-Rle([object[]]$Values) {
    $runs = New-Object System.Collections.Generic.List[object]
    if (-not $Values -or $Values.Count -eq 0) { return @() }

    $start = 0
    $curr = [int]$Values[0]
    $count = 1
    for ($i = 1; $i -lt $Values.Count; $i++) {
        $v = [int]$Values[$i]
        if ($v -eq $curr) {
            $count++
            continue
        }
        $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $curr }) | Out-Null
        $start = $i
        $curr = $v
        $count = 1
    }
    $runs.Add([pscustomobject]@{ start = $start; count = $count; familyId = $curr }) | Out-Null
    return @($runs.ToArray())
}

if (-not (Test-Path $ConverterDir)) { throw "ConverterDir nao encontrado: $ConverterDir" }
if (-not (Test-Path $SourceObjDir)) { throw "SourceObjDir nao encontrado: $SourceObjDir" }

Ensure-Dir $ResultDir
Ensure-Dir $CdDataDir

$converterDll = Join-Path $ConverterDir "ModelConverter.dll"
if (-not (Test-Path $converterDll)) { throw "ModelConverter.dll nao encontrado em $ConverterDir" }

    $ok = 0
    $fail = New-Object System.Collections.Generic.List[string]
    $generatedSegIds = New-Object System.Collections.Generic.List[int]
    $convertedSegments = New-Object 'System.Collections.Generic.HashSet[int]'

Push-Location $ConverterDir
try {
    $objs = Get-ChildItem -Path $SourceObjDir -Recurse -Filter $Pattern -File | Sort-Object Name
    foreach ($obj in $objs) {
        $id = Get-SegmentIdFromFile $obj.BaseName
        if ($null -eq $id) {
            $fail.Add($obj.Name) | Out-Null
            continue
        }
        if ($convertedSegments.Contains($id)) {
            continue
        }

        $outName = ("SEG_{0:D3}.NYA" -f $id)
        $out = Join-Path $ResultDir $outName
        Write-Host "Convertendo $($obj.Name) -> $outName"

        dotnet .\ModelConverter.dll `
            -i $obj.FullName `
            -o $out `
            -exp NyaExport `
            -t $Shading `
            -order Keep `
            -tex-width $TexWidth `
            -tex-height $TexHeight `
            -tex-pad-width $TexPadWidth `
            -tex-header-v2 `
            -tex-color-mode Paletted16

        if ($LASTEXITCODE -eq 0) {
            $ok++
            $generatedSegIds.Add($id) | Out-Null
            $convertedSegments.Add($id) | Out-Null
        } else {
            $fail.Add($obj.Name) | Out-Null
        }
    }
}
finally {
    Pop-Location
}

Write-Host "Geracao concluida: OK=$ok FAIL=$($fail.Count)"
if ($fail.Count -gt 0) {
    $fail | ForEach-Object { Write-Host "Falhou: $_" }
}

# =========================
# 2) Gera segments_map.json consolidado
# =========================
$familyIdByName = @{}
$families = New-Object System.Collections.Generic.List[object]
$segments = New-Object System.Collections.Generic.List[object]

foreach ($id in ($generatedSegIds | Sort-Object -Unique)) {
    $base = ("SEG_{0:D3}" -f $id)
    $nyaName = "$base.NYA"
    $mapPath = Join-Path $ResultDir "$base.map"
    $meshtexPath = Join-Path $ResultDir "$base.meshtex"

    if (-not (Test-Path $mapPath)) {
        Write-Host "Aviso: sem map para $base"
        continue
    }

    $faceFamilies = New-Object System.Collections.Generic.List[int]
    $faceTokens = New-Object System.Collections.Generic.List[string]

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
                    "8" = ("{0}_8{1}" -f $familyName, $info.Suffix)
                    "16" = ("{0}_16{1}" -f $familyName, $info.Suffix)
                    "32" = ("{0}_32{1}" -f $familyName, $info.Suffix)
                    "64" = ("{0}_64{1}" -f $familyName, $info.Suffix)
                }
                imageFiles = [ordered]@{
                    "8" = ("{0}_8.tga" -f $familyName)
                    "16" = ("{0}_16.tga" -f $familyName)
                    "32" = ("{0}_32.tga" -f $familyName)
                    "64" = ("{0}_64.tga" -f $familyName)
                }
            }) | Out-Null
        }

        $fid = [int]$familyIdByName[$familyName]
        $faceFamilies.Add($fid) | Out-Null
        $faceTokens.Add($token) | Out-Null
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
    $segments.Add([pscustomobject]@{
        id = $id
        nya = $nyaName
        faceCount = $faceArray.Count
        faceTextureFamily = $faceArray
        faceTextureFamilyRle = @(New-Rle $faceArray)
        meshes = @($meshes.ToArray())
    }) | Out-Null
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
    segments = @($segments | Sort-Object id)
}

$jsonPath = Join-Path $ResultDir "segments_map.json"
$jsonObj | ConvertTo-Json -Depth 12 | Set-Content -Path $jsonPath -Encoding UTF8
Write-Host "JSON gerado: $jsonPath"

# =========================
# 3) Copia NYA + JSON para cd\data
# =========================
Get-ChildItem -Path $ResultDir -File -Filter "SEG_*.NYA" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $CdDataDir $_.Name) -Force
}
Copy-Item -LiteralPath $jsonPath -Destination (Join-Path $CdDataDir "segments_map.json") -Force
Write-Host "Arquivos copiados para: $CdDataDir"

  # =========================
  # 4) Limpa sidecars .map/.meshtex
  # =========================
  if (-not $PreserveSidecars)
  {
      Get-ChildItem -Path $ResultDir -File -Filter *.map | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Path $ResultDir -File -Filter *.meshtex | Remove-Item -Force -ErrorAction SilentlyContinue
      Write-Host "Sidecars removidos (.map/.meshtex) de: $ResultDir"
  }
