param(
    [string]$JsonPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\segments_map.json",
    [string]$TextureRoot = "C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [switch]$UseLodSubfolders = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$Path) {
    New-Item -Path $Path -ItemType Directory -Force | Out-Null
}

function Resolve-AbsolutePathOrCreate([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    $base = (Get-Location).Path
    return [System.IO.Path]::GetFullPath((Join-Path $base $Path))
}

function Write-U16([System.IO.BinaryWriter]$bw, [uint16]$v) { $bw.Write($v) }
function Write-U32([System.IO.BinaryWriter]$bw, [uint32]$v) { $bw.Write($v) }

function Get-TgaInfo {
    param(
        [byte[]]$Bytes,
        [string]$PathForError = ""
    )

    if (-not $Bytes -or $Bytes.Length -lt 18) {
        return [pscustomobject]@{
            valid = $false
            reason = "short"
            colorMapType = -1
            imageType = -1
            pixelDepth = -1
            width = 0
            height = 0
        }
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        colorMapType = [int]$Bytes[1]
        imageType = [int]$Bytes[2]
        pixelDepth = [int]$Bytes[16]
        width = [int]([System.BitConverter]::ToUInt16($Bytes, 12))
        height = [int]([System.BitConverter]::ToUInt16($Bytes, 14))
    }
}

function Resolve-TexturePath {
    param(
        [string]$TextureRootPath,
        [string]$FileName,
        [int]$Lod,
        [bool]$UseSubfolders,
        [string]$OutputDir
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    # search inside output dir first
    $candidates.Add((Join-Path $OutDir $FileName)) | Out-Null
    if ($UseSubfolders) {
        $candidates.Add((Join-Path (Join-Path $TextureRootPath "$Lod") $FileName)) | Out-Null
    }
        $candidates.Add((Join-Path $TextureRootPath $FileName)) | Out-Null
    foreach ($lodDir in "64","32","16","8") {
        $candidates.Add((Join-Path (Join-Path $TextureRootPath $lodDir) $FileName)) | Out-Null
    }

    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) { return $p }
    }

    # Fallback for Blender-like material suffixes:
    # area_escape.006_64.tga -> area_escape_64.tga
    $normalized = [regex]::Replace($FileName, '\.\d{3}(?=_(8|16|32|64)\.)', '')
    if ($normalized -ne $FileName) {
        $fallback = New-Object System.Collections.Generic.List[string]
        if ($UseSubfolders) {
            $fallback.Add((Join-Path (Join-Path $TextureRootPath "$Lod") $normalized)) | Out-Null
        }
        $fallback.Add((Join-Path $TextureRootPath $normalized)) | Out-Null
        $fallback.Add((Join-Path (Join-Path $TextureRootPath "64") $normalized)) | Out-Null
        $fallback.Add((Join-Path (Join-Path $TextureRootPath "32") $normalized)) | Out-Null
        $fallback.Add((Join-Path (Join-Path $TextureRootPath "16") $normalized)) | Out-Null
        $fallback.Add((Join-Path (Join-Path $TextureRootPath "8") $normalized)) | Out-Null
        foreach ($p in $fallback) {
            if (Test-Path -LiteralPath $p) { return $p }
        }
    }

    # Fuzzy fallback:
    # Try to find any file with same LOD suffix and closest base name.
    $m = [regex]::Match($FileName, '^(?<base>.+)_(?<lod>8|16|32|64)\.(?<ext>tga|png)$', 'IgnoreCase')
    if ($m.Success) {
        $base = $m.Groups['base'].Value.ToLowerInvariant()
        $lodPart = $m.Groups['lod'].Value
        $ext = $m.Groups['ext'].Value.ToLowerInvariant()
        $searchDirs = @()
        if ($UseSubfolders) { $searchDirs += (Join-Path $TextureRootPath "$Lod") }
        $searchDirs += $TextureRootPath
        $searchDirs += (Join-Path $TextureRootPath "64")
        $searchDirs += (Join-Path $TextureRootPath "32")
        $searchDirs += (Join-Path $TextureRootPath "16")
        $searchDirs += (Join-Path $TextureRootPath "8")

        foreach ($dir in $searchDirs) {
            if (-not (Test-Path -LiteralPath $dir)) { continue }
            $files = Get-ChildItem -LiteralPath $dir -File -Filter "*_${lodPart}.${ext}" -ErrorAction SilentlyContinue
            if (-not $files) { continue }

            # 1) contains base
            $hit = $files | Where-Object { $_.BaseName.ToLowerInvariant().Contains($base) } | Select-Object -First 1
            if ($hit) { return $hit.FullName }

            # 2) starts with first token of base
            $token = ($base -split '[_\.]')[0]
            if (-not [string]::IsNullOrWhiteSpace($token)) {
                $hit = $files | Where-Object { $_.BaseName.ToLowerInvariant().StartsWith($token) } | Select-Object -First 1
                if ($hit) { return $hit.FullName }
            }
        }
    }
    return $null
}

function Build-TexBank {
    param(
        [object[]]$Families,
        [int]$Lod,
        [string]$TextureRootPath,
        [string]$OutDirectory,
        [bool]$UseSubfolders
    )

    # Preload payloads to compute offsets.
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($fam in $Families) {
        $fid = [uint32]$fam.id
        $fname = $null
        if ($fam.imageFiles -and $fam.imageFiles.PSObject.Properties.Name -contains "$Lod") {
            $fname = [string]$fam.imageFiles."$Lod"
        }
        if ([string]::IsNullOrWhiteSpace($fname)) {
            Write-Host ("AVISO: family {0} sem imageFiles[{1}], ignorando." -f $fid, $Lod)
            continue
        }
        $texPath = Resolve-TexturePath -TextureRootPath $TextureRootPath -FileName $fname -Lod $Lod -UseSubfolders:$UseSubfolders -OutputDir $OutDirectory
        if (-not $texPath) {
            Write-Host ("AVISO: textura nao encontrada family:{0} lod:{1} file:{2} procurado em {3}" -f $fid, $Lod, $fname, (Join-Path $TextureRootPath $fname))
            continue
        }

        $bytes = [System.IO.File]::ReadAllBytes($texPath)
        $tgaInfo = Get-TgaInfo -Bytes $bytes -PathForError $texPath
        if (-not $tgaInfo.valid) {
            Write-Host ("AVISO: TGA invalido family:{0} lod:{1} file:{2} motivo:{3}" -f $fid, $Lod, $fname, $tgaInfo.reason)
        }
        $entries.Add([pscustomobject]@{
            familyId = $fid
            name = [string]$fam.name
            file = [System.IO.Path]::GetFileName($texPath)
            fullPath = $texPath
            payload = $bytes
            size = [uint32]$bytes.Length
            offset = [uint32]0
            format = [uint32]0 # 0=tga/png raw file payload
            tgaColorMapType = [int]$tgaInfo.colorMapType
            tgaImageType = [int]$tgaInfo.imageType
            tgaPixelDepth = [int]$tgaInfo.pixelDepth
            tgaWidth = [int]$tgaInfo.width
            tgaHeight = [int]$tgaInfo.height
        }) | Out-Null
    }

    $count = $entries.Count
    $headerSize = 4 + 2 + 2 + 4 + 4 + 4 # magic + ver + lod + count + dataOffset + reserved
    $entrySize = 4 + 4 + 4 + 4          # familyId + offset + size + format
    $dataOffset = [uint32]($headerSize + ($entrySize * $count))

    $cursor = $dataOffset
    foreach ($e in $entries) {
        $e.offset = [uint32]$cursor
        $cursor += [uint32]$e.size
    }

    $bankPath = Join-Path $OutDirectory ("TBK{0}.BIN" -f $Lod)
    $fs = [System.IO.File]::Open($bankPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        # Header: "TBK1"
        Write-U32 $bw 0x314B4254
        Write-U16 $bw 1
        Write-U16 $bw ([uint16]$Lod)
        Write-U32 $bw ([uint32]$count)
        Write-U32 $bw ([uint32]$dataOffset)
        Write-U32 $bw 0

        foreach ($e in $entries) {
            Write-U32 $bw ([uint32]$e.familyId)
            Write-U32 $bw ([uint32]$e.offset)
            Write-U32 $bw ([uint32]$e.size)
            Write-U32 $bw ([uint32]$e.format)
        }
        foreach ($e in $entries) {
            $bw.Write([byte[]]$e.payload)
        }
        $bw.Flush()
    }
    finally {
        $fs.Close()
    }

    $indexPath = Join-Path $OutDirectory ("TBK{0}.json" -f $Lod)
    $indexObj = [pscustomobject]@{
        version = 1
        lod = $Lod
        count = $count
        bank = [System.IO.Path]::GetFileName($bankPath)
        entries = @($entries | ForEach-Object {
            [pscustomobject]@{
                familyId = $_.familyId
                name = $_.name
                file = $_.file
                offset = $_.offset
                size = $_.size
                format = $_.format
                tgaColorMapType = $_.tgaColorMapType
                tgaImageType = $_.tgaImageType
                tgaPixelDepth = $_.tgaPixelDepth
                tgaWidth = $_.tgaWidth
                tgaHeight = $_.tgaHeight
            }
        })
    }
    $indexObj | ConvertTo-Json -Depth 8 | Set-Content -Path $indexPath -Encoding UTF8

    return [pscustomobject]@{
        lod = $Lod
        bankPath = $bankPath
        indexPath = $indexPath
        count = $count
        bytes = (Get-Item -LiteralPath $bankPath).Length
    }
}

$JsonPath = Resolve-AbsolutePathOrCreate $JsonPath
$TextureRoot = Resolve-AbsolutePathOrCreate $TextureRoot
$OutDir = Resolve-AbsolutePathOrCreate $OutDir

if (-not (Test-Path -LiteralPath $JsonPath)) { throw "JsonPath nao encontrado: $JsonPath" }
if (-not (Test-Path -LiteralPath $TextureRoot)) { throw "TextureRoot nao encontrado: $TextureRoot" }
Ensure-Dir $OutDir

$j = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$families = @($j.textureFamilies)
if ($families.Count -eq 0) { throw "textureFamilies vazio em $JsonPath" }

$lods = @(8, 16, 32, 64)
$summary = New-Object System.Collections.Generic.List[object]

foreach ($lod in $lods) {
    $res = Build-TexBank -Families $families -Lod $lod -TextureRootPath $TextureRoot -OutDirectory $OutDir -UseSubfolders:$UseLodSubfolders
    $summary.Add($res) | Out-Null
    Write-Host ("OK TBK{0}.BIN entries:{1} bytes:{2}" -f $lod, $res.count, $res.bytes)
}

$compatEntries = New-Object System.Collections.Generic.List[object]
foreach ($lod in $lods) {
    $idxPath = Join-Path $OutDir ("TBK{0}.json" -f $lod)
    if (-not (Test-Path -LiteralPath $idxPath)) { continue }
    $idx = Get-Content -LiteralPath $idxPath -Raw | ConvertFrom-Json
    foreach ($e in @($idx.entries)) {
        $compatEntries.Add([pscustomobject]@{
            lod = $lod
            familyId = [int]$e.familyId
            file = [string]$e.file
            colorMapType = [int]$e.tgaColorMapType
            imageType = [int]$e.tgaImageType
            pixelDepth = [int]$e.tgaPixelDepth
            width = [int]$e.tgaWidth
            height = [int]$e.tgaHeight
        }) | Out-Null
    }
}

$compatPath = Join-Path $OutDir "tga_compat_report.json"
$compatObj = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    entries = @($compatEntries.ToArray())
}
$compatObj | ConvertTo-Json -Depth 8 | Set-Content -Path $compatPath -Encoding UTF8
Write-Host ("OK TGA compat: {0}" -f $compatPath)

$manifestPath = Join-Path $OutDir "texbanks_manifest.json"
$manifest = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    sourceJson = $JsonPath
    textureRoot = $TextureRoot
    banks = @($summary.ToArray())
    tgaCompatReport = $compatPath
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $manifestPath -Encoding UTF8
Write-Host ("OK Manifest: {0}" -f $manifestPath)
