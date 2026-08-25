param(
    [string]$JsonPath = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing\segments_map.json",
    [string]$TextureRoot = "C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA",
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [string]$ReportDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
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

function Get-ArqTgaSearchDirs {
    param(
        [string]$ResultRoot,
        [int]$Lod
    )

    $dirs = New-Object System.Collections.Generic.List[string]
    if ([string]::IsNullOrWhiteSpace($ResultRoot)) { return @($dirs.ToArray()) }

    # 3 Levels of Design:
    # - lod_0 / lod_1 ARQ_TGA => 64x64
    # - lod_2 ARQ_TGA => 32x32 (arquivos podem ainda se chamar F###64.TGA)
    switch ($Lod) {
        64 {
            $dirs.Add((Join-Path $ResultRoot "lod_0\ARQ_TGA")) | Out-Null
            $dirs.Add((Join-Path $ResultRoot "lod_1\ARQ_TGA")) | Out-Null
            $dirs.Add((Join-Path $ResultRoot "obj_64\ARQ_TGA")) | Out-Null
        }
        32 {
            $dirs.Add((Join-Path $ResultRoot "lod_2\ARQ_TGA")) | Out-Null
            $dirs.Add((Join-Path $ResultRoot "obj_32\ARQ_TGA")) | Out-Null
        }
        16 {
            $dirs.Add((Join-Path $ResultRoot "obj_16\ARQ_TGA")) | Out-Null
        }
        8 {
            $dirs.Add((Join-Path $ResultRoot "obj_8\ARQ_TGA")) | Out-Null
        }
        default {
            $dirs.Add((Join-Path $ResultRoot ("obj_{0}\ARQ_TGA" -f $Lod))) | Out-Null
        }
    }
    return @($dirs.ToArray())
}

# Cache: dirLower -> hashtable(nameLower -> fullPath)
$script:DirFileCache = @{}

function Get-DirFileIndex {
    param([string]$Dir)

    $key = $Dir.ToLowerInvariant()
    if ($script:DirFileCache.ContainsKey($key)) {
        return $script:DirFileCache[$key]
    }

    $index = @{}
    if (-not [string]::IsNullOrWhiteSpace($Dir) -and (Test-Path -LiteralPath $Dir)) {
        foreach ($f in @(Get-ChildItem -LiteralPath $Dir -File -ErrorAction SilentlyContinue)) {
            $index[$f.Name.ToLowerInvariant()] = $f.FullName
        }
    }
    $script:DirFileCache[$key] = $index
    return $index
}

function Find-FileInDirs {
    param(
        [string[]]$Dirs,
        [string]$FileName
    )

    if ([string]::IsNullOrWhiteSpace($FileName)) { return $null }
    $want = $FileName.ToLowerInvariant()
    foreach ($dir in $Dirs) {
        if ([string]::IsNullOrWhiteSpace($dir)) { continue }
        $index = Get-DirFileIndex -Dir $dir
        if ($index.ContainsKey($want)) { return [string]$index[$want] }
    }
    return $null
}

function Normalize-MaterialFamilyName([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name)) { return "" }
    $n = $Name.Trim()
    $n = [regex]::Replace($n, '_(8|16|32|64)(\.[^\\\/]+)?$', '', 'IgnoreCase')
    return $n.ToLowerInvariant()
}

function Build-MtlTextureAliasMap {
    param([string]$ResultRoot)

    $map = @{}
    if ([string]::IsNullOrWhiteSpace($ResultRoot)) { return $map }

    # lod_0 e suficiente para o de-para; lod_1/obj_64 so entram se lod_0 ausente.
    $mtlRoots = @(
        (Join-Path $ResultRoot "lod_0"),
        (Join-Path $ResultRoot "lod_1"),
        (Join-Path $ResultRoot "obj_64")
    )

    $loadedFromPreferred = $false
    foreach ($root in $mtlRoots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $mtlFiles = @(Get-ChildItem -LiteralPath $root -File -Filter *.mtl -ErrorAction SilentlyContinue)
        if ($mtlFiles.Count -le 0) { continue }
        foreach ($mtl in $mtlFiles) {
            $currentFamily = ""
            foreach ($line in [System.IO.File]::ReadLines($mtl.FullName)) {
                $t = $line.Trim()
                if ($t.StartsWith("newmtl ")) {
                    $currentFamily = Normalize-MaterialFamilyName ($t.Substring(7).Trim())
                    continue
                }
                if ([string]::IsNullOrWhiteSpace($currentFamily)) { continue }
                if ($t -notmatch '^(?i)map_Kd\s+(.+)$') { continue }

                $texName = [System.IO.Path]::GetFileName($Matches[1].Trim().Trim('"'))
                if ([string]::IsNullOrWhiteSpace($texName)) { continue }
                if (-not $map.ContainsKey($currentFamily)) {
                    $map[$currentFamily] = $texName
                }
            }
        }
        $loadedFromPreferred = $true
        # Uma pasta com MTLs ja cobre o de-para; evita reler centenas de MTLs iguais.
        if ($loadedFromPreferred) { break }
    }
    return $map
}

function Resolve-TexturePath {
    param(
        [string]$TextureRootPath,
        [string]$ResultRoot,
        [string]$FileName,
        [int]$Lod,
        [bool]$UseSubfolders,
        [string]$OutputDir,
        [hashtable]$MtlAliasMap
    )

    $searchDirs = New-Object System.Collections.Generic.List[string]
    # 1) Fonte autoritativa: ResultDir/lod_*/ARQ_TGA
    foreach ($arqDir in (Get-ArqTgaSearchDirs -ResultRoot $ResultRoot -Lod $Lod)) {
        $searchDirs.Add($arqDir) | Out-Null
    }
    # 2) Fallbacks: pacote, cd/data, TextureRoot legado
    if (-not [string]::IsNullOrWhiteSpace($ReportDir)) { $searchDirs.Add($ReportDir) | Out-Null }
    if (-not [string]::IsNullOrWhiteSpace($OutputDir)) { $searchDirs.Add($OutputDir) | Out-Null }
    if (-not [string]::IsNullOrWhiteSpace($TextureRootPath)) { $searchDirs.Add($TextureRootPath) | Out-Null }

    if ($UseSubfolders -and -not [string]::IsNullOrWhiteSpace($TextureRootPath)) {
        $searchDirs.Add((Join-Path $TextureRootPath "$Lod")) | Out-Null
        foreach ($lodDir in "64", "32", "16", "8") {
            $searchDirs.Add((Join-Path $TextureRootPath $lodDir)) | Out-Null
        }
    }

    $nameVariants = New-Object System.Collections.Generic.List[string]
    $nameVariants.Add($FileName) | Out-Null

    # Blender-like: area_escape.006_64.tga -> area_escape_64.tga
    $normalized = [regex]::Replace($FileName, '\.\d{3}(?=_(8|16|32|64)\.)', '')
    if ($normalized -ne $FileName) { $nameVariants.Add($normalized) | Out-Null }

    # De-para via MTL: familia/material -> arquivo real em ARQ_TGA (ex. F06364.TGA)
    if ($null -ne $MtlAliasMap -and $MtlAliasMap.Count -gt 0) {
        $familyFromFile = [System.IO.Path]::GetFileNameWithoutExtension($FileName)
        $familyFromFile = [regex]::Replace($familyFromFile, '_(8|16|32|64)$', '', 'IgnoreCase')
        $familyKey = Normalize-MaterialFamilyName $familyFromFile
        if (-not [string]::IsNullOrWhiteSpace($familyKey) -and $MtlAliasMap.ContainsKey($familyKey)) {
            $aliasName = [string]$MtlAliasMap[$familyKey]
            if (-not [string]::IsNullOrWhiteSpace($aliasName)) {
                $nameVariants.Add($aliasName) | Out-Null
                # Se o alias e F06364.TGA e pedimos LOD 32, tambem tente F063_32.TGA / F06332.TGA
                $aliasStem = [System.IO.Path]::GetFileNameWithoutExtension($aliasName)
                $aliasExt = [System.IO.Path]::GetExtension($aliasName)
                if ([string]::IsNullOrWhiteSpace($aliasExt)) { $aliasExt = ".TGA" }
                $aliasBase = [regex]::Replace($aliasStem, '_(8|16|32|64)$', '', 'IgnoreCase')
                $aliasBase = [regex]::Replace($aliasBase, '(8|16|32|64)$', '', 'IgnoreCase')
                if (-not [string]::IsNullOrWhiteSpace($aliasBase)) {
                    $nameVariants.Add(("{0}_{1}{2}" -f $aliasBase, $Lod, $aliasExt)) | Out-Null
                    $nameVariants.Add(("{0}{1}{2}" -f $aliasBase, $Lod, $aliasExt)) | Out-Null
                    # lod_2 mantém nome *64* no arquivo mesmo sendo 32x32
                    if ($Lod -eq 32) {
                        $nameVariants.Add(("{0}64{1}" -f $aliasBase, $aliasExt)) | Out-Null
                        $nameVariants.Add(("{0}_64{1}" -f $aliasBase, $aliasExt)) | Out-Null
                    }
                }
            }
        }
    }

    # lod_2: pedido *_32.tga pode existir como *64.tga (mesmo basename ISO)
    if ($Lod -eq 32) {
        $as64 = [regex]::Replace($FileName, '_(32)\.', '_64.', 'IgnoreCase')
        if ($as64 -ne $FileName) { $nameVariants.Add($as64) | Out-Null }
        $as64b = [regex]::Replace($FileName, '(32)\.(tga|png)$', '64.$1', 'IgnoreCase')
        if ($as64b -ne $FileName) { $nameVariants.Add($as64b) | Out-Null }
    }

    $dirsArr = @($searchDirs.ToArray() | Select-Object -Unique)
    foreach ($variant in ($nameVariants | Select-Object -Unique)) {
        $hit = Find-FileInDirs -Dirs $dirsArr -FileName $variant
        if ($hit) { return $hit }
    }

    # Sem fuzzy por token (ex. "grama_*" -> F02364/grama_lateral).
    # O de-para correto vem do MTL map_Kd ou do nome exato do arquivo em ARQ_TGA.
    return $null
}

function Build-TexBank {
    param(
        [object[]]$Families,
        [int]$Lod,
        [string]$TextureRootPath,
        [string]$ResultRoot,
        [string]$OutDirectory,
        [bool]$UseSubfolders,
        [hashtable]$MtlAliasMap
    )

    $primarySearchHint = $null
    $arqDirs = @(Get-ArqTgaSearchDirs -ResultRoot $ResultRoot -Lod $Lod)
    if ($arqDirs.Count -gt 0) { $primarySearchHint = $arqDirs[0] }
    elseif (-not [string]::IsNullOrWhiteSpace($ResultRoot)) { $primarySearchHint = $ResultRoot }
    else { $primarySearchHint = $TextureRootPath }

    # Preload payloads to compute offsets.
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($fam in $Families) {
        $fid = [uint32]$fam.id
        $fname = $null
        if ($fam.imageFiles -and $fam.imageFiles.PSObject.Properties.Name -contains "$Lod") {
            $fname = [string]$fam.imageFiles."$Lod"
        }
        if ([string]::IsNullOrWhiteSpace($fname)) {
            # Tenta de-para por nome da familia via MTL map_Kd.
            $famKey = Normalize-MaterialFamilyName ([string]$fam.name)
            if (-not [string]::IsNullOrWhiteSpace($famKey) -and $MtlAliasMap.ContainsKey($famKey)) {
                $fname = [string]$MtlAliasMap[$famKey]
            }
        }
        if ([string]::IsNullOrWhiteSpace($fname)) {
            Write-Host ("AVISO: family {0} sem imageFiles[{1}], ignorando." -f $fid, $Lod)
            continue
        }
        $texPath = Resolve-TexturePath -TextureRootPath $TextureRootPath -ResultRoot $ResultRoot -FileName $fname -Lod $Lod -UseSubfolders:$UseSubfolders -OutputDir $OutDirectory -MtlAliasMap $MtlAliasMap
        if (-not $texPath) {
            Write-Host ("AVISO: textura nao encontrada family:{0} lod:{1} file:{2} procurado em {3}" -f $fid, $Lod, $fname, (Join-Path $primarySearchHint $fname))
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

    $indexPath = Join-Path $ReportDir ("TBK{0}.json" -f $Lod)
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
$ResultDir = Resolve-AbsolutePathOrCreate $ResultDir
$OutDir = Resolve-AbsolutePathOrCreate $OutDir
$ReportDir = Resolve-AbsolutePathOrCreate $ReportDir

if (-not (Test-Path -LiteralPath $JsonPath)) { throw "JsonPath nao encontrado: $JsonPath" }
# TextureRoot legado e opcional se ResultDir/lod_*/ARQ_TGA existir.
$hasResultTextures = $false
foreach ($lodProbe in @(64, 32)) {
    foreach ($d in (Get-ArqTgaSearchDirs -ResultRoot $ResultDir -Lod $lodProbe)) {
        if (Test-Path -LiteralPath $d) { $hasResultTextures = $true; break }
    }
    if ($hasResultTextures) { break }
}
if (-not (Test-Path -LiteralPath $TextureRoot) -and -not $hasResultTextures) {
    throw "TextureRoot/ResultDir ARQ_TGA nao encontrados. TextureRoot=$TextureRoot ResultDir=$ResultDir"
}
Ensure-Dir $OutDir
Ensure-Dir $ReportDir

$j = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$families = @($j.textureFamilies)
if ($families.Count -eq 0) { throw "textureFamilies vazio em $JsonPath" }

Write-Host ("Texture search: ResultDir={0}" -f $ResultDir)
Write-Host ("  LOD64 dirs: {0}" -f ((Get-ArqTgaSearchDirs -ResultRoot $ResultDir -Lod 64) -join "; "))
Write-Host ("  LOD32 dirs: {0}" -f ((Get-ArqTgaSearchDirs -ResultRoot $ResultDir -Lod 32) -join "; "))

$mtlAliasMap = Build-MtlTextureAliasMap -ResultRoot $ResultDir
Write-Host ("MTL de-para (material -> TGA): {0} entradas" -f $mtlAliasMap.Count)

$lods = @(8, 16, 32, 64)
$summary = New-Object System.Collections.Generic.List[object]

foreach ($lod in $lods) {
    $res = Build-TexBank -Families $families -Lod $lod -TextureRootPath $TextureRoot -ResultRoot $ResultDir -OutDirectory $OutDir -UseSubfolders:$UseLodSubfolders -MtlAliasMap $mtlAliasMap
    $summary.Add($res) | Out-Null
    Write-Host ("OK TBK{0}.BIN entries:{1} bytes:{2}" -f $lod, $res.count, $res.bytes)
}

$compatEntries = New-Object System.Collections.Generic.List[object]
foreach ($lod in $lods) {
    $idxPath = Join-Path $ReportDir ("TBK{0}.json" -f $lod)
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

$compatPath = Join-Path $ReportDir "tga_compat_report.json"
$compatObj = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    entries = @($compatEntries.ToArray())
}
$compatObj | ConvertTo-Json -Depth 8 | Set-Content -Path $compatPath -Encoding UTF8
Write-Host ("OK TGA compat: {0}" -f $compatPath)

$manifestPath = Join-Path $ReportDir "texbanks_manifest.json"
$manifest = [pscustomobject]@{
    version = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    sourceJson = $JsonPath
    textureRoot = $TextureRoot
    resultDir = $ResultDir
    mtlAliasCount = [int]$mtlAliasMap.Count
    banks = @($summary.ToArray())
    tgaCompatReport = $compatPath
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $manifestPath -Encoding UTF8
Write-Host ("OK Manifest: {0}" -f $manifestPath)
