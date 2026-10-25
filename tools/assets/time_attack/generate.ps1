param([string]$MagickPath = 'magick')

$ErrorActionPreference = 'Stop'
$project = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$source = Join-Path $project 'cd\data\SPDHUD.TGA'
$target = Join-Path $project 'cd\data\TAHUD.TGA'
$magick = Get-Command $MagickPath -ErrorAction Stop
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Atlas base ausente: $source" }
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ('ta-hud-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $baseRaw = Join-Path $temp 'base.rgba'
    $extraRaw = Join-Path $temp 'extra.rgba'
    $combinedRaw = Join-Path $temp 'combined.rgba'
    & $magick.Source $source -depth 8 "RGBA:$baseRaw"
    if ($LASTEXITCODE -ne 0) { throw 'Falha lendo atlas original.' }
    & $magick.Source -background none (Join-Path $PSScriptRoot 'TA_EXTRA.svg') -channel A -threshold '20%' +channel -depth 8 "RGBA:$extraRaw"
    if ($LASTEXITCODE -ne 0) { throw 'Falha rasterizando sprites do Time Attack.' }
    $base = [System.IO.File]::ReadAllBytes($baseRaw)
    $extra = [System.IO.File]::ReadAllBytes($extraRaw)
    if ($base.Length -ne 256 * 240 * 4 -or $extra.Length -ne 256 * 96 * 4) {
        throw 'Dimensoes RGBA inesperadas nos atlases.'
    }

    # Place the new art only in cells unused by the resident speedometer.
    # This preserves its 256x240 parser and VRAM footprint exactly.
    function CopyCell([int]$sx, [int]$sy, [int]$dx, [int]$dy) {
        for ($py = 0; $py -lt 8; $py++) {
            for ($px = 0; $px -lt 8; $px++) {
                $from = (($sy + $py) * 256 + $sx + $px) * 4
                if ($extra[$from + 3] -eq 0) { continue }
                $to = (($dy + $py) * 256 + $dx + $px) * 4
                [System.Array]::Copy($extra, $from, $base, $to, 4)
            }
        }
    }

    for ($i = 0; $i -lt 8; $i++) {
        CopyCell ($i * 8) 0 (80 + $i * 8) 0                 # TOTAL TIME
        CopyCell (64 + $i * 8) 0 (80 + $i * 8) 16           # LAP TIME
    }
    for ($i = 0; $i -lt 7; $i++) {
        CopyCell ($i * 8) 8 (80 + $i * 8) 32               # BEST LAP
    }
    for ($i = 0; $i -lt 7; $i++) {
        CopyCell (64 + $i * 8) 8 (80 + $i * 8) 48          # COURSE RECORD
    }

    $white = @(
        @(13,0), @(14,0), @(15,0), @(13,1), @(14,1), @(15,1),
        @(12,2), @(13,2), @(14,2), @(15,2), @(12,3), @(13,3), @(14,3)
    )
    $red = @(
        @(15,3), @(5,4), @(6,4), @(7,4), @(8,4), @(9,4), @(10,4),
        @(11,4), @(12,4), @(13,4), @(14,4), @(15,4), @(12,5)
    )
    for ($i = 0; $i -lt 13; $i++) {
        CopyCell ($i * 8) 16 ($white[$i][0] * 16) ($white[$i][1] * 16)
        CopyCell ($i * 8) 24 ($red[$i][0] * 16) ($red[$i][1] * 16)
    }

    $countdown = @(
        @(13,5), @(14,5), @(15,5), @(10,6), @(11,6), @(12,6),
        @(13,6), @(14,6), @(15,6), @(8,14), @(9,14), @(10,14),
        @(11,14), @(12,14), @(13,14), @(14,14)
    )
    for ($sprite = 0; $sprite -lt 4; $sprite++) {
        for ($row = 0; $row -lt 2; $row++) {
            for ($col = 0; $col -lt 2; $col++) {
                $index = $sprite * 4 + $row * 2 + $col
                CopyCell ($sprite * 16 + $col * 8) (32 + $row * 8) `
                    ($countdown[$index][0] * 16) ($countdown[$index][1] * 16)
            }
        }
    }

    [System.IO.File]::WriteAllBytes($combinedRaw, $base)
    & $magick.Source -size '256x240' -depth 8 "RGBA:$combinedRaw" -alpha on -colors 16 -type PaletteAlpha -compress none $target
    if ($LASTEXITCODE -ne 0) { throw 'Falha gerando TAHUD.TGA.' }
    $bytes = [System.IO.File]::ReadAllBytes($target)
    $paletteCount = [int]$bytes[5] + 256 * [int]$bytes[6]
    $width = [int]$bytes[12] + 256 * [int]$bytes[13]
    $height = [int]$bytes[14] + 256 * [int]$bytes[15]
    if ($bytes[2] -ne 1 -or $bytes[16] -ne 8 -or $bytes[7] -ne 32 -or
        $paletteCount -gt 17 -or $width -ne 256 -or $height -ne 240) {
        throw "TAHUD exige TGA indexed8 256x240, <=16 cores: type=$($bytes[2]) bpp=$($bytes[16]) pal=$paletteCount size=${width}x${height}"
    }
    if ($bytes[18 + [int]$bytes[0] + 3] -ne 0) { throw 'Indice zero deve ser transparente.' }
    $pixelOffset = 18 + [int]$bytes[0] + 4 * $paletteCount
    if ($bytes.Length -lt $pixelOffset + 256 * 240) { throw 'Pixels TGA truncados.' }
    for ($i = $pixelOffset; $i -lt $pixelOffset + 256 * 240; $i++) {
        if ($bytes[$i] -ge $paletteCount) { throw "Indice de paleta invalido em $i" }
    }
    $topOrigin = ($bytes[17] -band 0x20) -ne 0
    for ($y = 224; $y -lt 240; $y++) {
        $fileY = if ($topOrigin) { $y } else { 239 - $y }
        for ($x = 240; $x -lt 256; $x++) {
            if ($bytes[$pixelOffset + $fileY * 256 + $x] -ne 0) {
                throw 'Tile transparente 15,14 foi sobrescrito.'
            }
        }
    }
    Write-Output "Gerado: $target ($($bytes.Length) bytes, $paletteCount cores)"
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
