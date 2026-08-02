param(
    [string]$ConverterDir = "C:\saturn\tools\ModelConverter-linux-main\BuildDrop",
    [string]$SourceObjDir = "",
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
    [switch]$RebuildSegmentsMap = $false,
    [bool]$ExportSurfaceFamilyMap = $true,
    [bool]$EnableSeamFaceDedup = $true,
    [switch]$AuditWalls = $false,
    [switch]$AuditWallsStrict = $false,
    [string]$AuditWallsReportDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-DefaultSourceObjDir {
    param(
        [string]$RequestedSourceObjDir,
        [string]$ResultRootDir
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedSourceObjDir)) {
        return $RequestedSourceObjDir
    }

    # 3 Levels of Design: prefer lod_0 (max faces) for map/GEO authority.
    $candidates = @(
        (Join-Path $ResultRootDir "lod_0"),
        (Join-Path $ResultRootDir "lod_1"),
        (Join-Path $ResultRootDir "lod_2"),
        # Legacy 4-folder layout (fallback)
        (Join-Path $ResultRootDir "obj_64"),
        (Join-Path $ResultRootDir "obj_32"),
        (Join-Path $ResultRootDir "obj_16"),
        (Join-Path $ResultRootDir "obj_8"),
        $ResultRootDir,
        "C:\Models\png\sectors\source\lod_0",
        "C:\Models\png\sectors\source\obj_64",
        "C:\Models\png\sectors\source"
    )

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Get-SegmentIdFromFile([string]$BaseName) {
    if ($BaseName -match '^seg_(\d+)$') {
        return [int]$Matches[1]
    }
    return $null
}

function ConvertTo-BoolValue([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    return $Value.Trim().ToLowerInvariant() -eq "true"
}

function Get-KeyValueTokens {
    param(
        [string]$Line
    )

    $map = @{}
    if ([string]::IsNullOrWhiteSpace($Line)) { return $map }
    foreach ($token in ($Line -split '\s+')) {
        if ([string]::IsNullOrWhiteSpace($token)) { continue }
        $eq = $token.IndexOf('=')
        if ($eq -le 0) { continue }
        $key = $token.Substring(0, $eq)
        $value = $token.Substring($eq + 1)
        if (-not [string]::IsNullOrWhiteSpace($key)) {
            $map[$key] = $value
        }
    }
    return $map
}

function New-WallAuditSummaryHtml {
    param(
        [string]$OutPath,
        [string]$AuditedSourceDir,
        [object[]]$Rows
    )

    $html = New-Object System.Collections.Generic.List[string]
    $null = $html.Add("<!DOCTYPE html>")
    $null = $html.Add('<html lang="pt-BR">')
    $null = $html.Add("<head>")
    $null = $html.Add('  <meta charset="UTF-8" />')
    $null = $html.Add("  <title>Resumo da Auditoria Offline de WallSegment2D</title>")
    $null = $html.Add("  <style>")
    $null = $html.Add("body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; line-height: 1.45; color: #111; }")
    $null = $html.Add("table { border-collapse: collapse; width: 100%; margin: 16px 0; }")
    $null = $html.Add("th, td { border: 1px solid #cfd6dd; padding: 8px; text-align: left; vertical-align: top; }")
    $null = $html.Add("th { background: #eef3f8; }")
    $null = $html.Add(".warn { background: #fff4db; }")
    $null = $html.Add(".fail { background: #ffe3e3; }")
    $null = $html.Add("a { color: #0b5cab; text-decoration: none; }")
    $null = $html.Add("  </style>")
    $null = $html.Add("</head>")
    $null = $html.Add("<body>")
    $null = $html.Add("<h1>Resumo da Auditoria Offline de WallSegment2D</h1>")
    $null = $html.Add(("<p><strong>Fonte auditada:</strong> {0}</p>" -f [System.Net.WebUtility]::HtmlEncode($AuditedSourceDir)))
    $null = $html.Add("<table>")
    $null = $html.Add("<tr><th>Segmento</th><th>OBJ</th><th>Faces verticais</th><th>Walls</th><th>Duplicados</th><th>Deg faces</th><th>Deg segs</th><th>Normals zero</th><th>Driveable</th><th>Mismatch</th><th>Relatório</th></tr>")
    foreach ($row in $Rows) {
        $cls = ""
        if ($row.FamilyMismatch -or $row.DegFaces -gt 0 -or $row.DegSegments -gt 0) {
            $cls = ' class="fail"'
        }
        elseif ($row.Duplicates -gt 0 -or $row.ZeroNormals -gt 0) {
            $cls = ' class="warn"'
        }

        $reportCell = ""
        if (-not [string]::IsNullOrWhiteSpace($row.ReportFileName)) {
            $reportHref = [System.Net.WebUtility]::HtmlEncode($row.ReportFileName)
            $reportCell = ('<a href="{0}">abrir</a>' -f $reportHref)
        }

        $null = $html.Add((
            "<tr{0}><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td><td>{8}</td><td>{9}</td><td>{10}</td><td>{11}</td></tr>" -f
            $cls,
            $row.SegmentId,
            [System.Net.WebUtility]::HtmlEncode($row.ObjName),
            $row.VerticalCandidates,
            $row.WallSegments,
            $row.Duplicates,
            $row.DegFaces,
            $row.DegSegments,
            $row.ZeroNormals,
            $row.DriveableFaces,
            $row.FamilyMismatch,
            $reportCell
        ))
    }
    $null = $html.Add("</table>")
    $null = $html.Add("</body></html>")
    [System.IO.File]::WriteAllLines($OutPath, $html)
}

function Invoke-WallAuditBatch {
    param(
        [string]$ObjRootDir,
        [string]$SegmentsMapPath,
        [string]$SfMapPath,
        [string]$Pattern,
        [string]$ReportDir,
        [switch]$Strict
    )

    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($null -eq $pythonCmd) {
        throw "Python nao encontrado no PATH; auditoria offline nao pode ser executada."
    }

    $auditScriptPath = Join-Path $scriptDir "audit_wall_segments_offline.py"
    if (-not (Test-Path -LiteralPath $auditScriptPath)) {
        throw "Script de auditoria nao encontrado: $auditScriptPath"
    }

    if (-not (Test-Path -LiteralPath $ReportDir)) {
        New-Item -Path $ReportDir -ItemType Directory -Force | Out-Null
    }

    $segmentsMapJson = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
    $mappedSegmentIds = New-Object 'System.Collections.Generic.HashSet[int]'
    foreach ($seg in @($segmentsMapJson.segments)) {
        if ($null -eq $seg) { continue }
        if (-not ($seg.PSObject.Properties.Name -contains "id")) { continue }
        [void]$mappedSegmentIds.Add([int]$seg.id)
    }

    $objs = @(Get-ChildItem -LiteralPath $ObjRootDir -Recurse -File -Filter $Pattern | Sort-Object FullName)
    if ($objs.Count -le 0) {
        throw ("Nenhum OBJ encontrado para auditoria em {0} com Pattern={1}" -f $ObjRootDir, $Pattern)
    }

    $rows = New-Object System.Collections.Generic.List[object]
    $strictFailures = New-Object System.Collections.Generic.List[string]
    $skippedObjs = New-Object System.Collections.Generic.List[string]
    $summaryJsonPath = Join-Path $ReportDir "wall_audit_summary.json"
    $summaryHtmlPath = Join-Path $ReportDir "wall_audit_summary.html"

    foreach ($obj in $objs) {
        $segmentId = Get-SegmentIdFromFile $obj.BaseName
        if ($null -eq $segmentId) { continue }
        if (-not $mappedSegmentIds.Contains([int]$segmentId)) {
            $skippedObjs.Add($obj.Name) | Out-Null
            continue
        }

        $reportFileName = ("wall_audit_seg_{0:D3}.html" -f $segmentId)
        $reportHtmlPath = Join-Path $ReportDir $reportFileName
        $args = @(
            $auditScriptPath,
            "--obj", $obj.FullName,
            "--segment-id", $segmentId,
            "--segments-map", $SegmentsMapPath,
            "--report-html", $reportHtmlPath
        )
        if (-not [string]::IsNullOrWhiteSpace($SfMapPath) -and (Test-Path -LiteralPath $SfMapPath)) {
            $args += @("--sfmap", $SfMapPath)
        }

        $outputLines = @(& $pythonCmd.Source @args 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw ("Falha na auditoria offline do segmento {0:D3}: {1}" -f $segmentId, ($outputLines -join [Environment]::NewLine))
        }

        $metrics = @{
            vertical_candidates = 0
            wall_segments = 0
            duplicates = 0
            deg_faces = 0
            deg_segments = 0
            zero_normals = 0
            driveable_faces = 0
            non_driveable_faces = 0
            family_mismatch = $false
        }
        foreach ($line in $outputLines) {
            $text = [string]$line
            $kv = Get-KeyValueTokens -Line $text
            foreach ($key in @("vertical_candidates","wall_segments","duplicates","deg_faces","deg_segments","zero_normals","driveable_faces","non_driveable_faces")) {
                if ($kv.ContainsKey($key) -and ($kv[$key] -match '^\d+$')) {
                    $metrics[$key] = [int]$kv[$key]
                }
            }
            if ($kv.ContainsKey("family_mismatch")) {
                $metrics.family_mismatch = ConvertTo-BoolValue ([string]$kv["family_mismatch"])
            }
        }

        $row = [pscustomobject]@{
            SegmentId = $segmentId
            ObjName = $obj.Name
            ObjPath = $obj.FullName
            VerticalCandidates = [int]$metrics.vertical_candidates
            WallSegments = [int]$metrics.wall_segments
            Duplicates = [int]$metrics.duplicates
            DegFaces = [int]$metrics.deg_faces
            DegSegments = [int]$metrics.deg_segments
            ZeroNormals = [int]$metrics.zero_normals
            DriveableFaces = [int]$metrics.driveable_faces
            NonDriveableFaces = [int]$metrics.non_driveable_faces
            FamilyMismatch = [bool]$metrics.family_mismatch
            ReportPath = $reportHtmlPath
            ReportFileName = $reportFileName
        }
        $rows.Add($row) | Out-Null

        if ($Strict -and ($row.FamilyMismatch -or $row.DegFaces -gt 0 -or $row.DegSegments -gt 0)) {
            $strictFailures.Add(("seg_{0:D3}: mismatch={1} deg_faces={2} deg_segments={3}" -f $row.SegmentId, $row.FamilyMismatch, $row.DegFaces, $row.DegSegments)) | Out-Null
        }
    }

    $rowsArray = @($rows.ToArray() | Sort-Object SegmentId)
    $rowsArray | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8
    New-WallAuditSummaryHtml -OutPath $summaryHtmlPath -AuditedSourceDir $ObjRootDir -Rows $rowsArray

    $dupTotal = ($rowsArray | Measure-Object -Property Duplicates -Sum).Sum
    $degFaceTotal = ($rowsArray | Measure-Object -Property DegFaces -Sum).Sum
    $degSegTotal = ($rowsArray | Measure-Object -Property DegSegments -Sum).Sum
    $mismatchCount = @($rowsArray | Where-Object { $_.FamilyMismatch }).Count

    Write-Host ("Wall audit concluida: segs={0} dup={1} deg_faces={2} deg_segments={3} mismatch={4}" -f
        $rowsArray.Count, $dupTotal, $degFaceTotal, $degSegTotal, $mismatchCount)
    if ($skippedObjs.Count -gt 0) {
        Write-Warning ("Wall audit ignorou {0} OBJ(s) sem entrada no segments_map: {1}" -f
            $skippedObjs.Count,
            (($skippedObjs | Select-Object -First 12) -join ", "))
    }
    Write-Host ("Wall audit reports: {0}" -f $summaryHtmlPath)

    if ($Strict -and $strictFailures.Count -gt 0) {
        $strictFailures | ForEach-Object { Write-Host (" - " + $_) }
        throw "Wall audit strict encontrou problemas estruturais."
    }
}

$SourceObjDir = Resolve-DefaultSourceObjDir -RequestedSourceObjDir $SourceObjDir -ResultRootDir $ResultDir
if (-not (Test-Path -LiteralPath $SourceObjDir)) { throw "SourceObjDir nao encontrado: $SourceObjDir" }
if (-not (Test-Path -LiteralPath $PackageDir)) { New-Item -Path $PackageDir -ItemType Directory -Force | Out-Null }
if (-not (Test-Path -LiteralPath $CdDataDir)) { New-Item -Path $CdDataDir -ItemType Directory -Force | Out-Null }
if ($AuditWallsStrict) { $AuditWalls = $true }
if ($AuditWalls -and [string]::IsNullOrWhiteSpace($AuditWallsReportDir)) {
    $AuditWallsReportDir = Join-Path $PackageDir "wall_audit"
}

$sourceObjCount = @(Get-ChildItem -LiteralPath $SourceObjDir -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue).Count
if ($sourceObjCount -le 0) {
    throw ("Nenhum OBJ encontrado em SourceObjDir={0} com Pattern={1}" -f $SourceObjDir, $Pattern)
}

Write-Host ("Build config: SourceObjDir={0} (objs:{1})" -f $SourceObjDir, $sourceObjCount)
Write-Host ("Build config: ResultDir={0}" -f $ResultDir)
Write-Host ("Build config: PackageDir={0}" -f $PackageDir)
Write-Host ("Build config: CdDataDir={0}" -f $CdDataDir)

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
$script:canonicalizeSegmentsMapScript = Join-Path $scriptDir "canonicalize_segments_map_texture_families.ps1"
$script:minifyJsonScript = Join-Path $scriptDir "minify_json.py"
$script:seamOwnershipScript = Join-Path $scriptDir "build_seam_face_ownership.ps1"

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
if (-not (Test-Path -LiteralPath $script:canonicalizeSegmentsMapScript)) { throw "Script nao encontrado: $script:canonicalizeSegmentsMapScript" }
if (-not (Test-Path -LiteralPath $script:minifyJsonScript)) { throw "Script nao encontrado: $script:minifyJsonScript" }
if ($EnableSeamFaceDedup -and -not (Test-Path -LiteralPath $script:seamOwnershipScript)) {
    Write-Warning ("Script de seam dedup nao encontrado: {0}. Etapa sera ignorada." -f $script:seamOwnershipScript)
    $EnableSeamFaceDedup = $false
}

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

Write-Host "=== Etapa 2.5/7: Canonizar aliases de textura no segments_map ==="
& $script:canonicalizeSegmentsMapScript `
    -SegmentsMapPath $jsonPath

function Test-SegmentsMapFamilyReferences {
    param(
        [string]$SegmentsMapPath
    )

    $json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
    if ($null -eq $json) {
        throw "Nao foi possivel carregar o segments_map para validacao: $SegmentsMapPath"
    }

    $knownFamilyIds = New-Object 'System.Collections.Generic.HashSet[int]'
    foreach ($family in @($json.textureFamilies)) {
        if ($null -eq $family) { continue }
        [void]$knownFamilyIds.Add([int]$family.id)
    }

    $errors = New-Object System.Collections.Generic.List[string]
    $seenErrors = New-Object 'System.Collections.Generic.HashSet[string]'
    $emitError = {
        param([string]$Message)
        if ($seenErrors.Add($Message)) {
            $errors.Add($Message) | Out-Null
        }
    }

    foreach ($segment in @($json.segments)) {
        if ($null -eq $segment) { continue }
        $segmentId = [int]$segment.id

        foreach ($familyId in @($segment.faceTextureFamily)) {
            $fid = [int]$familyId
            if ($fid -le 0 -or $knownFamilyIds.Contains($fid)) { continue }
            & $emitError ("seg {0}: faceTextureFamily -> familyId {1} inexistente" -f $segmentId, $fid)
        }

        foreach ($rle in @($segment.faceTextureFamilyRle)) {
            if ($null -eq $rle -or -not ($rle.PSObject.Properties.Name -contains "familyId")) { continue }
            $fid = [int]$rle.familyId
            if ($fid -le 0 -or $knownFamilyIds.Contains($fid)) { continue }
            & $emitError ("seg {0}: faceTextureFamilyRle -> familyId {1} inexistente" -f $segmentId, $fid)
        }

        if ($segment.PSObject.Properties.Name -contains "faces" -and $segment.faces) {
            foreach ($face in @($segment.faces)) {
                if ($null -eq $face -or -not ($face.PSObject.Properties.Name -contains "familyId")) { continue }
                $fid = [int]$face.familyId
                if ($fid -le 0 -or $knownFamilyIds.Contains($fid)) { continue }
                & $emitError ("seg {0}: faces[].familyId -> {1} inexistente" -f $segmentId, $fid)
            }
        }

        if ($segment.PSObject.Properties.Name -contains "meshes" -and $segment.meshes) {
            foreach ($mesh in @($segment.meshes)) {
                if ($null -eq $mesh -or -not ($mesh.PSObject.Properties.Name -contains "textureFamilies")) { continue }
                foreach ($familyId in @($mesh.textureFamilies)) {
                    $fid = [int]$familyId
                    if ($fid -le 0 -or $knownFamilyIds.Contains($fid)) { continue }
                    & $emitError ("seg {0}: meshes[].textureFamilies -> familyId {1} inexistente" -f $segmentId, $fid)
                }
            }
        }
    }

    if ($errors.Count -gt 0) {
        Write-Host "Falha de consistencia em segments_map.json:"
        $errors | Select-Object -First 32 | ForEach-Object { Write-Host (" - " + $_) }
        if ($errors.Count -gt 32) {
            Write-Host (" - ... mais {0} erro(s)" -f ($errors.Count - 32))
        }
        throw "segments_map.json contem referencias a familyId inexistente(s)."
    }

    Write-Host ("segments_map refs OK: segs={0} families={1}" -f @($json.segments).Count, @($json.textureFamilies).Count)
}

function New-SurfaceTypeRle {
    param(
        [int[]]$Values
    )
    $runs = New-Object System.Collections.Generic.List[object]
    if (-not $Values -or $Values.Count -eq 0) { return @() }

    $start = 0
    $curr = [int]$Values[0]
    $count = 1
    for ($i = 1; $i -lt $Values.Count; $i++) {
        $v = [int]$Values[$i]
        if ($v -eq $curr) {
            $count++
            continue
        }
        $runs.Add([pscustomobject]@{ start = $start; count = $count; surfaceTypeId = $curr }) | Out-Null
        $start = $i
        $curr = $v
        $count = 1
    }
    $runs.Add([pscustomobject]@{ start = $start; count = $count; surfaceTypeId = $curr }) | Out-Null
    return @($runs.ToArray())
}

function Convert-SurfaceTypeNameToId {
    param(
        [string]$Name
    )
    if ([string]::IsNullOrWhiteSpace($Name)) { return 0 }
    switch ($Name.Trim().ToLowerInvariant()) {
        "asphalt" { return 1 }
        "asfalto" { return 1 }
        "escape" { return 2 }
        "escapearea" { return 2 }
        "escape_area" { return 2 }
        "grass" { return 3 }
        "grama" { return 3 }
        default { return 0 }
    }
}

function Convert-SurfaceTypeIdToName {
    param(
        [int]$Id
    )
    switch ($Id) {
        1 { return "asphalt" }
        2 { return "escape_area" }
        3 { return "grass" }
        default { return "unknown" }
    }
}

function Normalize-SurfaceStem {
    param(
        [string]$Token
    )
    if ([string]::IsNullOrWhiteSpace($Token)) { return "" }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Token).Trim().ToLowerInvariant()
    if ($base -match '^(?<name>.+)_(8|16|32|64)(\..+)?$') {
        return $Matches['name']
    }
    if ($base -match '^(?<name>.+)\.\d+$') {
        return $Matches['name']
    }
    return $base
}

function Resolve-SurfaceTypeIdFromFamily {
    param(
        $Family
    )
    if ($null -eq $Family) { return 0 }

    if ($Family.PSObject.Properties.Name -contains "surfaceTypeId") {
        $explicit = [int]$Family.surfaceTypeId
        if ($explicit -ge 0 -and $explicit -le 255) { return $explicit }
    }
    if ($Family.PSObject.Properties.Name -contains "surfaceType") {
        $fromName = Convert-SurfaceTypeNameToId ([string]$Family.surfaceType)
        if ($fromName -gt 0) { return $fromName }
    }

    $stem = ""
    if ($Family.PSObject.Properties.Name -contains "sourceStem") {
        $stem = Normalize-SurfaceStem ([string]$Family.sourceStem)
    }
    if ([string]::IsNullOrWhiteSpace($stem) -and
        $Family.PSObject.Properties.Name -contains "variants" -and
        $Family.variants -and
        $Family.variants.PSObject.Properties.Name -contains "64") {
        $stem = Normalize-SurfaceStem ([string]$Family.variants."64")
    }
    if ([string]::IsNullOrWhiteSpace($stem) -and
        $Family.PSObject.Properties.Name -contains "imageFiles" -and
        $Family.imageFiles -and
        $Family.imageFiles.PSObject.Properties.Name -contains "64") {
        $stem = Normalize-SurfaceStem ([string]$Family.imageFiles."64")
    }
    if ([string]::IsNullOrWhiteSpace($stem) -and
        $Family.PSObject.Properties.Name -contains "name") {
        $stem = Normalize-SurfaceStem ([string]$Family.name)
    }

    $asphaltStems = @("f01064", "f04664", "f04764", "f05964", "f06064", "f06164", "f06264", "f06364", "asfalto", "roadpit", "roadgrid")
    $escapeStems = @("f01864", "f00164", "f00264", "f00364", "f00464", "f00564")
    $grassStems = @("f06864", "f04364", "f02564", "f02464", "f02364")

    if ($asphaltStems -contains $stem) { return 1 }
    if ($escapeStems -contains $stem) { return 2 }
    if ($grassStems -contains $stem) { return 3 }
    return 0
}

function Annotate-SegmentsMapSurfaceTypes {
    param(
        [string]$SegmentsMapPath
    )
    if (-not (Test-Path -LiteralPath $SegmentsMapPath)) {
        throw "segments_map inexistente para anotacao de solo: $SegmentsMapPath"
    }

    $json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
    if ($null -eq $json) {
        throw "Nao foi possivel carregar JSON para anotacao de solo: $SegmentsMapPath"
    }

    $familySurfaceTypeById = @{}
    foreach ($family in @($json.textureFamilies)) {
        if ($null -eq $family) { continue }
        if (-not ($family.PSObject.Properties.Name -contains "id")) { continue }
        $fid = [int]$family.id
        if ($fid -le 0) { continue }
        $stype = Resolve-SurfaceTypeIdFromFamily -Family $family
        $family | Add-Member -NotePropertyName surfaceTypeId -NotePropertyValue ([int]$stype) -Force
        $family | Add-Member -NotePropertyName surfaceType -NotePropertyValue (Convert-SurfaceTypeIdToName -Id $stype) -Force
        $familySurfaceTypeById[$fid] = [int]$stype
    }

    $json | Add-Member -NotePropertyName surfaceTypes -NotePropertyValue @(
        [pscustomobject]@{ id = 0; name = "unknown" },
        [pscustomobject]@{ id = 1; name = "asphalt" },
        [pscustomobject]@{ id = 2; name = "escape_area" },
        [pscustomobject]@{ id = 3; name = "grass" }
    ) -Force

    foreach ($segment in @($json.segments)) {
        if ($null -eq $segment) { continue }
        $faceSurfaceType = New-Object System.Collections.Generic.List[int]

        if ($segment.PSObject.Properties.Name -contains "faces" -and $segment.faces) {
            foreach ($face in @($segment.faces)) {
                if ($null -eq $face) { continue }
                $fid = 0
                if ($face.PSObject.Properties.Name -contains "familyId") {
                    $fid = [int]$face.familyId
                }
                $stype = 0
                if ($familySurfaceTypeById.ContainsKey($fid)) {
                    $stype = [int]$familySurfaceTypeById[$fid]
                }
                $face | Add-Member -NotePropertyName surfaceTypeId -NotePropertyValue ([int]$stype) -Force
                $face | Add-Member -NotePropertyName surfaceType -NotePropertyValue (Convert-SurfaceTypeIdToName -Id $stype) -Force
                $faceSurfaceType.Add($stype) | Out-Null
            }
        }
        elseif ($segment.PSObject.Properties.Name -contains "faceTextureFamily" -and $segment.faceTextureFamily) {
            foreach ($familyId in @($segment.faceTextureFamily)) {
                $fid = [int]$familyId
                $stype = 0
                if ($familySurfaceTypeById.ContainsKey($fid)) {
                    $stype = [int]$familySurfaceTypeById[$fid]
                }
                $faceSurfaceType.Add($stype) | Out-Null
            }
        }

        $segment | Add-Member -NotePropertyName faceSurfaceType -NotePropertyValue @($faceSurfaceType.ToArray()) -Force
        $segment | Add-Member -NotePropertyName faceSurfaceTypeRle -NotePropertyValue @(New-SurfaceTypeRle -Values @($faceSurfaceType.ToArray())) -Force
    }

    $json | ConvertTo-Json -Depth 16 -Compress | Set-Content -LiteralPath $SegmentsMapPath -Encoding UTF8
    Write-Host ("surface types anotados em: {0}" -f $SegmentsMapPath)
}

function Write-SurfaceFamilyMapBinary {
    param(
        [string]$SegmentsMapPath,
        [string]$OutBinPath
    )

    if (-not (Test-Path -LiteralPath $SegmentsMapPath)) {
        throw "segments_map inexistente para mapa de superficie: $SegmentsMapPath"
    }

    $json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
    if ($null -eq $json) {
        throw "Nao foi possivel carregar JSON para mapa de superficie: $SegmentsMapPath"
    }

    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($family in @($json.textureFamilies)) {
        if ($null -eq $family) { continue }
        if (-not ($family.PSObject.Properties.Name -contains "id")) { continue }
        $fid = [int]$family.id
        if ($fid -le 0 -or $fid -gt 4095) { continue }

        $stype = 0
        if ($family.PSObject.Properties.Name -contains "surfaceTypeId") {
            $stype = [int]$family.surfaceTypeId
        }
        elseif ($family.PSObject.Properties.Name -contains "surfaceType") {
            $stype = Convert-SurfaceTypeNameToId ([string]$family.surfaceType)
        }

        $mask = [byte]0
        if ($stype -ge 1 -and $stype -le 7) {
            $mask = [byte](1 -shl $stype)
        }

        $entries.Add([pscustomobject]@{
            familyId = [uint16]$fid
            surfaceTypeId = [byte]$stype
            surfaceMask = [byte]$mask
        }) | Out-Null
    }

    $ordered = @($entries.ToArray() | Sort-Object familyId)
    $outDir = Split-Path -Parent $OutBinPath
    if (-not [string]::IsNullOrWhiteSpace($outDir) -and -not (Test-Path -LiteralPath $outDir)) {
        New-Item -Path $outDir -ItemType Directory -Force | Out-Null
    }

    $fs = [System.IO.File]::Open($OutBinPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        try {
            # Header SFM1 (little-endian), version 1
            $bw.Write([uint32]0x314D4653) # "SFM1"
            $bw.Write([uint16]1)
            $bw.Write([uint16]0)
            $bw.Write([uint32]$ordered.Count)
            foreach ($entry in $ordered) {
                $bw.Write([uint16]$entry.familyId)
                $bw.Write([byte]$entry.surfaceTypeId)
                $bw.Write([byte]$entry.surfaceMask)
            }
        }
        finally {
            $bw.Dispose()
        }
    }
    finally {
        $fs.Dispose()
    }

    Write-Host ("surface family map gerado: {0} entries={1}" -f $OutBinPath, $ordered.Count)
}

function Write-SegmentCollisionMapBinary {
    param(
        [string]$SegmentsMapPath,
        [string]$OutBinPath
    )

    if (-not (Test-Path -LiteralPath $SegmentsMapPath)) {
        throw "segments_map inexistente para mapa de colisao: $SegmentsMapPath"
    }

    $json = Get-Content -LiteralPath $SegmentsMapPath -Raw | ConvertFrom-Json
    if ($null -eq $json) {
        throw "Nao foi possivel carregar JSON para mapa de colisao: $SegmentsMapPath"
    }

    $segments = @($json.segments | Sort-Object { [int]$_.id })
    $outDir = Split-Path -Parent $OutBinPath
    if (-not [string]::IsNullOrWhiteSpace($outDir) -and -not (Test-Path -LiteralPath $outDir)) {
        New-Item -Path $outDir -ItemType Directory -Force | Out-Null
    }

    $fs = [System.IO.File]::Open($OutBinPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    try {
        $bw = New-Object System.IO.BinaryWriter($fs)
        try {
            # Header SCM1 (little-endian), version 1
            $bw.Write([uint32]0x314D4353) # "SCM1"
            $bw.Write([uint16]1)
            $bw.Write([uint16]0)
            $bw.Write([uint32]$segments.Count)

            foreach ($segment in $segments) {
                if ($null -eq $segment) { continue }
                $segmentId = [int]$segment.id
                $faceTypes = @()
                if ($segment.PSObject.Properties.Name -contains "faceSurfaceType" -and $segment.faceSurfaceType) {
                    $faceTypes = @($segment.faceSurfaceType | ForEach-Object { [int]$_ })
                }
                elseif ($segment.PSObject.Properties.Name -contains "faceTextureFamily" -and $segment.faceTextureFamily) {
                    # Fallback conservador.
                    $faceTypes = @($segment.faceTextureFamily | ForEach-Object { 0 })
                }

                $rle = @()
                if ($segment.PSObject.Properties.Name -contains "faceSurfaceTypeRle" -and $segment.faceSurfaceTypeRle) {
                    $rle = @($segment.faceSurfaceTypeRle)
                }
                else {
                    $rle = @(New-SurfaceTypeRle -Values $faceTypes)
                }

                $bw.Write([uint16]$segmentId)
                $bw.Write([uint16]$faceTypes.Count)
                $bw.Write([uint32]$rle.Count)

                foreach ($run in $rle) {
                    $start = 0
                    $count = 0
                    $stype = 0
                    if ($run.PSObject.Properties.Name -contains "start") { $start = [int]$run.start }
                    if ($run.PSObject.Properties.Name -contains "count") { $count = [int]$run.count }
                    if ($run.PSObject.Properties.Name -contains "surfaceTypeId") { $stype = [int]$run.surfaceTypeId }

                    [byte]$flags = 0
                    if ($stype -ge 1 -and $stype -le 3) { $flags = [byte]($flags -bor 0x01) } # driveable
                    if ($stype -eq 1) { $flags = [byte]($flags -bor 0x02) } # asphalt-like
                    if ($stype -eq 2 -or $stype -eq 3) { $flags = [byte]($flags -bor 0x04) } # offroad-like
                    if (-not ($stype -ge 1 -and $stype -le 3)) { $flags = [byte]($flags -bor 0x08) } # has wall-like faces

                    $bw.Write([uint16]$start)
                    $bw.Write([uint16]$count)
                    $bw.Write([byte]$stype)
                    $bw.Write([byte]$flags)
                    $bw.Write([uint16]0) # reservado
                }
            }
        }
        finally {
            $bw.Dispose()
        }
    }
    finally {
        $fs.Dispose()
    }

    Write-Host ("segment collision map gerado: {0} segs={1}" -f $OutBinPath, $segments.Count)
}

Write-Host "=== Etapa 2.6/7: Renomear/copiar texturas com sufixo ==="
& $script:copyRenScript `
    -DataDir $PackageDir `
    -TextOutDir $CdDataDir `
    -ResultDir $ResultDir

Write-Host "=== Etapa 2.7/7: Atualizar segments_map com texturas renomeadas ==="
$renManifestPath = Join-Path $PackageDir "ren_textures_copy_map.json"
& $script:updateSegmentsMapScript `
    -SegmentsMapPath $jsonPath `
    -RenManifestPath $renManifestPath `
    -ResultDir $ResultDir

Write-Host "=== Etapa 2.8/7: Recanonizar families apos remap de texturas ==="
& $script:canonicalizeSegmentsMapScript `
    -SegmentsMapPath $jsonPath

Write-Host "=== Etapa 2.9/7: Validar referencias de familyId no segments_map ==="
Test-SegmentsMapFamilyReferences -SegmentsMapPath $jsonPath
Write-Host "=== Etapa 2.95/7: Anotar tipo de solo por face ==="
Annotate-SegmentsMapSurfaceTypes -SegmentsMapPath $jsonPath
if ($ExportSurfaceFamilyMap) {
    Write-Host "=== Etapa 2.96/7: Gerar mapa compacto de superficie por family ==="
    $surfaceMapBinPath = Join-Path $PackageDir "SFMAP.BIN"
    Write-SurfaceFamilyMapBinary -SegmentsMapPath $jsonPath -OutBinPath $surfaceMapBinPath
    Copy-Item -LiteralPath $surfaceMapBinPath -Destination (Join-Path $CdDataDir "SFMAP.BIN") -Force
    Write-Host "=== Etapa 2.97/7: Gerar mapa de colisao por segmento ==="
    $segmentCollisionMapPath = Join-Path $PackageDir "SCMAP.BIN"
    Write-SegmentCollisionMapBinary -SegmentsMapPath $jsonPath -OutBinPath $segmentCollisionMapPath
    Copy-Item -LiteralPath $segmentCollisionMapPath -Destination (Join-Path $CdDataDir "SCMAP.BIN") -Force
}
if ($AuditWalls) {
    Write-Host "=== Etapa 2.975/7: Auditoria offline de WallSegment2D ==="
    $surfaceMapForAudit = Join-Path $CdDataDir "SFMAP.BIN"
    if (-not (Test-Path -LiteralPath $surfaceMapForAudit)) {
        Write-Warning ("SFMAP.BIN nao encontrado em {0}; auditoria rodara sem filtro de solo." -f $surfaceMapForAudit)
        $surfaceMapForAudit = ""
    }
    Invoke-WallAuditBatch `
        -ObjRootDir $SourceObjDir `
        -SegmentsMapPath $jsonPath `
        -SfMapPath $surfaceMapForAudit `
        -Pattern $Pattern `
        -ReportDir $AuditWallsReportDir `
        -Strict:$AuditWallsStrict
}

Write-Host "=== Etapa 3/7: Gerar GEO/MAT (3 Levels of Design: lod_0/1/2) ==="
# Fase 2 dual mesh:
# - lod_0: S###.GEO + S###M64/M32 (alta, ranks 0-1 + MapHeight authority)
# - lod_1: S###L.GEO + S###LM64 (media, ranks 2-9)
# - lod_2: S###LM32 (far tex, ranks 10-19; reusa malha L)
$lod0Dir = Join-Path $ResultDir "lod_0"
$lod1Dir = Join-Path $ResultDir "lod_1"
$lod2Dir = Join-Path $ResultDir "lod_2"
$legacy64 = Join-Path $ResultDir "obj_64"
$legacy32 = Join-Path $ResultDir "obj_32"

$designPasses = @()
if (Test-Path -LiteralPath $lod0Dir) {
    $designPasses += @(
        @{ Name = "lod_0"; Dir = $lod0Dir; Lod = 64; SkipGeo = $false; AssetTag = "" },
        @{ Name = "lod_0"; Dir = $lod0Dir; Lod = 32; SkipGeo = $true;  AssetTag = "" }
    )
}
elseif (Test-Path -LiteralPath $legacy64) {
    Write-Host "Aviso: lod_0 ausente; usando legacy obj_64 como malha densa."
    $designPasses += @(
        @{ Name = "obj_64"; Dir = $legacy64; Lod = 64; SkipGeo = $false; AssetTag = "" },
        @{ Name = "obj_64"; Dir = $legacy64; Lod = 32; SkipGeo = $true;  AssetTag = "" }
    )
}
else {
    throw "Nenhuma pasta lod_0 (nem obj_64) em $ResultDir"
}

$lowDir = $null
if (Test-Path -LiteralPath $lod1Dir) { $lowDir = $lod1Dir }
elseif (Test-Path -LiteralPath $legacy32) { $lowDir = $legacy32 }

if ($null -ne $lowDir) {
    $designPasses += @(
        @{ Name = "lod_1"; Dir = $lowDir; Lod = 64; SkipGeo = $false; AssetTag = "L" },
        @{ Name = "lod_1"; Dir = $lowDir; Lod = 32; SkipGeo = $true;  AssetTag = "L" }
    )
}
# lod_2 textures: if separate folder with same mesh as lod_1, rewrite LM32 from lod_2 ARQ materials
if ((Test-Path -LiteralPath $lod2Dir) -and ($lod2Dir -ne $lowDir)) {
    $designPasses += @{ Name = "lod_2"; Dir = $lod2Dir; Lod = 32; SkipGeo = $true; AssetTag = "L" }
}

$componentScriptPath = $script:componentScript
if (-not (Test-Path -LiteralPath $componentScriptPath)) {
    throw "Script de componente nao encontrado: $componentScriptPath"
}

$seamOwnershipPath = ""
if ($EnableSeamFaceDedup) {
    Write-Host "=== Etapa 2.98/7: Gerar ownership de faces de costura ==="
    $seamOwnershipPath = Join-Path $PackageDir "seam_face_ownership.json"
    if (Test-Path -LiteralPath $script:seamOwnershipScript) {
        & $script:seamOwnershipScript `
            -ResultDir $ResultDir `
            -Pattern $Pattern `
            -OutJsonPath $seamOwnershipPath
    }
    else {
        Write-Host "Aviso: seam ownership script ausente; seguindo sem dedup de costura."
        $seamOwnershipPath = ""
    }
}

function Get-GeoFaceCount([string]$GeoPath) {
    if (-not (Test-Path -LiteralPath $GeoPath)) { return -1 }
    [byte[]]$b = [System.IO.File]::ReadAllBytes($GeoPath)
    if ($b.Length -lt 24) { return -1 }
    return [int][System.BitConverter]::ToUInt32($b, 20) # faces after verts count at offset 16: verts@16 faces@20
}

$segmentsDone = New-Object System.Collections.Generic.HashSet[int]
foreach ($entry in $designPasses) {
    if (-not (Test-Path -LiteralPath $entry.Dir)) {
        Write-Host "Aviso: pasta design ausente, pulando: $($entry.Dir)"
        continue
    }
    $assetTag = if ($entry.ContainsKey("AssetTag")) { [string]$entry.AssetTag } else { "" }
    Write-Host ("--- Design {0} Lod={1} SkipGeo={2} Tag='{3}' ---" -f $entry.Name, $entry.Lod, [bool]$entry.SkipGeo, $assetTag)
    $objs = Get-ChildItem -LiteralPath $entry.Dir -Filter $Pattern | Sort-Object Name
    foreach ($obj in $objs) {
        $id = Get-SegmentIdFromFile $obj.BaseName
        if ($null -eq $id) { continue }
        $segmentsDone.Add($id) | Out-Null
        try {
            $argList = @{
                SegmentId = $id
                ObjDir = $entry.Dir
                JsonPath = $jsonPath
                OutDir = $PackageDir
                Lod = $entry.Lod
                SeamOwnershipPath = $seamOwnershipPath
                AssetTag = $assetTag
            }
            if ($entry.SkipGeo) {
                & $componentScriptPath @argList -SkipGeo
            }
            else {
                & $componentScriptPath @argList
            }
        }
        catch {
            Write-Host ("Falha {0} LOD{1} SEG_{2:D3}: {3}" -f $entry.Name, $entry.Lod, $id, $_.Exception.Message)
        }
    }
}

Write-Host ("Concluido GEO/MAT design LOD: {0} segmentos distintos" -f $segmentsDone.Count)

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

Write-Host "=== Etapa 3.5/7: Gerar segmentos draw-ready SDR1 (high + low) ==="
# High mesh SDR (lod_0)
& $script:sdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -Lod 64 `
    -AssetTag "" `
    -AllSegments `
    -SegmentsMapPath $jsonPath `
    -CanonicalizeQuadUvOrder `
    -QuadUvEdgeTolerance 256 `
    -QuadUvHighTolerance 1024 `
    -CanonicalizeHighToleranceAllFamilies
# Low mesh SDR (lod_1) if present
$lowGeoSample = @(Get-ChildItem -LiteralPath $PackageDir -File -Filter "S???L.GEO" -ErrorAction SilentlyContinue)
if ($lowGeoSample.Count -gt 0) {
    & $script:sdrScript `
        -DataDir $PackageDir `
        -OutDir $PackageDir `
        -Lod 64 `
        -AssetTag "L" `
        -AllSegments `
        -SegmentsMapPath $jsonPath `
        -CanonicalizeQuadUvOrder `
        -QuadUvEdgeTolerance 256 `
        -QuadUvHighTolerance 1024 `
        -CanonicalizeHighToleranceAllFamilies
}

Write-Host "=== Etapa 3.6/7: Gerar blobs runtime RDR1 (high + low) ==="
& $script:rdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -AssetTag "" `
    -AllSegments
if ($lowGeoSample.Count -gt 0) {
    & $script:rdrScript `
        -DataDir $PackageDir `
        -OutDir $PackageDir `
        -AssetTag "L" `
        -AllSegments
}

Write-Host "=== Etapa 3.7/7: Gerar batches draw-ready BDR1 ==="
& $script:bdrScript `
    -DataDir $PackageDir `
    -OutDir $PackageDir `
    -BatchSize 2 `
    -AllBatches

Write-Host "=== Etapa 4/7: Minificar segments_map.json ==="
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

Write-Host "=== Etapa 4.1/7: Criar aliases 8.3 para segments_map ==="
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

Write-Host "=== Etapa 7.1/7: Gerar pack runtime TRKRDR.BIN (high) + TRKRDRL.BIN (low) ==="
& $script:trkRdrPackScript `
    -DataDir $PackageDir `
    -OutPath (Join-Path $CdDataDir "TRKRDR.BIN") `
    -AssetTag ""
$lowRdrSample = @(Get-ChildItem -LiteralPath $PackageDir -File -Filter "S???L.RDR" -ErrorAction SilentlyContinue)
if ($lowRdrSample.Count -gt 0) {
    & $script:trkRdrPackScript `
        -DataDir $PackageDir `
        -OutPath (Join-Path $CdDataDir "TRKRDRL.BIN") `
        -AssetTag "L"
}

Write-Host "=== Limpeza: Remover auxiliares de cd/data ==="
Remove-CdDataAuxFiles -TargetDirs $targetDirs

Write-Host "=== Validacao final ==="
$expectedSegmentIds = @($segmentsDone | Sort-Object -Unique)
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
$hasTrkRdrLowBin = Test-Path -LiteralPath (Join-Path $CdDataDir "TRKRDRL.BIN")
$hasSurfaceFamilyMapBin = Test-Path -LiteralPath (Join-Path $CdDataDir "SFMAP.BIN")
$hasSegmentCollisionMapBin = Test-Path -LiteralPath (Join-Path $CdDataDir "SCMAP.BIN")
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
Write-Host ("HAS TRKRDRL.BIN   : {0}" -f $hasTrkRdrLowBin)
Write-Host ("HAS SFMAP.BIN     : {0}" -f $hasSurfaceFamilyMapBin)
Write-Host ("HAS SCMAP.BIN     : {0}" -f $hasSegmentCollisionMapBin)
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
if ($ExportSurfaceFamilyMap -and -not $hasSurfaceFamilyMapBin) { $validationErrors.Add("SFMAP.BIN ausente em cd\\data") | Out-Null }
if ($ExportSurfaceFamilyMap -and -not $hasSegmentCollisionMapBin) { $validationErrors.Add("SCMAP.BIN ausente em cd\\data") | Out-Null }
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
    Write-Host "=== Reaplicar anotacao de tipo de solo apos rebuild ==="
    Annotate-SegmentsMapSurfaceTypes -SegmentsMapPath $jsonPath
    if ($ExportSurfaceFamilyMap) {
        Write-Host "=== Regerar mapa compacto de superficie apos rebuild ==="
        $surfaceMapBinPath = Join-Path $PackageDir "SFMAP.BIN"
        Write-SurfaceFamilyMapBinary -SegmentsMapPath $jsonPath -OutBinPath $surfaceMapBinPath
        Copy-Item -LiteralPath $surfaceMapBinPath -Destination (Join-Path $CdDataDir "SFMAP.BIN") -Force
        Write-Host "=== Regerar mapa de colisao por segmento apos rebuild ==="
        $segmentCollisionMapPath = Join-Path $PackageDir "SCMAP.BIN"
        Write-SegmentCollisionMapBinary -SegmentsMapPath $jsonPath -OutBinPath $segmentCollisionMapPath
        Copy-Item -LiteralPath $segmentCollisionMapPath -Destination (Join-Path $CdDataDir "SCMAP.BIN") -Force
    }
    Write-Host "=== Validando segments_map final apos rebuild ==="
    Test-SegmentsMapFamilyReferences -SegmentsMapPath $jsonPath
    Write-Host "=== Minificando segments_map final apos rebuild ==="
    & python $script:minifyJsonScript $jsonPath
    Write-Host "=== Recriando aliases 8.3 apos rebuild final ==="
    Copy-SegmentsMapShortNames -Source $jsonPath -TargetDirs $targetDirs
    Write-Host "=== Limpeza final: Remover auxiliares de cd/data apos rebuild ==="
    Remove-CdDataAuxFiles -TargetDirs $targetDirs
} else {
    Write-Host "segments_map.json mantido (rebuild desativado)."
}
