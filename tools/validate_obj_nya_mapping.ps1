param(
    [Parameter(Mandatory = $true)] [string]$ObjPath,
    [Parameter(Mandatory = $true)] [string]$NyaPath,
    [string]$OutCsv = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-Be32([byte[]]$Data, [int]$Offset) {
    return (($Data[$Offset] -shl 24) -bor ($Data[$Offset + 1] -shl 16) -bor ($Data[$Offset + 2] -shl 8) -bor $Data[$Offset + 3])
}

function Read-Be16([byte[]]$Data, [int]$Offset) {
    return (($Data[$Offset] -shl 8) -bor $Data[$Offset + 1])
}

if (-not (Test-Path $ObjPath)) { throw "OBJ not found: $ObjPath" }
if (-not (Test-Path $NyaPath)) { throw "NYA not found: $NyaPath" }

# Parse OBJ faces (material + vertex indices + uv indices).
$objFaces = @()
$currentMaterial = ""
Get-Content $ObjPath | ForEach-Object {
    $line = $_.Trim()
    if ($line.StartsWith("usemtl ")) {
        $currentMaterial = $line.Substring(7).Trim()
        return
    }
    if ($line.StartsWith("f ")) {
        $tokens = $line.Substring(2).Trim().Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        $verts = @()
        $uvs = @()
        foreach ($t in $tokens) {
            $parts = $t.Split("/")
            if ($parts.Length -gt 0 -and $parts[0] -ne "") { $verts += [int]$parts[0] } else { $verts += -1 }
            if ($parts.Length -gt 1 -and $parts[1] -ne "") { $uvs += [int]$parts[1] } else { $uvs += -1 }
        }
        while ($verts.Count -lt 4) { $verts += $verts[$verts.Count - 1] }
        while ($uvs.Count -lt 4) { $uvs += $uvs[$uvs.Count - 1] }
        $objFaces += [pscustomobject]@{
            FaceIndex = $objFaces.Count
            Material = $currentMaterial
            ObjVerts = ($verts -join ",")
            ObjUvs = ($uvs -join ",")
        }
    }
}

# Parse NYA first mesh faces and flags.
[byte[]]$b = [System.IO.File]::ReadAllBytes($NyaPath)
if ($b.Length -lt 12) { throw "Invalid NYA: too small" }

$type = Read-Be32 $b 0
$meshCount = Read-Be32 $b 4
$texCount = Read-Be32 $b 8
if ($meshCount -lt 1) { throw "Invalid NYA: meshCount=0" }

$off = 12
$pts = Read-Be32 $b $off
$pol = Read-Be32 $b ($off + 4)
$off += 8

$vertsOff = $off
$polyOff = $vertsOff + ($pts * 12)
$attrOff = $polyOff + ($pol * 20)

$rows = @()
$limit = [Math]::Min($objFaces.Count, $pol)
for ($i = 0; $i -lt $limit; ++$i) {
    $pOff = $polyOff + ($i * 20)
    $v0 = Read-Be16 $b ($pOff + 12)
    $v1 = Read-Be16 $b ($pOff + 14)
    $v2 = Read-Be16 $b ($pOff + 16)
    $v3 = Read-Be16 $b ($pOff + 18)

    $aOff = $attrOff + ($i * 8)
    $flags = $b[$aOff]
    $hasTex = (($flags -band 0x80) -ne 0)
    $tid = Read-Be32 $b ($aOff + 4)
    $tidOk = (-not $hasTex) -or ($tid -ge 0 -and $tid -lt $texCount)

    $rows += [pscustomobject]@{
        Face = $i
        Material = $objFaces[$i].Material
        ObjVerts = $objFaces[$i].ObjVerts
        ObjUvs = $objFaces[$i].ObjUvs
        NyaVerts = "$v0,$v1,$v2,$v3"
        HasTexture = if ($hasTex) { 1 } else { 0 }
        TextureId = $tid
        TextureIdInRange = if ($tidOk) { 1 } else { 0 }
    }
}

$summary = [pscustomobject]@{
    ObjFaces = $objFaces.Count
    NyaFaces = $pol
    NyaTextures = $texCount
    InvalidTextureIds = @($rows | Where-Object { $_.TextureIdInRange -eq 0 }).Count
}

Write-Host ("OBJ faces={0} NYA faces={1} tex={2} invalidTexIds={3}" -f $summary.ObjFaces, $summary.NyaFaces, $summary.NyaTextures, $summary.InvalidTextureIds)

if ([string]::IsNullOrWhiteSpace($OutCsv)) {
    $OutCsv = [System.IO.Path]::ChangeExtension($NyaPath, ".facecheck.csv")
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $OutCsv
Write-Host "CSV saved: $OutCsv"
