param(
    [string]$DataDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$TextOutDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [switch]$UppercaseExt = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $DataDir)) {
    throw "DataDir nao encontrado: $DataDir"
}
if (-not (Test-Path -LiteralPath $TextOutDir)) {
    New-Item -ItemType Directory -Force -Path $TextOutDir | Out-Null
}

$sources = @(
    @{ Lod = 8;  Dir = (Join-Path $DataDir "8_ren")  },
    @{ Lod = 16; Dir = (Join-Path $DataDir "16_ren") },
    @{ Lod = 32; Dir = (Join-Path $DataDir "32_ren") },
    @{ Lod = 64; Dir = (Join-Path $DataDir "64_ren") }
)

function Build-TargetBaseName {
    param(
        [string]$Stem,
        [int]$Lod
    )

    $base = $Stem
    # remove sufixo lod ja existente no final, ex: xxx_64 / xxx64
    $base = [regex]::Replace($base, '_(8|16|32|64)$', '', 'IgnoreCase')
    $base = [regex]::Replace($base, '(8|16|32|64)$', '', 'IgnoreCase')
    if ([string]::IsNullOrWhiteSpace($base)) { $base = "T" }

    $lodTxt = [string]$Lod
    $withUnderscore = "$base`_$lodTxt"
    if ($withUnderscore.Length -le 8) {
        return $withUnderscore
    }

    $noUnderscore = "$base$lodTxt"
    if ($noUnderscore.Length -le 8) {
        return $noUnderscore
    }

    # ainda estourou: truncar base para caber sem underscore
    $maxBase = 8 - $lodTxt.Length
    if ($maxBase -lt 1) { $maxBase = 1 }
    if ($base.Length -gt $maxBase) {
        $base = $base.Substring(0, $maxBase)
    }
    return "$base$lodTxt"
}

function Extract-FamilyBase {
    param([string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) { return "" }
    if ($Name -match '^(?<base>.+?)_(8|16|32|64)$') {
        return $Matches['base']
    }
    return $Name
}

$used = @{}
$entries = New-Object System.Collections.Generic.List[object]
$copied = 0
$skipped = 0

foreach ($src in $sources) {
    $lod = [int]$src.Lod
        $files = @()
        if (Test-Path -LiteralPath $src.Dir) {
            $files += Get-ChildItem -LiteralPath $src.Dir -File | Where-Object { $_.Extension -ieq ".tga" }
        }
        if ($files.Count -eq 0) {
            $fallbackDir = Join-Path (Join-Path $ResultDir ("obj_$($lod)")) "ARQ_TGA"
            if (Test-Path -LiteralPath $fallbackDir) {
                $files += Get-ChildItem -LiteralPath $fallbackDir -File | Where-Object { $_.Extension -ieq ".tga" }
            }
        }
        if ($files.Count -eq 0) {
            $pattern = "^F\\d{3}_?$lod\\.TGA$"
            $files += Get-ChildItem -LiteralPath $DataDir -File | Where-Object { $_.Name -match $pattern }
        }
        $files = $files | Sort-Object Name
        foreach ($f in $files) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        $newBase = Build-TargetBaseName -Stem $stem -Lod $lod
        $newExt = if ($UppercaseExt) { ".TGA" } else { ".tga" }
        $newName = "$newBase$newExt"
        $key = $newName.ToLowerInvariant()
        $family = Extract-FamilyBase $stem
        $familyKey = if ([string]::IsNullOrWhiteSpace($family)) {
            $newBase
        } else {
            $family
        }
        $familyKey = $familyKey.ToLowerInvariant()

        if ($used.ContainsKey($key)) {
            $n = 1
            do {
                $suffix = [string]$n
                $maxBase = 8 - $suffix.Length
                if ($maxBase -lt 1) { $maxBase = 1 }
                $adjBase = if ($newBase.Length -gt $maxBase) { $newBase.Substring(0, $maxBase) } else { $newBase }
                $newName = "$adjBase$suffix$newExt"
                $key = $newName.ToLowerInvariant()
                $n++
            } while ($used.ContainsKey($key))
        }
        $used[$key] = $true

        $dstPath = Join-Path $DataDir $newName
        Copy-Item -LiteralPath $f.FullName -Destination $dstPath -Force
        $copied++

        $entries.Add([pscustomobject]@{
            source_folder = [string]$src.Dir
            source_name = [string]$f.Name
            source_path = [string]$f.FullName
            lod = [int]$lod
            target_name = [string]$newName
            target_path = [string]$dstPath
            family = [string]$familyKey
        }) | Out-Null
    }
}

$manifestPath = Join-Path $DataDir "ren_textures_copy_map.json"
$manifestShortPath = Join-Path $TextOutDir "RTMAP.TXT"
$manifest = [ordered]@{
    version = 1
    generated_at_utc = [string]([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"))
    data_dir = [string]$DataDir
    copied = [int]$copied
    skipped = [int]$skipped
    items = @($entries.ToArray())
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestShortPath -Encoding ASCII

Write-Host ("OK copied: {0}" -f $copied)
Write-Host ("Map: {0}" -f $manifestPath)
Write-Host ("Map short: {0}" -f $manifestShortPath)
