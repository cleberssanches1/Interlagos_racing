param(
    [string]$SourceDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [string]$ManifestDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [switch]$IncludeNya,
    [switch]$IncludeTga
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "SourceDir nao encontrado: $SourceDir"
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
if (-not (Test-Path -LiteralPath $ManifestDir)) {
    New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
}

function Write-U32LE([System.IO.BinaryWriter]$bw, [uint32]$v) { $bw.Write([uint32]$v) }
function Write-FixedAscii([System.IO.BinaryWriter]$bw, [string]$text, [int]$len) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($text)
    $buf = New-Object byte[] $len
    $n = [Math]::Min($bytes.Length, $len - 1)
    if ($n -gt 0) { [Array]::Copy($bytes, 0, $buf, 0, $n) }
    $bw.Write($buf)
}

function Build-Pack {
    param(
        [string]$TypeName,
        [string[]]$FilePaths,
        [string]$OutPath
    )

    $files = @($FilePaths | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    if ($files.Count -eq 0) {
        return $null
    }

    $entries = New-Object System.Collections.Generic.List[object]
    $entrySize = 64 + 4 + 4
    $headerSize = 4 + 4 + 4
    $tableSize = $files.Count * $entrySize
    $dataOffset = $headerSize + $tableSize
    $cursor = [uint32]$dataOffset

    foreach ($p in $files) {
        $fi = Get-Item -LiteralPath $p
        $entries.Add([pscustomobject]@{
            name = [string]$fi.Name
            path = [string]$fi.FullName
            size = [uint32]$fi.Length
            offset = [uint32]$cursor
        }) | Out-Null
        $cursor = [uint32]($cursor + [uint32]$fi.Length)
    }

    $fs = [System.IO.File]::Open($OutPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        # 'PAK1'
        Write-U32LE $bw 0x314B4150
        Write-U32LE $bw 1
        Write-U32LE $bw ([uint32]$entries.Count)

        foreach ($e in $entries) {
            Write-FixedAscii $bw $e.name 64
            Write-U32LE $bw ([uint32]$e.offset)
            Write-U32LE $bw ([uint32]$e.size)
        }

        foreach ($e in $entries) {
            $bytes = [System.IO.File]::ReadAllBytes($e.path)
            $bw.Write($bytes)
        }
        $bw.Flush()
    }
    finally {
        $fs.Close()
    }

    return [pscustomobject]@{
        type = $TypeName
        out = $OutPath
        count = [int]$entries.Count
        bytes = [int](Get-Item -LiteralPath $OutPath).Length
        items = @($entries.ToArray())
    }
}

$groups = [ordered]@{
    GEO   = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???.GEO" | Sort-Object Name | ForEach-Object FullName))
    SDR   = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???.SDR" | Sort-Object Name | ForEach-Object FullName))
    BDR   = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "B*.BDR" | Sort-Object Name | ForEach-Object FullName))
    MAT8  = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???M8.MAT" | Sort-Object Name | ForEach-Object FullName))
    MAT16 = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???M16.MAT" | Sort-Object Name | ForEach-Object FullName))
    MAT32 = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???M32.MAT" | Sort-Object Name | ForEach-Object FullName))
    MAT64 = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "S???M64.MAT" | Sort-Object Name | ForEach-Object FullName))
}

if ($IncludeNya) {
    $groups["NYA"] = @((Get-ChildItem -LiteralPath $SourceDir -File -Filter "SEG_*.NYA" | Sort-Object Name | ForEach-Object FullName))
}
if ($IncludeTga) {
    $groups["TGA8"]  = @((Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Name -match '^F\d{3}_?8\.TGA$' }  | Sort-Object Name | ForEach-Object FullName))
    $groups["TGA16"] = @((Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Name -match '^F\d{3}_?16\.TGA$' } | Sort-Object Name | ForEach-Object FullName))
    $groups["TGA32"] = @((Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Name -match '^F\d{3}_?32\.TGA$' } | Sort-Object Name | ForEach-Object FullName))
    $groups["TGA64"] = @((Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Name -match '^F\d{3}_?64\.TGA$' } | Sort-Object Name | ForEach-Object FullName))
}

$results = New-Object System.Collections.Generic.List[object]
foreach ($k in $groups.Keys) {
    $out = Join-Path $OutDir ("{0}.BIN" -f $k)
    $r = Build-Pack -TypeName $k -FilePaths $groups[$k] -OutPath $out
    if ($null -ne $r) {
        $results.Add($r) | Out-Null
        Write-Host ("OK {0}.BIN files:{1} bytes:{2}" -f $k, $r.count, $r.bytes)
    } else {
        Write-Host ("SKIP {0}.BIN (sem arquivos)" -f $k)
    }
}

$manifestPath = Join-Path $ManifestDir "packs_manifest.json"
$manifest = [ordered]@{
    version = 1
    generated_at_utc = [string]([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"))
    source_dir = [string]$SourceDir
    out_dir = [string]$OutDir
    packs = @($results.ToArray())
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host ("Manifest: {0}" -f $manifestPath)
