param(
    [string]$ConverterDir = "C:\saturn\tools\ModelConverter-linux-main\BuildDrop",
    [string]$SourceObjDir = "C:\Models\png\sectors\source",
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$PackageDir = "C:\saturn\SaturnRingLib-main\Projects\pacote_rancing",
    [string]$CdDataDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [string]$TextureRoot = "C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA",
    [string]$Pattern = "seg_*.obj",
    [ValidateSet("Smooth", "Flat")]
    [string]$Shading = "Smooth",
    [int]$TexWidth = 8,
    [int]$TexHeight = 8,
    [int]$TexPadWidth = 8,
    [switch]$UseLodSubfolders = $false,
    [switch]$RebuildSegmentsMap = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-SegmentIdFromFile([string]$BaseName) {
    if ($BaseName -match '^seg_(\d+)$') {
        return [int]$Matches[1]
    }
    return $null
}

if (-not (Test-Path -LiteralPath $SourceObjDir)) { throw "SourceObjDir nao encontrado: $SourceObjDir" }
if (-not (Test-Path -LiteralPath $PackageDir)) { New-Item -Path $PackageDir -ItemType Directory -Force | Out-Null }
if (-not (Test-Path -LiteralPath $CdDataDir)) { New-Item -Path $CdDataDir -ItemType Directory -Force | Out-Null }

$scriptDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrWhiteSpace($scriptDir)) {
    throw "Nao foi possivel resolver o diretorio de scripts (PSScriptRoot/MyInvocation)."
}

$script:exportScript = Join-Path $scriptDir "export_nya_with_segments_json.ps1"
$script:componentScript = Join-Path $scriptDir "generate_segment_component.ps1"
$script:sdrScript = Join-Path $scriptDir "generate_segment_draw_ready.ps1"
$script:rdrScript = Join-Path $scriptDir "generate_segment_runtime_draw.ps1"
$script:trkRdrPackScript = Join-Path $scriptDir "generate_track_runtime_pack.ps1"
$script:bdrScript = Join-Path $scriptDir "generate_batch_draw_ready.ps1"
$script:texbanksScript = Join-Path $scriptDir "generate_texbanks.ps1"
$script:seg1FamScript = Join-Path $scriptDir "build_seg1_facefam_bin.ps1"
$script:packByTypeScript = Join-Path $scriptDir "pack_assets_by_type.ps1"
$script:copyRenScript = Join-Path $scriptDir "copy_ren_textures_to_data.ps1"
$script:updateSegmentsMapScript = Join-Path $scriptDir "update_segments_map_with_renamed_textures.ps1"
$script:minifyJsonScript = Join-Path $scriptDir "minify_json.py"

if (-not (Test-Path -LiteralPath $script:exportScript)) { throw "Script nao encontrado: $script:exportScript" }
if (-not (Test-Path -LiteralPath $script:componentScript)) { throw "Script nao encontrado: $script:componentScript" }
if (-not (Test-Path -LiteralPath $script:sdrScript)) { throw "Script nao encontrado: $script:sdrScript" }
if (-not (Test-Path -LiteralPath $script:rdrScript)) { throw "Script nao encontrado: $script:rdrScript" }
if (-not (Test-Path -LiteralPath $script:bdrScript)) { throw "Script nao encontrado: $script:bdrScript" }
if (-not (Test-Path -LiteralPath $script:texbanksScript)) { throw "Script nao encontrado: $script:texbanksScript" }
if (-not (Test-Path -LiteralPath $script:seg1FamScript)) { throw "Script nao encontrado: $script:seg1FamScript" }
if (-not (Test-Path -LiteralPath $script:packByTypeScript)) { throw "Script nao encontrado: $script:packByTypeScript" }
if (-not (Test-Path -LiteralPath $script:copyRenScript)) { throw "Script nao encontrado: $script:copyRenScript" }
if (-not (Test-Path -LiteralPath $script:updateSegmentsMapScript)) { throw "Script nao encontrado: $script:updateSegmentsMapScript" }
if (-not (Test-Path -LiteralPath $script:minifyJsonScript)) { throw "Script nao encontrado: $script:minifyJsonScript" }

Write-Host "=== Etapa 1/3: Exportar NYA + segments_map.json ==="
$exportArgs = @{
    ConverterDir = $ConverterDir
    SourceObjDir = $SourceObjDir
    ResultDir = $ResultDir
    CdDataDir = $PackageDir
    Pattern = $Pattern
    Shading = $Shading
    TexWidth = $TexWidth
    TexHeight = $TexHeight
    TexPadWidth = $TexPadWidth
}
if ($RebuildSegmentsMap) {
    $exportArgs.PreserveSidecars = $true
}
& $script:exportScript @exportArgs

$jsonPath = Join-Path $PackageDir "segments_map.json"
if (-not (Test-Path -LiteralPath $jsonPath)) {
    throw "segments_map.json nao foi gerado em: $jsonPath"
}

Write-Host "=== Etapa 2/7: Atualizar segments_map.json válido ==="
function Get-SegmentsMapSource {
    param(
        [string]$MapDir
    )
    $candidates = @(
        "SAP.json",
        "SAP",
        "sap.json",
        "seg_map.json",
        "seg_map",
        "segments_map_before_rebuild.json",
        "segments_map",
        "segmap",
        "smap"
    )
    foreach ($name in $candidates) {
        $path = Join-Path $MapDir $name
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }
    return $null
}

if ($RebuildSegmentsMap) {
    Write-Host "RebuildSegmentsMap ativo: mantendo o segments_map.json novo da exportacao nesta etapa."
}
else {
    $sourceMap = Get-SegmentsMapSource -MapDir $PackageDir
    if ([string]::IsNullOrWhiteSpace($sourceMap)) {
        Write-Host "Nenhuma fonte antiga de segments_map encontrada; mantendo o arquivo gerado na exportacao."
    }
    else {
        Copy-Item -LiteralPath $sourceMap -Destination $jsonPath -Force
        Write-Host ("segments_map.json atualizado a partir de {0}" -f $sourceMap)
    }
}

Write-Host "=== Etapa 3/7: Gerar GEO/MAT para cada LOD ==="
$lodDirs = @(
    @{ Lod = 8; Dir = Join-Path $ResultDir "obj_8" },
    @{ Lod = 16; Dir = Join-Path $ResultDir "obj_16" },
    @{ Lod = 32; Dir = Join-Path $ResultDir "obj_32" },
    @{ Lod = 64; Dir = Join-Path $ResultDir "obj_64" }
)

$componentScriptPath = $script:componentScript
if (-not (Test-Path -LiteralPath $componentScriptPath)) {
    throw "Script de componente nao encontrado: $componentScriptPath"
}

$segmentsDone = New-Object System.Collections.Generic.HashSet[int]
foreach ($entry in $lodDirs) {
    if (-not (Test-Path -LiteralPath $entry.Dir)) {
        Write-Host "Aviso: pasta LOD ausente, pulando: $($entry.Dir)"
        continue
    }
    $objs = Get-ChildItem -LiteralPath $entry.Dir -Filter $Pattern | Sort-Object Name
    foreach ($obj in $objs) {
        $id = Get-SegmentIdFromFile $obj.BaseName
        if ($null -eq $id) { continue }
        $segmentsDone.Add($id) | Out-Null
        try {
            & $componentScriptPath `
                -SegmentId $id `
                -ObjDir $entry.Dir `
                -JsonPath $jsonPath `
                -OutDir $PackageDir `
                -Lod $entry.Lod
        }
        catch {
            Write-Host ("Falha LOD{0} SEG_{1:D3}: {2}" -f $entry.Lod, $id, $_.Exception.Message)
        }
    }
}

Write-Host ("Concluido GEO/MAT por LOD: {0} segmentos distintos encontrados" -f $segmentsDone.Count)

function Test-GeoMatPayload {
    param(
        [string]$BaseDir
    )

    $errors = New-Object System.Collections.Generic.List[string]

    $geoFiles = @(Get-ChildItem -LiteralPath $BaseDir -File -Filter "S???.GEO" -ErrorAction SilentlyContinue)
    foreach ($f in $geoFiles) {
        try {
            [byte[]]$b = [System.IO.File]::ReadAllBytes($f.FullName)
            if ($b.Length -lt 24) {
                $errors.Add(("GEO pequeno: {0} len={1}" -f $f.Name, $b.Length)) | Out-Null
                continue
            }
            $magic = [uint32][System.BitConverter]::ToUInt32($b, 0)
            $ver = [uint16][System.BitConverter]::ToUInt16($b, 4)
            $payload = [uint32][System.BitConverter]::ToUInt32($b, 12)
            $expect = [uint32]($payload + 16)
            if ($magic -ne 0x314F4547 -or $ver -ne 1 -or $expect -ne $b.Length) {
                $errors.Add(("GEO invalido: {0} magic=0x{1:X8} ver={2} payload+16={3} len={4}" -f $f.Name, $magic, $ver, $expect, $b.Length)) | Out-Null
            }
        }
        catch {
            $errors.Add(("Falha lendo GEO {0}: {1}" -f $f.Name, $_.Exception.Message)) | Out-Null
        }
    }

    $matFiles = @(Get-ChildItem -LiteralPath $BaseDir -File -Filter "S???M*.MAT" -ErrorAction SilentlyContinue)
    foreach ($f in $matFiles) {
        try {
            [byte[]]$b = [System.IO.File]::ReadAllBytes($f.FullName)
            if ($b.Length -lt 20) {
                $errors.Add(("MAT pequeno: {0} len={1}" -f $f.Name, $b.Length)) | Out-Null
                continue
            }
            $magic = [uint32][System.BitConverter]::ToUInt32($b, 0)
            $ver = [uint16][System.BitConverter]::ToUInt16($b, 4)
            $payload = [uint32][System.BitConverter]::ToUInt32($b, 12)
            $expect = [uint32]($payload + 16)
            if ($magic -ne 0x3154414D -or $ver -ne 1 -or $expect -ne $b.Length) {
                $errors.Add(("MAT invalido: {0} magic=0x{1:X8} ver={2} payload+16={3} len={4}" -f $f.Name, $magic, $ver, $expect, $b.Length)) | Out-Null
            }
        }
        catch {
            $errors.Add(("Falha lendo MAT {0}: {1}" -f $f.Name, $_.Exception.Message)) | Out-Null
        }
    }

    return @($errors.ToArray())
}

$payloadErrors = @(Test-GeoMatPayload -BaseDir $PackageDir)
if ($payloadErrors.Count -gt 0) {
    Write-Host "Falha de integridade apos gerar GEO/MAT. Arquivos corrompidos detectados:"
    $payloadErrors | ForEach-Object { Write-Host (" - " + $_) }
    throw "GEO/MAT invalidos. Interrompido antes da etapa SDR para evitar propagar corrupcao."
}

Write-Host "=== Etapa 3.5/7: Gerar segmentos draw-ready SDR1 ==="
& $script:sdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -Lod 8 `
    -AllSegments

Write-Host "=== Etapa 3.6/7: Gerar blobs runtime RDR1 ==="
& $script:rdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -AllSegments

Write-Host "=== Etapa 3.7/7: Gerar batches draw-ready BDR1 ==="
& $script:bdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -BatchSize 2 `
    -AllBatches

Write-Host "=== Etapa 4/6: Renomear/copiar texturas com sufixo ==="
& $script:copyRenScript `
    -DataDir $PackageDir `
    -TextOutDir $CdDataDir

Write-Host "=== Etapa 4.5: Atualizar segments_map com texturas renomeadas ==="
$renManifestPath = Join-Path $PackageDir "ren_textures_copy_map.json"
& $script:updateSegmentsMapScript `
    -SegmentsMapPath $jsonPath `
    -RenManifestPath $renManifestPath `
    -ResultDir $ResultDir

Write-Host "=== Etapa 4.6: Minificar segments_map.json ==="
& python $script:minifyJsonScript $jsonPath

function Copy-SegmentsMapShortNames {
    param(
        [string]$Source,
        [string[]]$TargetDirs
    )

    $names = @(
        "smap.txt",
        "SMAP.TXT",
        "smap.txt;1",
        "SMAP.TXT;1",
        "sap.txt",
        "SAP.TXT",
        "sap.txt;1",
        "SAP.TXT;1"
    )

    foreach ($dir in $TargetDirs) {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
        }
        foreach ($name in $names) {
            $dest = Join-Path $dir $name
            try {
                $sameFile =
                    [System.IO.Path]::GetFullPath($Source).TrimEnd('\').ToLowerInvariant() -eq
                    [System.IO.Path]::GetFullPath($dest).TrimEnd('\').ToLowerInvariant()
                if ($sameFile) { continue }
            }
            catch {
                # If path normalization fails, fall back to Copy-Item and let it raise a real error.
            }
            Copy-Item -Force -Path $Source -Destination $dest
        }
    }
}

# Remove helper files from cd/data so only runtime-facing files remain there.
function Remove-CdDataAuxFiles {
    param(
        [string[]]$TargetDirs
    )

    $patterns = @(
        "*.json",
        "segments_map",
        "segments_map.json"
    )

    foreach ($dir in $TargetDirs) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        foreach ($pattern in $patterns) {
            $files = @(Get-ChildItem -LiteralPath $dir -File -Filter $pattern -ErrorAction SilentlyContinue)
            foreach ($file in $files) {
                try {
                    Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
                }
                catch {
                    Write-Host ("Aviso: nao foi possivel remover auxiliar de cd/data: {0}" -f $file.FullName)
                }
            }
        }
    }
}

Write-Host "=== Etapa 4.7: Criar aliases 8.3 para segments_map ==="
$upperCdDir = Join-Path (Split-Path -Parent $CdDataDir) "CD\DATA"
$targetDirs = @($CdDataDir)
if ($upperCdDir -ne $CdDataDir) { $targetDirs += $upperCdDir }
Copy-SegmentsMapShortNames -Source $jsonPath -TargetDirs $targetDirs

Write-Host "=== Etapa 5/6: Gerar TEXBANK_*.BIN ==="
& $script:texbanksScript `
    -JsonPath $jsonPath `
    -TextureRoot $PackageDir `
    -OutDir $CdDataDir `
    -ReportDir $PackageDir `
    -UseLodSubfolders:$UseLodSubfolders

Write-Host "=== Etapa 6/6: Gerar S001FAM.BIN ==="
$seg1FamOut = Join-Path $CdDataDir "S001FAM.BIN"
& $script:seg1FamScript `
    -JsonPath $jsonPath `
    -OutPath $seg1FamOut

Write-Host "=== Etapa 7/7: Empacotar por tipo (*.BIN) incluindo imagens ==="
& $script:packByTypeScript `
    -SourceDir $PackageDir `
    -OutDir $CdDataDir `
    -ManifestDir $PackageDir `
    -IncludeTga

Write-Host "=== Etapa 7.1/7: Gerar pack runtime dedicado TRKRDR.BIN ==="
& $script:trkRdrPackScript `
    -DataDir $PackageDir `
    -OutPath (Join-Path $CdDataDir "TRKRDR.BIN")

Write-Host "=== Limpeza: Remover auxiliares de cd/data ==="
Remove-CdDataAuxFiles -TargetDirs $targetDirs

Write-Host "=== Validacao final ==="
$expectedSegmentIds = $segmentsDone | Sort-Object -Unique
$expectedCount = $expectedSegmentIds.Count

$geoCountLong = @(Get-ChildItem -Path $PackageDir -File -Filter "SEG_*.GEO").Count
$geoCountShort = @(Get-ChildItem -Path $PackageDir -File -Filter "S???.GEO").Count
$rdrCount = @(Get-ChildItem -Path $PackageDir -File -Filter "S???.RDR").Count
$matCountLong = @(Get-ChildItem -Path $PackageDir -File -Filter "SEG_*.MAT").Count
$matCountShort = @(Get-ChildItem -Path $PackageDir -File -Filter "S???M*.MAT").Count
$geoCount = [Math]::Max($geoCountLong, $geoCountShort)
$matCount = [Math]::Max($matCountLong, $matCountShort)
$texbankCountLegacy = @(Get-ChildItem -Path $CdDataDir -File -Filter "TEXBANK_*.BIN").Count
$texbankCountShort = @(Get-ChildItem -Path $CdDataDir -File -Filter "TBK*.BIN").Count
$texbankCount = $texbankCountLegacy + $texbankCountShort

$segmentsMapPath = Join-Path $PackageDir "segments_map.json"
$texManifestPath = Join-Path $PackageDir "texbanks_manifest.json"
$tgaCompatPath = Join-Path $PackageDir "tga_compat_report.json"
$seg1FamPath = Join-Path $CdDataDir "S001FAM.BIN"
$packsManifestPath = Join-Path $PackageDir "packs_manifest.json"
$hasSegmentsMap = Test-Path -LiteralPath $segmentsMapPath
$hasTexManifest = Test-Path -LiteralPath $texManifestPath
$hasTgaCompat = Test-Path -LiteralPath $tgaCompatPath
$hasSeg1Fam = Test-Path -LiteralPath $seg1FamPath
$hasPacksManifest = Test-Path -LiteralPath $packsManifestPath
$hasGeoBin = Test-Path -LiteralPath (Join-Path $CdDataDir "GEO.BIN")
$hasRdrBin = Test-Path -LiteralPath (Join-Path $CdDataDir "RDR.BIN")
$hasTrkRdrBin = Test-Path -LiteralPath (Join-Path $CdDataDir "TRKRDR.BIN")
$hasMat8Bin = Test-Path -LiteralPath (Join-Path $CdDataDir "MAT8.BIN")
$hasMat16Bin = Test-Path -LiteralPath (Join-Path $CdDataDir "MAT16.BIN")
$hasMat32Bin = Test-Path -LiteralPath (Join-Path $CdDataDir "MAT32.BIN")
$hasMat64Bin = Test-Path -LiteralPath (Join-Path $CdDataDir "MAT64.BIN")

Write-Host ("EXPECTED SEGMENTS: {0}" -f $expectedCount)
Write-Host ("FOUND GEO         : {0} (long:{1} short:{2})" -f $geoCount, $geoCountLong, $geoCountShort)
Write-Host ("FOUND RDR         : {0}" -f $rdrCount)
Write-Host ("FOUND MAT         : {0} (long:{1} short:{2})" -f $matCount, $matCountLong, $matCountShort)
Write-Host ("FOUND TEXBANK BIN : {0} (legacy:{1} short:{2})" -f $texbankCount, $texbankCountLegacy, $texbankCountShort)
Write-Host ("HAS segments_map  : {0}" -f $hasSegmentsMap)
Write-Host ("HAS manifest      : {0}" -f $hasTexManifest)
Write-Host ("HAS TGA compat    : {0}" -f $hasTgaCompat)
Write-Host ("HAS S001FAM.BIN   : {0}" -f $hasSeg1Fam)
Write-Host ("HAS GEO.BIN       : {0}" -f $hasGeoBin)
Write-Host ("HAS RDR.BIN       : {0}" -f $hasRdrBin)
Write-Host ("HAS TRKRDR.BIN    : {0}" -f $hasTrkRdrBin)
Write-Host ("HAS MAT8.BIN      : {0}" -f $hasMat8Bin)
Write-Host ("HAS MAT16.BIN     : {0}" -f $hasMat16Bin)
Write-Host ("HAS MAT32.BIN     : {0}" -f $hasMat32Bin)
Write-Host ("HAS MAT64.BIN     : {0}" -f $hasMat64Bin)
Write-Host ("HAS packs manifest: {0}" -f $hasPacksManifest)

$validationErrors = New-Object System.Collections.Generic.List[string]
if ($geoCount -lt $expectedCount) { $validationErrors.Add(("GEO insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $geoCount)) | Out-Null }
if ($rdrCount -lt $expectedCount) { $validationErrors.Add(("RDR insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $rdrCount)) | Out-Null }
if ($matCount -lt $expectedCount) { $validationErrors.Add(("MAT insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $matCount)) | Out-Null }
if ($texbankCount -lt 4) { $validationErrors.Add(("TEXBANK_*.BIN insuficiente: esperado>=4, encontrado={0}" -f $texbankCount)) | Out-Null }
if (-not $hasSegmentsMap) { $validationErrors.Add("segments_map.json ausente em pacote_rancing") | Out-Null }
if (-not $hasTexManifest) { $validationErrors.Add("texbanks_manifest.json ausente em pacote_rancing") | Out-Null }
if (-not $hasTgaCompat) { $validationErrors.Add("tga_compat_report.json ausente em pacote_rancing") | Out-Null }
if (-not $hasSeg1Fam) { $validationErrors.Add("S001FAM.BIN ausente em cd\\data") | Out-Null }
if (-not $hasGeoBin) { $validationErrors.Add("GEO.BIN ausente em cd\\data") | Out-Null }
if (-not $hasRdrBin) { $validationErrors.Add("RDR.BIN ausente em cd\\data") | Out-Null }
if (-not $hasTrkRdrBin) { $validationErrors.Add("TRKRDR.BIN ausente em cd\\data") | Out-Null }
if (-not $hasMat8Bin) { $validationErrors.Add("MAT8.BIN ausente em cd\\data") | Out-Null }
if (-not $hasMat16Bin) { $validationErrors.Add("MAT16.BIN ausente em cd\\data") | Out-Null }
if (-not $hasMat32Bin) { $validationErrors.Add("MAT32.BIN ausente em cd\\data") | Out-Null }
if (-not $hasMat64Bin) { $validationErrors.Add("MAT64.BIN ausente em cd\\data") | Out-Null }
if (-not $hasPacksManifest) { $validationErrors.Add("packs_manifest.json ausente em pacote_rancing") | Out-Null }

if ($validationErrors.Count -gt 0) {
    Write-Host "Falhas na validacao final:"
    $validationErrors | ForEach-Object { Write-Host (" - " + $_) }
    throw "Validacao final falhou. Corrija os itens acima e rode novamente."
}

Write-Host "Validacao final: OK"
Write-Host "Arquivos soltos finais em: $PackageDir"
Write-Host "Arquivos BIN/TXT finais em: $CdDataDir"
if ($RebuildSegmentsMap) {
    Write-Host "=== Reconstruindo segments_map completo ==="
    & ".\tools\build_segments_map_full.ps1" `
        -ResultDir $ResultDir `
        -OutJsonPath $jsonPath `
        -SourceJsonPath $jsonPath `
        -Shading $Shading `
        -TexWidth $TexWidth `
        -TexHeight $TexHeight `
        -TexPadWidth $TexPadWidth
    Write-Host "=== Minificando segments_map final apos rebuild ==="
    & python $script:minifyJsonScript $jsonPath
    Write-Host "=== Recriando aliases 8.3 apos rebuild final ==="
    Copy-SegmentsMapShortNames -Source $jsonPath -TargetDirs $targetDirs
    Write-Host "=== Limpeza final: Remover auxiliares de cd/data apos rebuild ==="
    Remove-CdDataAuxFiles -TargetDirs $targetDirs
} else {
    Write-Host "segments_map.json mantido (rebuild desativado)."
}
