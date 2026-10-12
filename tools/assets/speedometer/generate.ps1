param(
    [string]$MagickPath = "magick"
)

$ErrorActionPreference = "Stop"
$sourceDirectory = $PSScriptRoot
$outputDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\cd\data"))
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    throw "Diretorio de saida nao existe: $outputDirectory"
}

$magick = Get-Command $MagickPath -ErrorAction SilentlyContinue
if (-not $magick) {
    throw "ImageMagick nao encontrado. Informe -MagickPath com o caminho do magick.exe."
}

Get-ChildItem -LiteralPath $sourceDirectory -Filter "SPD_*.svg" | ForEach-Object {
    $target = Join-Path $outputDirectory ($_.BaseName + ".TGA")
    & $magick.Source -background none $_.FullName -alpha on -depth 8 -define tga:bits-per-pixel=32 -compress none $target
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao gerar $target"
    }
}

# Build one 256x240 image atlas so the runtime can load a single NBG tileset.
# 240px height leaves one transparent 16px row for Bmp2Tile's empty character.
$atlasPath = Join-Path $outputDirectory "SPD_ATLAS.TGA"
$runtimeAtlasPath = Join-Path $outputDirectory "SPDHUD.TGA"
$tempAtlas = Join-Path ([System.IO.Path]::GetTempPath()) ("spd-atlas-" + [guid]::NewGuid().ToString("N") + ".png")
$runtimeParts = Join-Path ([System.IO.Path]::GetTempPath()) ("spd-runtime-" + [guid]::NewGuid().ToString("N"))
try {
    & $magick.Source -size 256x240 xc:none -type TrueColorAlpha $tempAtlas
    if ($LASTEXITCODE -ne 0) { throw "Falha criando tela temporaria para o atlas." }

    $layers = @(
        @{ Name = "SPD_PANEL.TGA"; X = 0; Y = 0 },
        @{ Name = "SPD_GEARS.TGA"; X = 0; Y = 96 },
        @{ Name = "SPD_DIGITS.TGA"; X = 128; Y = 96 },
        @{ Name = "SPD_UNITS.TGA"; X = 128; Y = 160 }
    )

    foreach ($layer in $layers) {
        $next = $tempAtlas + ".next.png"
        $source = Join-Path $outputDirectory $layer.Name
        & $magick.Source -background none $tempAtlas $source -geometry ("+{0}+{1}" -f $layer.X, $layer.Y) -compose Over -composite -type TrueColorAlpha $next
        if ($LASTEXITCODE -ne 0) { throw "Falha compondo $($layer.Name) no atlas." }
        Move-Item -LiteralPath $next -Destination $tempAtlas -Force
    }

    & $magick.Source -background none $tempAtlas -alpha on -type TrueColorAlpha -depth 8 -define tga:bits-per-pixel=32 -compress none $atlasPath
    if ($LASTEXITCODE -ne 0) { throw "Falha gerando $atlasPath" }

    # The Saturn NBG2 accepts indexed color, not the review atlas's RGB555.
    # Bake each live tile over the exact panel cell that it replaces: transparent
    # glyph pixels must preserve the static dial underneath when applicable.
    New-Item -ItemType Directory -Path $runtimeParts | Out-Null
    $panel = Join-Path $outputDirectory "SPD_PANEL.TGA"
    $gear = Join-Path $outputDirectory "SPD_GEARS.TGA"
    $digits = Join-Path $outputDirectory "SPD_DIGITS.TGA"
    $rpm = Join-Path $outputDirectory "SPD_RPM_SEGMENTS.TGA"
    $runtimeAtlas = Join-Path $runtimeParts "atlas.png"
    & $magick.Source -background none -size 256x240 xc:none $panel -geometry +0+0 -compose Over -composite $runtimeAtlas
    if ($LASTEXITCODE -ne 0) { throw "Falha iniciando atlas runtime." }

    function Add-ComposedTile {
        param([string]$Source, [int]$SourceX, [int]$SourceY,
              [int]$PanelX, [int]$PanelY, [int]$Width, [int]$Height,
              [int]$AtlasX, [int]$AtlasY)
        $base = Join-Path $runtimeParts "base.png"
        $art = Join-Path $runtimeParts "art.png"
        $tile = Join-Path $runtimeParts "tile.png"
        $next = Join-Path $runtimeParts "next.png"
        & $magick.Source -background none $panel -crop ("{0}x{1}+{2}+{3}" -f $Width,$Height,$PanelX,$PanelY) +repage -define png:color-type=6 $base
        if ($LASTEXITCODE -ne 0) { throw "Falha recortando painel." }
        & $magick.Source -background none $Source -crop ("{0}x{1}+{2}+{3}" -f $Width,$Height,$SourceX,$SourceY) +repage -define png:color-type=6 $art
        if ($LASTEXITCODE -ne 0) { throw "Falha recortando sprite." }
        & $magick.Source -background none $base $art -compose Over -composite -define png:color-type=6 $tile
        if ($LASTEXITCODE -ne 0) { throw "Falha compondo sprite." }
        & $magick.Source -background none $runtimeAtlas $tile -geometry ("+{0}+{1}" -f $AtlasX,$AtlasY) -compose Over -composite -define png:color-type=6 $next
        if ($LASTEXITCODE -ne 0) { throw "Falha atualizando atlas runtime." }
        Move-Item -LiteralPath $next -Destination $runtimeAtlas -Force
    }

    for ($i = 0; $i -lt 12; $i++) {
        $col = $i % 6; $row = [int][Math]::Floor($i / 6)
        Add-ComposedTile $gear ($col * 16) ($row * 16) 64 0 16 16 ($i * 16) 80
    }
    for ($i = 0; $i -lt 10; $i++) {
        $col = $i % 5; $row = [int][Math]::Floor($i / 5)
        Add-ComposedTile $digits ($col * 16) ($row * 16) 16 64 16 16 ($i * 16) 96
    }
    # Ten arc cells x twelve precomposed states fit in atlas rows 7..14.
    # Clipping the lit curve at each threshold preserves intermediate RPM
    # steps even when several thresholds lie inside one 16px map cell.
    $rpmCells = @(
        @(0,4), @(0,3), @(0,2), @(1,3), @(1,2),
        @(2,2), @(2,1), @(3,1), @(3,2), @(4,2)
    )
    $stageCutoffs = @(7,13,19,25,31,37,43,49,55,61,67,80)
    for ($stageIndex = 0; $stageIndex -lt 12; $stageIndex++) {
        $partial = Join-Path $runtimeParts 'rpm-partial.png'
        $stage = Join-Path $runtimeParts 'rpm-stage.png'
        & $magick.Source -background none $rpm -crop ("{0}x80+0+0" -f $stageCutoffs[$stageIndex]) +repage -define png:color-type=6 $partial
        if ($LASTEXITCODE -ne 0) { throw 'Falha recortando nivel de RPM.' }
        & $magick.Source -size 80x80 xc:none $partial -geometry +0+0 -compose Over -composite -define png:color-type=6 $stage
        if ($LASTEXITCODE -ne 0) { throw 'Falha compondo nivel de RPM.' }
        for ($cellIndex = 0; $cellIndex -lt $rpmCells.Count; $cellIndex++) {
            $cellX = $rpmCells[$cellIndex][0]; $cellY = $rpmCells[$cellIndex][1]
            $variant = $stageIndex * $rpmCells.Count + $cellIndex
            $atlasX = ($variant % 16) * 16
            $atlasY = (7 + [int][Math]::Floor($variant / 16)) * 16
            Add-ComposedTile $stage ($cellX * 16) ($cellY * 16) ($cellX * 16) ($cellY * 16) 16 16 $atlasX $atlasY
        }
    }

    & $magick.Source -background none $runtimeAtlas -alpha on -colors 16 -type PaletteAlpha -depth 8 -compress none $runtimeAtlasPath
    if ($LASTEXITCODE -ne 0) { throw "Falha gerando $runtimeAtlasPath" }
    $bytes = [System.IO.File]::ReadAllBytes($runtimeAtlasPath)
    $paletteCount = [int]$bytes[6] * 256 + [int]$bytes[5]
    if ($bytes[2] -ne 1 -or $bytes[16] -ne 8 -or $paletteCount -lt 2 -or $paletteCount -gt 17) {
        throw "Atlas runtime exige TGA palettized8 com no maximo 16 cores uteis; tipo=$($bytes[2]) profundidade=$($bytes[16]) entradas=$paletteCount"
    }
    $paletteDepth = [int]$bytes[7] / 8
    if ($paletteDepth -ne 4) {
        throw "Atlas runtime exige entradas BGRA de 32 bits."
    }
    if ($bytes[21 + [int]$bytes[0]] -ne 0) {
        throw "Atlas runtime exige indice 0 transparente."
    }
    $opaqueColors = 0
    for ($i = 0; $i -lt $paletteCount; $i++) {
        if ($bytes[18 + [int]$bytes[0] + ($i * 4) + 3] -ne 0) { $opaqueColors++ }
    }
    if ($opaqueColors -gt 15) {
        throw "Atlas runtime excede as 15 cores visiveis do VDP2 4bpp: $opaqueColors"
    }
}
finally {
    if (Test-Path -LiteralPath $tempAtlas) { Remove-Item -LiteralPath $tempAtlas -Force }
    $tempNext = $tempAtlas + ".next.png"
    if (Test-Path -LiteralPath $tempNext) { Remove-Item -LiteralPath $tempNext -Force }
    if (Test-Path -LiteralPath $runtimeParts) { Remove-Item -LiteralPath $runtimeParts -Recurse -Force }
}

Get-ChildItem -LiteralPath $outputDirectory -Filter "SPD_*.TGA" | ForEach-Object { Write-Output $_.FullName }
Write-Output $runtimeAtlasPath
