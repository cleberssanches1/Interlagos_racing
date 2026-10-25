param(
    [Parameter(Mandatory = $true)][string]$InputTga,
    [Parameter(Mandatory = $true)][string]$OutputTga
)

$source = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InputTga).Path)
if ($source.Length -lt 18 -or $source[0] -ne 0 -or $source[1] -ne 1 -or $source[2] -ne 1 -or $source[7] -ne 24 -or $source[16] -ne 8) {
    throw 'Expected an uncompressed, 8-bit indexed TGA with a 24-bit palette and no image ID.'
}

$paletteStart = [BitConverter]::ToUInt16($source, 3)
$paletteCount = [BitConverter]::ToUInt16($source, 5)
$width = [BitConverter]::ToUInt16($source, 12)
$height = [BitConverter]::ToUInt16($source, 14)
if ($paletteStart -ne 0 -or $paletteCount -lt 17 -or $paletteCount -ge 256 -or $width -ne 512 -or $height -ne 64) {
    throw 'Expected a 512x64 panorama with 17..255 palette entries starting at index zero.'
}

$pixelCount = $width * $height
$sourcePixels = 18 + ($paletteCount * 3)
if ($source.Length -lt ($sourcePixels + $pixelCount)) {
    throw 'TGA pixel data is truncated.'
}

$output = New-Object byte[] ($source.Length + 3)
[Array]::Copy($source, 0, $output, 0, 18)
$newPaletteCount = $paletteCount + 1
$output[5] = [byte]($newPaletteCount -band 255)
$output[6] = [byte]($newPaletteCount -shr 8)
# Palette entry zero stays black; every actual pixel uses an index from 1 upward.
[Array]::Copy($source, 18, $output, 21, $paletteCount * 3)

$targetPixels = $sourcePixels + 3
for ($i = 0; $i -lt $pixelCount; ++$i) {
    $index = [int]$source[$sourcePixels + $i]
    if ($index -ge $paletteCount) {
        throw "Pixel $i references palette index $index outside the palette."
    }
    $output[$targetPixels + $i] = [byte]($index + 1)
}

$trailingLength = $source.Length - ($sourcePixels + $pixelCount)
if ($trailingLength -gt 0) {
    [Array]::Copy($source, $sourcePixels + $pixelCount, $output, $targetPixels + $pixelCount, $trailingLength)
}

$destination = [IO.Path]::GetFullPath($OutputTga)
if ([IO.File]::Exists($destination)) {
    throw "Refusing to overwrite existing asset: $destination"
}
[IO.File]::WriteAllBytes($destination, $output)
Write-Output "Saved $destination ($width x $height, $newPaletteCount palette entries, reserved index 0)."
