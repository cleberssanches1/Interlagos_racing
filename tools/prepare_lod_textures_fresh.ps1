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
foreach ($family in @($json.textureFamilies | Sort-Object { [int]$_.id })) {
    if ($null -eq $family.sourceFiles) { throw "Family $($family.id) sem sourceFiles." }
    $source0 = Join-Path $roots.lod_0 ([string]$family.sourceFiles.lod_0)
    $source1 = Join-Path $roots.lod_1 ([string]$family.sourceFiles.lod_1)
    $source2 = Join-Path $roots.lod_2 ([string]$family.sourceFiles.lod_2)
    foreach ($source in @($source0, $source1, $source2)) {
        if (-not (Test-Path -LiteralPath $source)) { throw "Fonte TGA ausente: $source" }
    }
    $hash0 = (Get-FileHash -LiteralPath $source0 -Algorithm SHA256).Hash
    $hash1 = (Get-FileHash -LiteralPath $source1 -Algorithm SHA256).Hash
    if ($hash0 -ne $hash1) { throw "Conteudo lod_0/lod_1 divergente para family $($family.id)." }

    $target64 = Join-Path $OutDir ([string]$family.imageFiles."64")
    $target32 = Join-Path $OutDir ([string]$family.imageFiles."32")
    $info0 = Get-TgaInfo $source0
    $info2 = Get-TgaInfo $source2

    $transform64 = "copy"
    if ($info0.width -eq 32 -and $info0.height -eq 32) {
        Invoke-PalettedResize $source0 $target64 64 64
        $transform64 = "nearest_32x32_to_64x64"
    }
    else {
        Copy-Item -LiteralPath $source0 -Destination $target64 -Force
    }

    $target32Width = $info2.width
    $target32Height = $info2.height
    $transform32 = "copy"
    $maxDim = [Math]::Max($info2.width, $info2.height)
    if ($maxDim -gt 32) {
        $scale = 32.0 / [double]$maxDim
        $target32Width = [Math]::Max(8, [int][Math]::Round(($info2.width * $scale) / 8.0) * 8)
        $target32Height = [Math]::Max(1, [int][Math]::Round($info2.height * $scale))
        Invoke-PalettedResize $source2 $target32 $target32Width $target32Height
        $transform32 = "nearest_fit_32"
    }
    else {
        Copy-Item -LiteralPath $source2 -Destination $target32 -Force
    }

    foreach ($spec in @(
        [pscustomobject]@{ lod = 64; sourceGroup = "lod_0"; source = $source0; destination = $target64; transform = $transform64 },
        [pscustomobject]@{ lod = 32; sourceGroup = "lod_2"; source = $source2; destination = $target32; transform = $transform32 }
    )) {
        $sourceInfo = Get-TgaInfo $spec.source
        $outInfo = Get-TgaInfo $spec.destination
        $entries.Add([pscustomobject]([ordered]@{
            familyId = [int]$family.id
            family = [string]$family.name
            lod = [int]$spec.lod
            sourceGroup = [string]$spec.sourceGroup
            sourcePath = [System.IO.Path]::GetFullPath([string]$spec.source)
            sourceSha256 = (Get-FileHash -LiteralPath $spec.source -Algorithm SHA256).Hash
            sourceWidth = [int]$sourceInfo.width
            sourceHeight = [int]$sourceInfo.height
            targetPath = [System.IO.Path]::GetFullPath([string]$spec.destination)
            targetSha256 = (Get-FileHash -LiteralPath $spec.destination -Algorithm SHA256).Hash
            width = [int]$outInfo.width
            height = [int]$outInfo.height
            transform = [string]$spec.transform
        })) | Out-Null
    }
}

$manifest = [pscustomobject]([ordered]@{
    version = 2
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    allowedRoots = @($roots.Values)
    entries = @($entries.ToArray())
})
$manifestPath = Join-Path $OutDir "texture_sources_manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host ("Fresh LOD textures: families={0} outputs={1}" -f @($json.textureFamilies).Count, $entries.Count)
Write-Host ("Manifest: {0}" -f $manifestPath)
