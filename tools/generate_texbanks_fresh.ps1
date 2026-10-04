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

function Get-CanonicalTgaSha256([byte[]]$Bytes, [string]$Path) {
    if ($null -eq $Bytes -or $Bytes.Length -lt 18) { throw "TGA truncado: $Path" }
    # O campo image-id nao participa do conteudo renderizado. Remova-o antes
    # de comparar LOD0/LOD1 para que metadados de exportacao nao criem um
    # override falso no banco compartilhado 64x64.
    $idLength = [int]$Bytes[0]
    if ((18 + $idLength) -gt $Bytes.Length) { throw "TGA com image-id truncado: $Path" }
    [byte[]]$canonical = New-Object byte[] ($Bytes.Length - $idLength)
    [Array]::Copy($Bytes, 0, $canonical, 0, 18)
    $canonical[0] = 0
    [Array]::Copy($Bytes, 18 + $idLength, $canonical, 18, $Bytes.Length - 18 - $idLength)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($canonical))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

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
$bankSpecs = @(
    [pscustomobject]@{ sourceGroup = "lod_0"; bankId = 0; runtimeIndex = 3; nominalTextureSize = 64; fileName = "TBKLOD0.BIN"; indexName = "TBKLOD0.json" },
    [pscustomobject]@{ sourceGroup = "lod_1"; bankId = 1; runtimeIndex = 1; nominalTextureSize = 64; fileName = "TBKLOD1.BIN"; indexName = "TBKLOD1.json" },
    [pscustomobject]@{ sourceGroup = "lod_2"; bankId = 2; runtimeIndex = 2; nominalTextureSize = 32; fileName = "TBKLOD2.BIN"; indexName = "TBKLOD2.json" }
)
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
    $expectedSpec = @($bankSpecs | Where-Object { [int]$_.bankId -eq [int]$entry.bankId }) | Select-Object -First 1
    if ($null -eq $expectedSpec -or [string]$entry.sourceGroup -ne [string]$expectedSpec.sourceGroup) {
        throw "Entrada de banco/agrupamento invalida: family $($entry.familyId) bankId=$($entry.bankId) group=$($entry.sourceGroup)"
    }
}

$json = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$families = @($json.textureFamilies | Sort-Object { [int]$_.id })
if ($families.Count -eq 0) { throw "textureFamilies vazio." }

$bankSummaries = New-Object System.Collections.Generic.List[object]
$compatEntries = New-Object System.Collections.Generic.List[object]
$lod0CanonicalHashes = @{}
foreach ($bankSpec in $bankSpecs) {
    $entries = New-Object System.Collections.Generic.List[object]
    $logicalEntries = New-Object System.Collections.Generic.List[object]
    $sharedFamilyIds = New-Object System.Collections.Generic.List[int]
    foreach ($family in $families) {
        $sourceEntry = @($sourceManifest.entries | Where-Object {
            [int]$_.familyId -eq [int]$family.id -and [int]$_.bankId -eq [int]$bankSpec.bankId
        }) | Select-Object -First 1
        if ($null -eq $sourceEntry) {
            throw "Manifesto sem family $($family.id) bankId $($bankSpec.bankId) ($($bankSpec.sourceGroup))."
        }
        $path = [string]$sourceEntry.targetPath
        if (-not (Test-Path -LiteralPath $path)) {
            # A publicacao atomica move a pasta de staging; manifests publicados
            # podem conservar o targetPath absoluto antigo. Resolva novamente
            # pelo grupo + nome, sempre dentro de PreparedTextureDir.
            $relocatedPath = Join-Path (Join-Path $PreparedTextureDir ([string]$sourceEntry.sourceGroup)) ([System.IO.Path]::GetFileName($path))
            if (Test-Path -LiteralPath $relocatedPath) {
                $path = $relocatedPath
            }
            else {
                throw "Textura preparada ausente: $path (relocado tentado: $relocatedPath)"
            }
        }
        $preparedRoot = [System.IO.Path]::GetFullPath($PreparedTextureDir).TrimEnd('\') + '\'
        $fullPath = [System.IO.Path]::GetFullPath($path)
        if (-not $fullPath.StartsWith($preparedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Textura preparada fora da pasta autorizada: $fullPath"
        }
        $fileName = [System.IO.Path]::GetFileName($fullPath)
        [byte[]]$bytes = [System.IO.File]::ReadAllBytes($path)
        $info = Get-TgaInfo $bytes $path
        $canonicalSha256 = Get-CanonicalTgaSha256 $bytes $path
        $nominal = [int]$bankSpec.nominalTextureSize
        if ([int]$info.width -ne $nominal -or [int]$info.height -ne $nominal) {
            throw ("TGA fora do tamanho nominal {0}x{0}: family {1} bank {2} got {3}x{4} ({5})" -f
                $nominal, $family.id, $bankSpec.sourceGroup, $info.width, $info.height, $path)
        }
        $isSharedFromLod0 = $false
        if ([int]$bankSpec.bankId -eq 0) {
            $lod0CanonicalHashes[[int]$family.id] = $canonicalSha256
        }
        elseif ([int]$bankSpec.bankId -eq 1) {
            if (-not $lod0CanonicalHashes.ContainsKey([int]$family.id)) {
                throw "Hash canonico LOD0 ausente para family $($family.id)."
            }
            $isSharedFromLod0 =
                [string]$lod0CanonicalHashes[[int]$family.id] -eq [string]$canonicalSha256
        }

        $logicalEntry = [pscustomobject]@{
            familyId = [uint32]$family.id
            name = [string]$family.name
            file = $fileName
            sourcePath = [string]$sourceEntry.sourcePath
            sourceGroup = [string]$sourceEntry.sourceGroup
            sourceSha256 = [string]$sourceEntry.sourceSha256
            canonicalSha256 = $canonicalSha256
            transform = [string]$sourceEntry.transform
            sharedFromBankId = $(if ($isSharedFromLod0) { 0 } else { -1 })
            payload = $bytes
            size = [uint32]$bytes.Length
            offset = [uint32]0
            format = [uint32]0
            tgaColorMapType = [int]$info.colorMapType
            tgaImageType = [int]$info.imageType
            tgaPixelDepth = [int]$info.pixelDepth
            tgaWidth = [int]$info.width
            tgaHeight = [int]$info.height
        }
        $logicalEntries.Add($logicalEntry) | Out-Null
        if ($isSharedFromLod0) {
            $sharedFamilyIds.Add([int]$family.id) | Out-Null
        }
        else {
            $entries.Add($logicalEntry) | Out-Null
        }
    }

    $headerSize = 20
    $entrySize = 16
    [uint32]$dataOffset = $headerSize + ($entrySize * $entries.Count)
    [uint32]$cursor = $dataOffset
    foreach ($entry in $entries) { $entry.offset = $cursor; $cursor += $entry.size }

    $bankPath = Join-Path $OutDir $bankSpec.fileName
    $stream = [System.IO.File]::Open($bankPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    $writer = $null
    try {
        $writer = New-Object System.IO.BinaryWriter($stream)
        Write-U32 $writer 0x314B4254
        Write-U16 $writer 2
        Write-U16 $writer ([uint16]$bankSpec.bankId)
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

    $indexPath = Join-Path $ReportDir $bankSpec.indexName
    $indexEntries = @($entries | ForEach-Object {
        [pscustomobject]([ordered]@{
            familyId = [int]$_.familyId
            name = $_.name
            file = $_.file
            sourcePath = $_.sourcePath
            sourceGroup = $_.sourceGroup
            sourceSha256 = $_.sourceSha256
            canonicalSha256 = $_.canonicalSha256
            transform = $_.transform
            sharedFromBankId = [int]$_.sharedFromBankId
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
    [pscustomobject]@{
        version = 4
        bankId = [int]$bankSpec.bankId
        designLod = [string]$bankSpec.sourceGroup
        runtimeIndex = [int]$bankSpec.runtimeIndex
        nominalTextureSize = [int]$bankSpec.nominalTextureSize
        count = $entries.Count
        logicalCount = $logicalEntries.Count
        sharedFromBankId = $(if ([int]$bankSpec.bankId -eq 1) { 0 } else { -1 })
        sharedFamilyIds = @($sharedFamilyIds.ToArray())
        bank = [System.IO.Path]::GetFileName($bankPath)
        entries = $indexEntries
    } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $indexPath -Encoding UTF8

    foreach ($entry in @($logicalEntries.ToArray())) {
        $compatEntries.Add([pscustomobject]@{
            bankId = [int]$bankSpec.bankId; designLod = [string]$bankSpec.sourceGroup
            runtimeIndex = [int]$bankSpec.runtimeIndex; nominalTextureSize = [int]$bankSpec.nominalTextureSize
            familyId = [int]$entry.familyId; file = $entry.file
            sourcePath = $entry.sourcePath; sourceGroup = $entry.sourceGroup; transform = $entry.transform
            canonicalSha256 = $entry.canonicalSha256; sharedFromBankId = [int]$entry.sharedFromBankId
            colorMapType = $entry.tgaColorMapType; imageType = $entry.tgaImageType
            pixelDepth = $entry.tgaPixelDepth; width = $entry.tgaWidth; height = $entry.tgaHeight
        }) | Out-Null
    }
    $bankSummaries.Add([pscustomobject]@{
        bankId = [int]$bankSpec.bankId; designLod = [string]$bankSpec.sourceGroup
        runtimeIndex = [int]$bankSpec.runtimeIndex; nominalTextureSize = [int]$bankSpec.nominalTextureSize
        bankPath = $bankPath; indexPath = $indexPath
        count = $entries.Count; logicalCount = $logicalEntries.Count
        sharedCount = $sharedFamilyIds.Count; sharedFromBankId = $(if ([int]$bankSpec.bankId -eq 1) { 0 } else { -1 })
        bytes = (Get-Item -LiteralPath $bankPath).Length
    }) | Out-Null
    Write-Host ("OK {0} designLod:{1} bankId:{2} entries:{3} shared:{4} logical:{5}" -f
        $bankSpec.fileName, $bankSpec.sourceGroup, $bankSpec.bankId,
        $entries.Count, $sharedFamilyIds.Count, $logicalEntries.Count)
}

$compatPath = Join-Path $ReportDir "tga_compat_report.json"
[pscustomobject]@{ version = 4; generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"); entries = @($compatEntries.ToArray()) } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compatPath -Encoding UTF8
$manifestPath = Join-Path $ReportDir "texbanks_manifest.json"
[pscustomobject]@{
    version = 4
    generatedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    sourceJson = [System.IO.Path]::GetFullPath($JsonPath)
    preparedTextureDir = [System.IO.Path]::GetFullPath($PreparedTextureDir)
    allowedRoots = @($sourceManifest.allowedRoots)
    sharing = [pscustomobject]@{
        mode = "lod0_base_lod1_overrides"
        baseBankId = 0
        overrideBankId = 1
        textureSize = 64
        comparison = "canonical_tga_sha256_without_image_id"
    }
    banks = @($bankSummaries.ToArray())
    tgaCompatReport = $compatPath
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
