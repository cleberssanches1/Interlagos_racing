param(
    [Parameter(Mandatory = $true)]
    [string]$JsonPath,
    [Parameter(Mandatory = $true)]
    [string]$PreparedTextureDir,
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    [Parameter(Mandatory = $true)]
    [string]$ReportDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-U16([System.IO.BinaryWriter]$Writer, [uint16]$Value) { $Writer.Write($Value) }
function Write-U32([System.IO.BinaryWriter]$Writer, [uint32]$Value) { $Writer.Write($Value) }

function Get-TgaInfo([byte[]]$Bytes, [string]$Path) {
    if ($null -eq $Bytes -or $Bytes.Length -lt 18) { throw "TGA truncado: $Path" }
    $info = [pscustomobject]@{
        colorMapType = [int]$Bytes[1]
        imageType = [int]$Bytes[2]
        width = [int][System.BitConverter]::ToUInt16($Bytes, 12)
        height = [int][System.BitConverter]::ToUInt16($Bytes, 14)
        pixelDepth = [int]$Bytes[16]
    }
    if ($info.colorMapType -ne 1 -or $info.imageType -ne 1 -or $info.pixelDepth -ne 8) {
        throw ("TGA incompativel family payload: {0} cmap={1} type={2} depth={3}" -f
            $Path, $info.colorMapType, $info.imageType, $info.pixelDepth)
    }
    return $info
}

if (-not (Test-Path -LiteralPath $JsonPath)) { throw "JsonPath ausente: $JsonPath" }
if (-not (Test-Path -LiteralPath $PreparedTextureDir)) { throw "PreparedTextureDir ausente: $PreparedTextureDir" }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null

$sourceManifestPath = Join-Path $PreparedTextureDir "texture_sources_manifest.json"
if (-not (Test-Path -LiteralPath $sourceManifestPath)) { throw "Manifesto de origem ausente: $sourceManifestPath" }
$sourceManifest = Get-Content -LiteralPath $sourceManifestPath -Raw | ConvertFrom-Json
$allowedSuffixes = @(
    '\lod_0\ARQ_TGA',
    '\lod_1\ARQ_TGA',
    '\lod_2\ARQ_TGA'
)
foreach ($entry in @($sourceManifest.entries)) {
    $parent = [System.IO.Path]::GetDirectoryName([string]$entry.sourcePath).TrimEnd('\')
    $allowed = $false
    foreach ($suffix in $allowedSuffixes) {
        if ($parent.EndsWith($suffix, [System.StringComparison]::OrdinalIgnoreCase)) { $allowed = $true; break }
    }
    if (-not $allowed) { throw "Origem fora da whitelist: $($entry.sourcePath)" }
    if ([int]$entry.lod -eq 64 -and [string]$entry.sourceGroup -ne "lod_0") {
        throw "Entrada TBK64 fora do agrupamento lod_0: family $($entry.familyId)"
    }
    if ([int]$entry.lod -eq 32 -and [string]$entry.sourceGroup -ne "lod_2") {
        throw "Entrada TBK32 fora do agrupamento lod_2: family $($entry.familyId)"
    }
}

$json = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$families = @($json.textureFamilies | Sort-Object { [int]$_.id })
if ($families.Count -eq 0) { throw "textureFamilies vazio." }

$bankSummaries = New-Object System.Collections.Generic.List[object]
$compatEntries = New-Object System.Collections.Generic.List[object]
foreach ($lod in @(32, 64)) {
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($family in $families) {
        if ($null -eq $family.imageFiles -or -not ($family.imageFiles.PSObject.Properties.Name -contains "$lod")) {
            throw "Family $($family.id) sem imageFiles[$lod]."
        }
        $fileName = [string]$family.imageFiles."$lod"
        $path = Join-Path $PreparedTextureDir $fileName
        if (-not (Test-Path -LiteralPath $path)) { throw "Textura preparada ausente: $path" }
        [byte[]]$bytes = [System.IO.File]::ReadAllBytes($path)
        $info = Get-TgaInfo $bytes $path
        $sourceEntry = @($sourceManifest.entries | Where-Object {
            [int]$_.familyId -eq [int]$family.id -and [int]$_.lod -eq $lod
        }) | Select-Object -First 1
        if ($null -eq $sourceEntry) { throw "Manifesto sem family $($family.id) lod $lod." }
        $entries.Add([pscustomobject]@{
            familyId = [uint32]$family.id
            name = [string]$family.name
            file = $fileName
            sourcePath = [string]$sourceEntry.sourcePath
            sourceGroup = [string]$sourceEntry.sourceGroup
            sourceSha256 = [string]$sourceEntry.sourceSha256
            transform = [string]$sourceEntry.transform
            payload = $bytes
            size = [uint32]$bytes.Length
            offset = [uint32]0
            format = [uint32]0
            tgaColorMapType = [int]$info.colorMapType
            tgaImageType = [int]$info.imageType
            tgaPixelDepth = [int]$info.pixelDepth
            tgaWidth = [int]$info.width
            tgaHeight = [int]$info.height
        }) | Out-Null
    }

    $headerSize = 20
    $entrySize = 16
    [uint32]$dataOffset = $headerSize + ($entrySize * $entries.Count)
    [uint32]$cursor = $dataOffset
    foreach ($entry in $entries) { $entry.offset = $cursor; $cursor += $entry.size }

    $bankPath = Join-Path $OutDir ("TBK{0}.BIN" -f $lod)
    $stream = [System.IO.File]::Open($bankPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $writer = New-Object System.IO.BinaryWriter($stream)
        Write-U32 $writer 0x314B4254
        Write-U16 $writer 1
        Write-U16 $writer ([uint16]$lod)
        Write-U32 $writer ([uint32]$entries.Count)
        Write-U32 $writer $dataOffset
        Write-U32 $writer 0
        foreach ($entry in $entries) {
            Write-U32 $writer $entry.familyId
            Write-U32 $writer $entry.offset
            Write-U32 $writer $entry.size
            Write-U32 $writer $entry.format
        }
        foreach ($entry in $entries) { $writer.Write([byte[]]$entry.payload) }
        $writer.Flush()
    }
    finally {
        if ($writer) { $writer.Dispose() }
        $stream.Dispose()
    }

    $indexPath = Join-Path $ReportDir ("TBK{0}.json" -f $lod)
    $indexEntries = @($entries | ForEach-Object {
        [pscustomobject]([ordered]@{
            familyId = [int]$_.familyId
            name = $_.name
            file = $_.file
            sourcePath = $_.sourcePath
            sourceGroup = $_.sourceGroup
            sourceSha256 = $_.sourceSha256
            transform = $_.transform
            offset = [int64]$_.offset
            size = [int64]$_.size
            format = [int]$_.format
            tgaColorMapType = $_.tgaColorMapType
            tgaImageType = $_.tgaImageType
            tgaPixelDepth = $_.tgaPixelDepth
            tgaWidth = $_.tgaWidth
            tgaHeight = $_.tgaHeight
        })
    })
    [pscustomobject]@{ version = 2; lod = $lod; count = $entries.Count; bank = [System.IO.Path]::GetFileName($bankPath); entries = $indexEntries } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $indexPath -Encoding UTF8

    foreach ($entry in $indexEntries) {
        $compatEntries.Add([pscustomobject]@{
            lod = $lod; familyId = $entry.familyId; file = $entry.file
            sourcePath = $entry.sourcePath; sourceGroup = $entry.sourceGroup; transform = $entry.transform
            colorMapType = $entry.tgaColorMapType; imageType = $entry.tgaImageType
            pixelDepth = $entry.tgaPixelDepth; width = $entry.tgaWidth; height = $entry.tgaHeight
        }) | Out-Null
    }
    $bankSummaries.Add([pscustomobject]@{
        lod = $lod; bankPath = $bankPath; indexPath = $indexPath
        count = $entries.Count; bytes = (Get-Item -LiteralPath $bankPath).Length
    }) | Out-Null
    Write-Host ("OK TBK{0}.BIN entries:{1}" -f $lod, $entries.Count)
}

$compatPath = Join-Path $ReportDir "tga_compat_report.json"
[pscustomobject]@{ version = 2; generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"); entries = @($compatEntries.ToArray()) } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compatPath -Encoding UTF8
$manifestPath = Join-Path $ReportDir "texbanks_manifest.json"
[pscustomobject]@{
    version = 2
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    sourceJson = [System.IO.Path]::GetFullPath($JsonPath)
    preparedTextureDir = [System.IO.Path]::GetFullPath($PreparedTextureDir)
    allowedRoots = @($sourceManifest.allowedRoots)
    banks = @($bankSummaries.ToArray())
    tgaCompatReport = $compatPath
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
