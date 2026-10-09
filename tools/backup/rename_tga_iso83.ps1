param(
    [string]$TextureRoot = "C:\Users\clebe\OneDrive\Ãrea de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA",
    [string]$OutMapJson = "C:\Users\clebe\OneDrive\Ãrea de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA\tga_rename_map.json",
    [switch]$Recurse,
    [switch]$UppercaseExt = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $TextureRoot)) {
    throw "TextureRoot nao encontrado: $TextureRoot"
}

if ($Recurse) {
    throw "Recurse desabilitado neste script. Ele renomeia apenas arquivos .tga na pasta raiz de TextureRoot."
}

function Get-FamilyAndLod {
    param([string]$Stem)
    $m = [regex]::Match($Stem, '^(?<family>.+?)_(?<lod>8|16|32|64)$', 'IgnoreCase')
    if ($m.Success) {
        return [pscustomobject]@{
            Family = $m.Groups['family'].Value.ToLowerInvariant()
            Lod    = [int]$m.Groups['lod'].Value
        }
    }
    return [pscustomobject]@{
        Family = $Stem.ToLowerInvariant()
        Lod    = 0
    }
}

function Build-ShortName {
    param(
        [int]$FamilyId,
        [int]$Lod,
        [bool]$UpperExt
    )

    # Base <= 8 chars (ISO 8.3)
    # lod=8  -> F001_8
    # lod=16 -> F00116
    # lod=32 -> F00132
    # lod=64 -> F00164
    # lod=0  -> F001
    $base = switch ($Lod) {
        8  { ("F{0:D3}_8"  -f $FamilyId) }
        16 { ("F{0:D3}16"  -f $FamilyId) }
        32 { ("F{0:D3}32"  -f $FamilyId) }
        64 { ("F{0:D3}64"  -f $FamilyId) }
        default { ("F{0:D3}" -f $FamilyId) }
    }
    $ext = if ($UpperExt) { ".TGA" } else { ".tga" }
    return $base + $ext
}

$files = Get-ChildItem -LiteralPath $TextureRoot -File | Where-Object { $_.Extension -ieq ".tga" } | Sort-Object FullName

if ($files.Count -eq 0) {
    throw "Nenhum .tga encontrado em: $TextureRoot"
}

$familyIdByName = @{}
$nextFamilyId = 1

$entries = New-Object System.Collections.Generic.List[object]
$usedTargetByDir = @{}

foreach ($f in $files) {
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
    $parsed = Get-FamilyAndLod -Stem $stem
    $family = [string]$parsed.Family
    $lod = [int]$parsed.Lod

    if (-not $familyIdByName.ContainsKey($family)) {
        $familyIdByName[$family] = $nextFamilyId
        $nextFamilyId++
    }
    $familyId = [int]$familyIdByName[$family]

    $targetName = Build-ShortName -FamilyId $familyId -Lod $lod -UpperExt:$UppercaseExt
    $dirKey = $f.DirectoryName.ToLowerInvariant()
    if (-not $usedTargetByDir.ContainsKey($dirKey)) {
        $usedTargetByDir[$dirKey] = @{}
    }
    if ($usedTargetByDir[$dirKey].ContainsKey($targetName.ToLowerInvariant())) {
        throw "Colisao de nome no diretorio '$($f.DirectoryName)': $targetName"
    }
    $usedTargetByDir[$dirKey][$targetName.ToLowerInvariant()] = $true

    $targetPath = Join-Path $f.DirectoryName $targetName
    $entries.Add([pscustomobject]@{
        old_name = $f.Name
        new_name = $targetName
        old_path = $f.FullName
        new_path = $targetPath
        family = $family
        family_id = $familyId
        lod = $lod
    }) | Out-Null
}

# Rename pass
$renamed = 0
$skipped = 0
foreach ($e in $entries) {
    if ($e.old_name -ieq $e.new_name) {
        $skipped++
        continue
    }
    if (Test-Path -LiteralPath $e.new_path) {
        throw "Destino ja existe: $($e.new_path)"
    }
    Rename-Item -LiteralPath $e.old_path -NewName $e.new_name
    $renamed++
}

$outDir = Split-Path -Parent $OutMapJson
if (-not [string]::IsNullOrWhiteSpace($outDir)) {
    New-Item -Path $outDir -ItemType Directory -Force | Out-Null
}

$itemsArray = @($entries.ToArray())
$familyCount = @($familyIdByName.Keys).Count
$manifest = [ordered]@{
    version = 1
    texture_root = [string]$TextureRoot
    generated_at_utc = [string]([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"))
    file_count = [int]$entries.Count
    renamed_count = [int]$renamed
    skipped_count = [int]$skipped
    families = [int]$familyCount
    items = $itemsArray
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutMapJson -Encoding UTF8

Write-Host ("OK TGA rename: total={0} renamed={1} skipped={2} families={3}" -f $entries.Count, $renamed, $skipped, $familyCount)
Write-Host ("Map: {0}" -f $OutMapJson)

