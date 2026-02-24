param(
    [int]$SegmentId = 1,
    [string]$ObjDir = "C:\Models\png\sectors\source",
    [string]$JsonPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\segments_map.json",
    [string]$OutDir = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-U16([System.IO.BinaryWriter]$bw, [uint16]$v) { $bw.Write($v) }
function Write-I16([System.IO.BinaryWriter]$bw, [int16]$v) { $bw.Write($v) }
function Write-U32([System.IO.BinaryWriter]$bw, [uint32]$v) { $bw.Write($v) }
function Write-I32([System.IO.BinaryWriter]$bw, [int32]$v) { $bw.Write($v) }

function To-Fxp32([double]$v) {
    return [int32][Math]::Round($v * 65536.0)
}

function To-I16Uv([double]$v) {
    $scaled = [int][Math]::Round($v * 32767.0)
    if ($scaled -lt -32768) { $scaled = -32768 }
    if ($scaled -gt 32767) { $scaled = 32767 }
    return [int16]$scaled
}

if (-not (Test-Path $ObjDir)) { throw "ObjDir nao encontrado: $ObjDir" }
if (-not (Test-Path $JsonPath)) { throw "JSON nao encontrado: $JsonPath" }
New-Item -Path $OutDir -ItemType Directory -Force | Out-Null

$objName = ("seg_{0:D3}.obj" -f $SegmentId)
$objPath = Join-Path $ObjDir $objName
if (-not (Test-Path $objPath)) { throw "OBJ nao encontrado: $objPath" }

$json = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
$segNode = $json.segments | Where-Object { [int]$_.id -eq $SegmentId } | Select-Object -First 1
$hasSegmentMap = $true
if (-not $segNode) {
    $hasSegmentMap = $false
    Write-Host ("AVISO: Segmento {0} nao encontrado em segments_map.json. MAT sera gerado com materialId=0 para todas as faces." -f $SegmentId)
}
$faceFamilies = @()
if ($hasSegmentMap -and $segNode.faceTextureFamily) {
    $faceFamilies = @($segNode.faceTextureFamily | ForEach-Object { [uint32]$_ })
}

$verts = New-Object System.Collections.Generic.List[object]
$uvs = New-Object System.Collections.Generic.List[object]
$faces = New-Object System.Collections.Generic.List[object]

$lines = Get-Content -LiteralPath $objPath
foreach ($line in $lines) {
    $t = $line.Trim()
    if ($t.Length -eq 0 -or $t.StartsWith("#")) { continue }

    if ($t.StartsWith("v ")) {
        $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        if ($p.Count -ge 4) {
            $verts.Add([PSCustomObject]@{
                x = [double]$p[1]
                y = [double]$p[2]
                z = [double]$p[3]
            }) | Out-Null
        }
        continue
    }

    if ($t.StartsWith("vt ")) {
        $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        if ($p.Count -ge 3) {
            $uvs.Add([PSCustomObject]@{
                u = [double]$p[1]
                v = [double]$p[2]
            }) | Out-Null
        }
        continue
    }

    if ($t.StartsWith("f ")) {
        $p = $t.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        if ($p.Count -lt 4) { continue }
        $corners = @()
        for ($i = 1; $i -lt $p.Count; $i++) {
            $tok = $p[$i]
            $parts = $tok.Split("/")
            $vi = if ($parts.Count -ge 1 -and $parts[0].Length -gt 0) { [int]$parts[0] } else { 0 }
            $ti = if ($parts.Count -ge 2 -and $parts[1].Length -gt 0) { [int]$parts[1] } else { 0 }
            if ($vi -le 0) { throw "Face com indice de vertice invalido: '$tok' em $objName" }
            $corners += ,([PSCustomObject]@{ vi = $vi - 1; ti = [Math]::Max(0, $ti - 1) })
        }
        if ($corners.Count -gt 4) {
            # Fan triangulation for n-gons: (0, i, i+1)
            for ($i = 1; $i -lt ($corners.Count - 1); $i++) {
                $faces.Add(@($corners[0], $corners[$i], $corners[$i + 1])) | Out-Null
            }
        } else {
            $faces.Add($corners) | Out-Null
        }
    }
}

if ($verts.Count -eq 0 -or $faces.Count -eq 0) {
    throw "OBJ sem vertices/faces suficientes: $objPath"
}

$geoPath = Join-Path $OutDir ("SEG_{0:D3}.GEO" -f $SegmentId)
$matPath = Join-Path $OutDir ("SEG_{0:D3}.MAT" -f $SegmentId)

# GEO
$geoFs = [System.IO.File]::Open($geoPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $bw = New-Object System.IO.BinaryWriter($geoFs)
    # FileHeader
    $geoPayloadBytes = [uint32](8 + ($verts.Count * 12) + ($faces.Count * 28))
    Write-U32 $bw 0x314F4547 # GEO1
    Write-U16 $bw 1
    Write-U16 $bw 0
    Write-U32 $bw ([uint32]$SegmentId)
    Write-U32 $bw $geoPayloadBytes
    # GeoHeader
    Write-U32 $bw ([uint32]$verts.Count)
    Write-U32 $bw ([uint32]$faces.Count)
    # Vertices
    foreach ($v in $verts) {
        Write-I32 $bw (To-Fxp32 $v.x)
        Write-I32 $bw (To-Fxp32 $v.y)
        Write-I32 $bw (To-Fxp32 $v.z)
    }
    # Faces
    foreach ($f in $faces) {
        $kind = [byte]$f.Count
        if ($kind -ne 3 -and $kind -ne 4) { $kind = 3 }

        $vi = @(0,0,0,0)
        $uu = @(0,0,0,0)
        $vv = @(0,0,0,0)
        for ($i = 0; $i -lt $f.Count; $i++) {
            $c = $f[$i]
            if ($c.vi -lt 0 -or $c.vi -ge $verts.Count) { throw "Indice de vertice fora do range na face" }
            $vi[$i] = [uint16]$c.vi
            if ($c.ti -ge 0 -and $c.ti -lt $uvs.Count) {
                $u = [double]$uvs[$c.ti].u
                $v = [double]$uvs[$c.ti].v
                $uu[$i] = To-I16Uv $u
                $vv[$i] = To-I16Uv $v
            }
        }
        for ($i = 0; $i -lt 4; $i++) { Write-U16 $bw ([uint16]$vi[$i]) }
        for ($i = 0; $i -lt 4; $i++) { Write-I16 $bw ([int16]$uu[$i]) }
        for ($i = 0; $i -lt 4; $i++) { Write-I16 $bw ([int16]$vv[$i]) }
        $bw.Write([byte]$kind)
        $bw.Write([byte]0)
        Write-U16 $bw 0
    }
    $bw.Flush()
} finally {
    $geoFs.Close()
}

# MAT
$matFs = [System.IO.File]::Open($matPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $bw = New-Object System.IO.BinaryWriter($matFs)
    $matPayloadBytes = [uint32](4 + ($faces.Count * 4))
    Write-U32 $bw 0x3154414D # MAT1
    Write-U16 $bw 1
    Write-U16 $bw 0
    Write-U32 $bw ([uint32]$SegmentId)
    Write-U32 $bw $matPayloadBytes
    # MatHeader
    Write-U32 $bw ([uint32]$faces.Count)
    # Bindings
    for ($i = 0; $i -lt $faces.Count; $i++) {
        $mid = [uint32]0
        if ($i -lt $faceFamilies.Count) { $mid = [uint32]$faceFamilies[$i] }
        Write-U32 $bw $mid
    }
    $bw.Flush()
} finally {
    $matFs.Close()
}

Write-Host ("OK GEO: {0}" -f $geoPath)
Write-Host ("OK MAT: {0}" -f $matPath)
Write-Host ("Verts:{0} Faces:{1} Families:{2}" -f $verts.Count, $faces.Count, $faceFamilies.Count)
