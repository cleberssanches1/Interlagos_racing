param(
    [Parameter(Mandatory = $true)]
    [string]$JsonPath,
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [Parameter(Mandatory = $true)]
    [string]$OutDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-TgaInfo([string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 18) { throw "TGA invalido/truncado: $Path" }
    $colorMapLength = [int][System.BitConverter]::ToUInt16($bytes, 5)
    $colorMapEntryBits = [int]$bytes[7]
    $colorMapEntryBytes = [int][Math]::Ceiling($colorMapEntryBits / 8.0)
    return [pscustomobject]@{
        idLength = [int]$bytes[0]
        colorMapType = [int]$bytes[1]
        imageType = [int]$bytes[2]
        colorMapFirstIndex = [int][System.BitConverter]::ToUInt16($bytes, 3)
        colorMapLength = $colorMapLength
        colorMapEntryBits = $colorMapEntryBits
        pixelDataOffset = 18 + [int]$bytes[0] + ($colorMapLength * $colorMapEntryBytes)
        width = [int][System.BitConverter]::ToUInt16($bytes, 12)
        height = [int][System.BitConverter]::ToUInt16($bytes, 14)
        pixelDepth = [int]$bytes[16]
        descriptor = [int]$bytes[17]
    }
}

function Invoke-PalettedResize([string]$Source, [string]$Destination, [int]$Width, [int]$Height) {
    # Preserve the source palette verbatim.  VDP1 transparency is keyed by
    # pixel index 0, so sending an indexed TGA through a general-purpose image
    # quantizer is unsafe: ImageMagick is allowed to reorder the colormap and
    # used to move the magenta key away from entry 0.
    [byte[]]$sourceBytes = [System.IO.File]::ReadAllBytes($Source)
    $sourceInfo = Get-TgaInfo $Source
    if ($sourceInfo.colorMapType -ne 1 -or
        $sourceInfo.imageType -ne 1 -or
        $sourceInfo.pixelDepth -ne 8 -or
        $sourceInfo.colorMapFirstIndex -ne 0 -or
        $sourceInfo.colorMapLength -le 0 -or
        $sourceInfo.colorMapEntryBits -notin @(24, 32)) {
        throw "TGA fonte incompativel com resize indexado seguro: $Source"
    }
    if ($Width -le 0 -or $Height -le 0) {
        throw "Dimensao de destino invalida: ${Width}x${Height}"
    }

    $sourcePixelCount = $sourceInfo.width * $sourceInfo.height
    if (($sourceInfo.pixelDataOffset + $sourcePixelCount) -gt $sourceBytes.Length) {
        throw "TGA fonte truncado nos pixels: $Source"
    }

    $targetPixelCount = $Width * $Height
    [byte[]]$targetBytes = New-Object byte[] ($sourceInfo.pixelDataOffset + $targetPixelCount)
    [System.Array]::Copy($sourceBytes, 0, $targetBytes, 0, $sourceInfo.pixelDataOffset)
    $targetBytes[12] = [byte]($Width -band 0xFF)
    $targetBytes[13] = [byte](($Width -shr 8) -band 0xFF)
    $targetBytes[14] = [byte]($Height -band 0xFF)
    $targetBytes[15] = [byte](($Height -shr 8) -band 0xFF)

    for ($y = 0; $y -lt $Height; ++$y) {
        $sourceY = [Math]::Min(
            $sourceInfo.height - 1,
            [int][Math]::Floor((($y + 0.5) * $sourceInfo.height) / [double]$Height))
        for ($x = 0; $x -lt $Width; ++$x) {
            $sourceX = [Math]::Min(
                $sourceInfo.width - 1,
                [int][Math]::Floor((($x + 0.5) * $sourceInfo.width) / [double]$Width))
            $sourceOffset = $sourceInfo.pixelDataOffset + ($sourceY * $sourceInfo.width) + $sourceX
            $targetOffset = $sourceInfo.pixelDataOffset + ($y * $Width) + $x
            $targetBytes[$targetOffset] = $sourceBytes[$sourceOffset]
        }
    }

    [System.IO.File]::WriteAllBytes($Destination, $targetBytes)
    $info = Get-TgaInfo $Destination
    if ($info.colorMapType -ne 1 -or $info.imageType -ne 1 -or $info.pixelDepth -ne 8) {
        throw "TGA derivado incompativel com Saturn (esperado paletted type1/8bpp): $Destination"
    }
    $sourcePaletteBytes = $sourceInfo.colorMapLength * [int][Math]::Ceiling($sourceInfo.colorMapEntryBits / 8.0)
    for ($i = 0; $i -lt $sourcePaletteBytes; ++$i) {
        $paletteOffset = 18 + $sourceInfo.idLength + $i
        if ($targetBytes[$paletteOffset] -ne $sourceBytes[$paletteOffset]) {
            throw "Resize alterou a paleta (indice 0/transparencia nao preservado): $Destination"
        }
    }
}

function Invoke-PalettedUvUnwrap([string]$Source, [string]$Destination, [object[]]$UvCoords, [int]$Width, [int]$Height) {
    # A distorted sprite do VDP1 always consumes um retangulo completo. Sample
    # o quadrilatero UV do OBJ para uma textura retangular por face, seguindo a
    # mesma interpolacao usada por ModelConverter/Texture.GetUnwrap.
    if ($UvCoords.Count -ne 4) { throw "UV unwrap exige exatamente quatro cantos: $Destination" }
    [byte[]]$sourceBytes = [System.IO.File]::ReadAllBytes($Source)
    $sourceInfo = Get-TgaInfo $Source
    if ($sourceInfo.colorMapType -ne 1 -or
        $sourceInfo.imageType -ne 1 -or
        $sourceInfo.pixelDepth -ne 8 -or
        $sourceInfo.colorMapFirstIndex -ne 0 -or
        $sourceInfo.colorMapLength -le 0 -or
        $sourceInfo.colorMapEntryBits -notin @(24, 32)) {
        throw "TGA fonte incompativel com UV unwrap indexado seguro: $Source"
    }
    if ($Width -le 0 -or $Height -le 0 -or ($Width % 8) -ne 0) {
        throw "Dimensao de UV unwrap invalida para VDP1: ${Width}x${Height}"
    }
    if (($sourceInfo.pixelDataOffset + ($sourceInfo.width * $sourceInfo.height)) -gt $sourceBytes.Length) {
        throw "TGA fonte truncado nos pixels: $Source"
    }

    [byte[]]$targetBytes = New-Object byte[] ($sourceInfo.pixelDataOffset + ($Width * $Height))
    [System.Array]::Copy($sourceBytes, 0, $targetBytes, 0, $sourceInfo.pixelDataOffset)
    $targetBytes[12] = [byte]($Width -band 0xFF)
    $targetBytes[13] = [byte](($Width -shr 8) -band 0xFF)
    $targetBytes[14] = [byte]($Height -band 0xFF)
    $targetBytes[15] = [byte](($Height -shr 8) -band 0xFF)

    $sourceTopOrigin = (($sourceInfo.descriptor -band 0x20) -ne 0)
    $targetTopOrigin = $sourceTopOrigin
    for ($targetTopY = 0; $targetTopY -lt $Height; $targetTopY++) {
        # Converter percorre y do fundo para o topo; em coordenada logica
        # top-down isso equivale ao complemento abaixo.
        [double]$portionY = 1.0 - (($targetTopY + 0.5) / [double]$Height)
        for ($x = 0; $x -lt $Width; $x++) {
            [double]$portionX = ($x + 0.5) / [double]$Width

            [double]$topU = ([double]$UvCoords[0].u) + ((([double]$UvCoords[1].u) - ([double]$UvCoords[0].u)) * $portionX)
            [double]$topV = ([double]$UvCoords[0].v) + ((([double]$UvCoords[1].v) - ([double]$UvCoords[0].v)) * $portionX)
            [double]$bottomU = ([double]$UvCoords[3].u) + ((([double]$UvCoords[2].u) - ([double]$UvCoords[3].u)) * $portionX)
            [double]$bottomV = ([double]$UvCoords[3].v) + ((([double]$UvCoords[2].v) - ([double]$UvCoords[3].v)) * $portionX)
            [double]$sampleU = $bottomU + (($topU - $bottomU) * $portionY)
            [double]$sampleV = $bottomV + (($topV - $bottomV) * $portionY)

            [int]$sourceX = [int][Math]::Truncate($sampleU * ($sourceInfo.width - 1))
            [int]$sourceTopY = ($sourceInfo.height - 1) - [int][Math]::Truncate($sampleV * ($sourceInfo.height - 1))
            if ($sourceX -ge $sourceInfo.width) { $sourceX %= $sourceInfo.width }
            elseif ($sourceX -lt 0) { $sourceX = $sourceInfo.width - ([Math]::Abs($sourceX + 1) % $sourceInfo.width) - 1 }
            if ($sourceTopY -ge $sourceInfo.height) { $sourceTopY %= $sourceInfo.height }
            elseif ($sourceTopY -lt 0) { $sourceTopY = $sourceInfo.height - ([Math]::Abs($sourceTopY + 1) % $sourceInfo.height) - 1 }

            $sourceFileY = if ($sourceTopOrigin) { $sourceTopY } else { $sourceInfo.height - 1 - $sourceTopY }
            $targetFileY = if ($targetTopOrigin) { $targetTopY } else { $Height - 1 - $targetTopY }
            $sourceOffset = $sourceInfo.pixelDataOffset + ($sourceFileY * $sourceInfo.width) + $sourceX
            $targetOffset = $sourceInfo.pixelDataOffset + ($targetFileY * $Width) + $x
            $targetBytes[$targetOffset] = $sourceBytes[$sourceOffset]
        }
    }

    [System.IO.File]::WriteAllBytes($Destination, $targetBytes)
    $outInfo = Get-TgaInfo $Destination
    if ($outInfo.width -ne $Width -or $outInfo.height -ne $Height) {
        throw "UV unwrap gerou dimensao inesperada: $Destination"
    }
    $paletteBytes = $sourceInfo.colorMapLength * [int][Math]::Ceiling($sourceInfo.colorMapEntryBits / 8.0)
    for ($i = 0; $i -lt $paletteBytes; $i++) {
        $paletteOffset = 18 + $sourceInfo.idLength + $i
        if ($targetBytes[$paletteOffset] -ne $sourceBytes[$paletteOffset]) {
            throw "UV unwrap alterou a paleta/indice transparente 0: $Destination"
        }
    }
}

if (-not (Test-Path -LiteralPath $JsonPath)) { throw "JsonPath ausente: $JsonPath" }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$roots = [ordered]@{
    lod_0 = (Join-Path $ResultDir "lod_0\ARQ_TGA")
    lod_1 = (Join-Path $ResultDir "lod_1\ARQ_TGA")
    lod_2 = (Join-Path $ResultDir "lod_2\ARQ_TGA")
}
foreach ($root in $roots.Values) {
    if (-not (Test-Path -LiteralPath $root)) { throw "Raiz TGA autorizada ausente: $root" }
}

$json = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$entries = New-Object System.Collections.Generic.List[object]
$bankSpecs = @(
    [pscustomobject]@{ sourceGroup = "lod_0"; bankId = 0; runtimeIndex = 3; nominalTextureSize = 64 },
    [pscustomobject]@{ sourceGroup = "lod_1"; bankId = 1; runtimeIndex = 1; nominalTextureSize = 64 },
    [pscustomobject]@{ sourceGroup = "lod_2"; bankId = 2; runtimeIndex = 2; nominalTextureSize = 32 }
)
foreach ($spec in $bankSpecs) {
    New-Item -ItemType Directory -Path (Join-Path $OutDir $spec.sourceGroup) -Force | Out-Null
}

foreach ($family in @($json.textureFamilies | Sort-Object { [int]$_.id })) {
    if ($null -eq $family.sourceFiles) { throw "Family $($family.id) sem sourceFiles." }
    $hasUvUnwrap = ($family.PSObject.Properties.Name -contains "uvUnwrap" -and $null -ne $family.uvUnwrap)

    foreach ($spec in $bankSpecs) {
        $sourceName = [string]$family.sourceFiles.PSObject.Properties[$spec.sourceGroup].Value
        $source = Join-Path $roots[$spec.sourceGroup] $sourceName
        if (-not (Test-Path -LiteralPath $source)) { throw "Fonte TGA ausente: $source" }

        if ($family.PSObject.Properties.Name -contains "bankFiles") {
            $targetName = [string]$family.bankFiles.PSObject.Properties[$spec.sourceGroup].Value
        }
        else {
            $targetName = [string]$family.imageFiles.PSObject.Properties["$($spec.nominalTextureSize)"].Value
        }
        if ([string]::IsNullOrWhiteSpace($targetName)) {
            throw "Family $($family.id) sem bankFiles[$($spec.sourceGroup)]."
        }
        $destination = Join-Path (Join-Path $OutDir $spec.sourceGroup) $targetName
        $sourceInfo = Get-TgaInfo $source
        $targetSize = [int]$spec.nominalTextureSize
        $transform = "copy_preserve_source"
        if ($hasUvUnwrap) {
            # Unwrap direto no tamanho nominal do banco (64 ou 32) para o VDP1
            # sempre receber tile completo no LOD esperado.
            $uv = @($family.uvUnwrap.uvByLod.PSObject.Properties[$spec.sourceGroup].Value)
            Invoke-PalettedUvUnwrap $source $destination $uv $targetSize $targetSize
            $transform = ("uv_face_unwrap_{0}_to_{1}" -f $spec.sourceGroup, $targetSize)
        }
        elseif ($sourceInfo.width -eq $targetSize -and $sourceInfo.height -eq $targetSize) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
            $transform = "copy_preserve_source"
        }
        else {
            # Upsample/downsample nearest preservando paleta (indice 0 = transparencia).
            Invoke-PalettedResize $source $destination $targetSize $targetSize
            $transform = ("paletted_resize_{0}x{1}_to_{2}x{2}" -f $sourceInfo.width, $sourceInfo.height, $targetSize)
        }

        $outInfo = Get-TgaInfo $destination
        if ($outInfo.width -ne $targetSize -or $outInfo.height -ne $targetSize) {
            throw ("Textura preparada fora do tamanho nominal {0}x{0}: family {1} got {2}x{3} ({4})" -f
                $targetSize, $family.id, $outInfo.width, $outInfo.height, $destination)
        }
        $entries.Add([pscustomobject]([ordered]@{
            familyId = [int]$family.id
            family = [string]$family.name
            bankId = [int]$spec.bankId
            runtimeIndex = [int]$spec.runtimeIndex
            nominalTextureSize = [int]$spec.nominalTextureSize
            sourceGroup = [string]$spec.sourceGroup
            sourcePath = [System.IO.Path]::GetFullPath($source)
            sourceSha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            sourceWidth = [int]$sourceInfo.width
            sourceHeight = [int]$sourceInfo.height
            targetPath = [System.IO.Path]::GetFullPath($destination)
            targetSha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            width = [int]$outInfo.width
            height = [int]$outInfo.height
            transform = $transform
        })) | Out-Null
    }
}

$manifest = [pscustomobject]([ordered]@{
    version = 3
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    allowedRoots = @($roots.Values)
    entries = @($entries.ToArray())
})
$manifestPath = Join-Path $OutDir "texture_sources_manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host ("Fresh LOD textures: families={0} outputs={1}" -f @($json.textureFamilies).Count, $entries.Count)
Write-Host ("Manifest: {0}" -f $manifestPath)
