param(
    [string]$ConverterDir = "C:\saturn\tools\ModelConverter-linux-main\BuildDrop",
    [string]$SourceObjDir = "C:\Models\png\sectors\source",
    [string]$ResultDir = "C:\Models\png\sectors\result",
    [string]$CdDataDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data",
    [string]$TextureRoot = "C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA",
    [string]$Pattern = "seg_*.obj",
    [ValidateSet("Smooth", "Flat")]
    [string]$Shading = "Smooth",
    [int]$TexWidth = 8,
    [int]$TexHeight = 8,
    [int]$TexPadWidth = 8,
    [switch]$UseLodSubfolders = $false
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
$script:texbanksScript = Join-Path $scriptDir "generate_texbanks.ps1"

if (-not (Test-Path -LiteralPath $script:exportScript)) { throw "Script nao encontrado: $script:exportScript" }
if (-not (Test-Path -LiteralPath $script:componentScript)) { throw "Script nao encontrado: $script:componentScript" }
if (-not (Test-Path -LiteralPath $script:texbanksScript)) { throw "Script nao encontrado: $script:texbanksScript" }

Write-Host "=== Etapa 1/3: Exportar NYA + segments_map.json ==="
& $script:exportScript `
    -ConverterDir $ConverterDir `
    -SourceObjDir $SourceObjDir `
    -ResultDir $ResultDir `
    -CdDataDir $CdDataDir `
    -Pattern $Pattern `
    -Shading $Shading `
    -TexWidth $TexWidth `
    -TexHeight $TexHeight `
    -TexPadWidth $TexPadWidth

$jsonPath = Join-Path $CdDataDir "segments_map.json"
if (-not (Test-Path -LiteralPath $jsonPath)) {
    throw "segments_map.json nao foi gerado em: $jsonPath"
}

Write-Host "=== Etapa 2/3: Gerar GEO/MAT para todos os segmentos ==="
$objs = @(Get-ChildItem -Path $SourceObjDir -Filter $Pattern | Sort-Object Name)
$objCount = $objs.Count
if ($objCount -eq 0) {
    throw "Nenhum OBJ encontrado em $SourceObjDir com padrao $Pattern"
}
$componentScriptPath = $script:componentScript
if (-not (Test-Path -LiteralPath $componentScriptPath)) {
    throw "Script de componente nao encontrado: $componentScriptPath"
}

$ok = 0
$fail = New-Object System.Collections.Generic.List[string]

foreach ($obj in $objs) {
    $id = Get-SegmentIdFromFile $obj.BaseName
    if ($null -eq $id) { continue }

    try {
        & $componentScriptPath `
            -SegmentId $id `
            -ObjDir $SourceObjDir `
            -JsonPath $jsonPath `
            -OutDir $CdDataDir
        $ok++
    }
    catch {
        $fail.Add(("SEG_{0:D3}: {1}" -f $id, $_.Exception.Message)) | Out-Null
    }
}

Write-Host ("Concluido GEO/MAT: TOTAL={0} OK={1} FAIL={2}" -f $objCount, $ok, $fail.Count)
if ($fail.Count -gt 0) {
    $fail | ForEach-Object { Write-Host "Falhou: $_" }
}

Write-Host "=== Etapa 3/3: Gerar TEXBANK_*.BIN ==="
& $script:texbanksScript `
    -JsonPath $jsonPath `
    -TextureRoot $TextureRoot `
    -OutDir $CdDataDir `
    -UseLodSubfolders:$UseLodSubfolders

Write-Host "=== Validacao final ==="
$expectedSegmentIds = @(
    $objs |
    ForEach-Object { Get-SegmentIdFromFile $_.BaseName } |
    Where-Object { $null -ne $_ } |
    Sort-Object -Unique
)
$expectedCount = $expectedSegmentIds.Count

$segCount = @(Get-ChildItem -Path $CdDataDir -File -Filter "SEG_*.NYA").Count
$geoCount = @(Get-ChildItem -Path $CdDataDir -File -Filter "SEG_*.GEO").Count
$matCount = @(Get-ChildItem -Path $CdDataDir -File -Filter "SEG_*.MAT").Count
$texbankCount = @(Get-ChildItem -Path $CdDataDir -File -Filter "TEXBANK_*.BIN").Count

$segmentsMapPath = Join-Path $CdDataDir "segments_map.json"
$texManifestPath = Join-Path $CdDataDir "texbanks_manifest.json"
$hasSegmentsMap = Test-Path -LiteralPath $segmentsMapPath
$hasTexManifest = Test-Path -LiteralPath $texManifestPath

Write-Host ("EXPECTED SEGMENTS: {0}" -f $expectedCount)
Write-Host ("FOUND SEG_*.NYA   : {0}" -f $segCount)
Write-Host ("FOUND SEG_*.GEO   : {0}" -f $geoCount)
Write-Host ("FOUND SEG_*.MAT   : {0}" -f $matCount)
Write-Host ("FOUND TEXBANK BIN : {0}" -f $texbankCount)
Write-Host ("HAS segments_map  : {0}" -f $hasSegmentsMap)
Write-Host ("HAS manifest      : {0}" -f $hasTexManifest)

$validationErrors = New-Object System.Collections.Generic.List[string]
if ($segCount -lt $expectedCount) { $validationErrors.Add(("SEG_*.NYA insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $segCount)) | Out-Null }
if ($geoCount -lt $expectedCount) { $validationErrors.Add(("SEG_*.GEO insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $geoCount)) | Out-Null }
if ($matCount -lt $expectedCount) { $validationErrors.Add(("SEG_*.MAT insuficiente: esperado={0}, encontrado={1}" -f $expectedCount, $matCount)) | Out-Null }
if ($texbankCount -lt 4) { $validationErrors.Add(("TEXBANK_*.BIN insuficiente: esperado>=4, encontrado={0}" -f $texbankCount)) | Out-Null }
if (-not $hasSegmentsMap) { $validationErrors.Add("segments_map.json ausente em cd\\data") | Out-Null }
if (-not $hasTexManifest) { $validationErrors.Add("texbanks_manifest.json ausente em cd\\data") | Out-Null }

if ($validationErrors.Count -gt 0) {
    Write-Host "Falhas na validacao final:"
    $validationErrors | ForEach-Object { Write-Host (" - " + $_) }
    throw "Validacao final falhou. Corrija os itens acima e rode novamente."
}

Write-Host "Validacao final: OK"
Write-Host "Arquivos finais em: $CdDataDir"
