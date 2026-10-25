param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$names = @('F08764.TGA', 'F08864.TGA', 'F08964.TGA', 'F09064.TGA')
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

foreach ($name in $names) {
    $source = Join-Path $InputDir $name
    $target = Join-Path $OutputDir $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Arquivo ausente: $source" }
    if (Test-Path -LiteralPath $target) { throw "Destino ja existe: $target" }

    [byte[]]$data = [IO.File]::ReadAllBytes($source)
    $width = [int]$data[12] + 256 * [int]$data[13]
    $height = [int]$data[14] + 256 * [int]$data[15]
    $paletteCount = [int]$data[5] + 256 * [int]$data[6]
    if ($data.Length -lt 18 + 48 + 128 * 128 -or
        $data[1] -ne 1 -or $data[2] -ne 1 -or
        $data[3] -ne 0 -or $data[4] -ne 0 -or
        $paletteCount -ne 16 -or $data[7] -ne 24 -or
        $width -ne 128 -or $height -ne 128 -or $data[16] -ne 8) {
        throw "Formato TGA inesperado: $source"
    }

    $paletteOffset = 18 + [int]$data[0]
    $recolored = 0
    # Index zero is the magenta transparency key. Change palette entries only;
    # the 128x128 pixel-index map and TGA header remain byte-for-byte intact.
    for ($index = 1; $index -lt 16; $index++) {
        $offset = $paletteOffset + 3 * $index
        $b = [int]$data[$offset]
        $g = [int]$data[$offset + 1]
        $r = [int]$data[$offset + 2]
        $channels = @($r, $g, $b)
        $spread = ($channels | Measure-Object -Maximum).Maximum -
                  ($channels | Measure-Object -Minimum).Minimum
        $brightness = ($r + $g + $b) / 3
        if ($spread -gt 10 -or $brightness -lt 130) { continue }

        if ($brightness -lt 155) { $newR = 99; $newG = 126; $newB = 49 }
        elseif ($brightness -lt 190) { $newR = 125; $newG = 151; $newB = 64 }
        elseif ($brightness -lt 220) { $newR = 151; $newG = 177; $newB = 81 }
        elseif ($brightness -lt 245) { $newR = 177; $newG = 203; $newB = 104 }
        else { $newR = 199; $newG = 219; $newB = 126 }

        $data[$offset] = [byte]$newB
        $data[$offset + 1] = [byte]$newG
        $data[$offset + 2] = [byte]$newR
        $recolored++
    }

    [IO.File]::WriteAllBytes($target, $data)
    Write-Output "$name : $recolored cores claras alteradas"
}
