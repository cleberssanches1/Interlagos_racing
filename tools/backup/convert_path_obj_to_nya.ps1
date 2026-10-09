param(
    [string]$InputPath = "C:\Models\png\PATH.obj",
    [string]$OutputPath = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\PATH.NYA",
    [string]$SummaryPath = "",
    [ValidateSet("TrackObj", "SwapYZ", "ObjAsIs")]
    [string]$AxisMapping = "ObjAsIs"
)

$ErrorActionPreference = "Stop"

function Parse-InvariantDouble {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    return [double]::Parse(
        $Text,
        [System.Globalization.NumberStyles]::Float -bor [System.Globalization.NumberStyles]::AllowThousands,
        [System.Globalization.CultureInfo]::InvariantCulture)
}

function Convert-ObjVertexIndex {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Token,
        [Parameter(Mandatory = $true)]
        [int]$VertexCount
    )

    $parts = $Token.Split('/')
    if ($parts.Count -eq 0 -or [string]::IsNullOrWhiteSpace($parts[0])) {
        throw "OBJ line token invalido: '$Token'"
    }

    $index = [int]$parts[0]
    if ($index -lt 0) {
        $index = $VertexCount + $index + 1
    }
    if ($index -le 0 -or $index -gt $VertexCount) {
        throw "Indice de vertice fora do intervalo: $index (total=$VertexCount)"
    }

    return $index
}

function Append-PolylineRefs {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[int]]$OrderedRefs,
        [Parameter(Mandatory = $true)]
        [int[]]$Refs
    )

    if ($Refs.Count -lt 2) {
        return
    }

    if ($OrderedRefs.Count -eq 0) {
        foreach ($refIndex in $Refs) {
            $OrderedRefs.Add($refIndex)
        }
        return
    }

    $routeFirst = $OrderedRefs[0]
    $routeLast = $OrderedRefs[$OrderedRefs.Count - 1]
    $first = $Refs[0]
    $last = $Refs[$Refs.Count - 1]

    if ($first -eq $routeLast) {
        for ($i = 1; $i -lt $Refs.Count; ++$i) {
            if ($OrderedRefs[$OrderedRefs.Count - 1] -ne $Refs[$i]) {
                $OrderedRefs.Add($Refs[$i])
            }
        }
        return
    }

    if ($last -eq $routeLast) {
        for ($i = $Refs.Count - 2; $i -ge 0; --$i) {
            if ($OrderedRefs[$OrderedRefs.Count - 1] -ne $Refs[$i]) {
                $OrderedRefs.Add($Refs[$i])
            }
        }
        return
    }

    if ($last -eq $routeFirst) {
        for ($i = $Refs.Count - 2; $i -ge 0; --$i) {
            if ($OrderedRefs[0] -ne $Refs[$i]) {
                $OrderedRefs.Insert(0, $Refs[$i])
            }
        }
        return
    }

    if ($first -eq $routeFirst) {
        for ($i = 1; $i -lt $Refs.Count; ++$i) {
            if ($OrderedRefs[0] -ne $Refs[$i]) {
                $OrderedRefs.Insert(0, $Refs[$i])
            }
        }
        return
    }

    # Segmento desconectado: preserva o trecho na ordem encontrada.
    foreach ($refIndex in $Refs) {
        if ($OrderedRefs.Count -eq 0 -or $OrderedRefs[$OrderedRefs.Count - 1] -ne $refIndex) {
            $OrderedRefs.Add($refIndex)
        }
    }
}

function Add-UInt32BE {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[byte]]$Bytes,
        [Parameter(Mandatory = $true)]
        [uint32]$Value
    )

    $raw = [System.BitConverter]::GetBytes($Value)
    if ([System.BitConverter]::IsLittleEndian) {
        [Array]::Reverse($raw)
    }
    $Bytes.AddRange($raw)
}

function Add-Int32BE {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[byte]]$Bytes,
        [Parameter(Mandatory = $true)]
        [int]$Value
    )

    $raw = [System.BitConverter]::GetBytes([int32]$Value)
    if ([System.BitConverter]::IsLittleEndian) {
        [Array]::Reverse($raw)
    }
    $Bytes.AddRange($raw)
}

function To-Fixed1616Raw {
    param(
        [Parameter(Mandatory = $true)]
        [double]$Value
    )

    return [int][Math]::Round($Value * 65536.0)
}

function Convert-VertexToEnginePoint {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Vertex,
        [Parameter(Mandatory = $true)]
        [string]$Mapping
    )

    switch ($Mapping) {
        "TrackObj" {
            return [pscustomobject]@{
                # PATH.obj uses XY as ground plane. Match the segment OBJ space:
                # engine X <- -OBJ Y, engine Y <- OBJ Z, engine Z <- -OBJ X.
                X = -[double]$Vertex.Y
                Y = [double]$Vertex.Z
                Z = -[double]$Vertex.X
            }
        }
        "SwapYZ" {
            return [pscustomobject]@{
                X = [double]$Vertex.X
                Y = [double]$Vertex.Z
                Z = [double]$Vertex.Y
            }
        }
        default {
            return [pscustomobject]@{
                X = [double]$Vertex.X
                Y = [double]$Vertex.Y
                Z = [double]$Vertex.Z
            }
        }
    }
}

if (-not (Test-Path $InputPath)) {
    throw "Arquivo OBJ nao encontrado: $InputPath"
}

$vertices = New-Object 'System.Collections.Generic.List[object]'
$vertices.Add($null) | Out-Null
$linesByObject = New-Object 'System.Collections.Generic.List[object]'
$currentLine = $null
$currentName = ""

foreach ($rawLine in Get-Content $InputPath) {
    $line = $rawLine.Trim()
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    if ($line.StartsWith('#')) { continue }

    if ($line -match '^o\s+(.+)$') {
        $currentName = $matches[1].Trim()
        $currentLine = [pscustomobject]@{
            Name = $currentName
            Refs = (New-Object 'System.Collections.Generic.List[int]')
        }
        $linesByObject.Add($currentLine) | Out-Null
        continue
    }

    if ($line -match '^v\s+(.+)$') {
        $parts = $line.Split(@(' '), [System.StringSplitOptions]::RemoveEmptyEntries)
        if ($parts.Count -lt 4) {
            throw "Linha de vertice invalida: $line"
        }

        $vertex = [pscustomobject]@{
            X = (Parse-InvariantDouble $parts[1])
            Y = (Parse-InvariantDouble $parts[2])
            Z = (Parse-InvariantDouble $parts[3])
        }
        $vertices.Add($vertex) | Out-Null
        continue
    }

    if ($line -match '^l\s+(.+)$') {
        if ($null -eq $currentLine) {
            $currentName = "path_1"
            $currentLine = [pscustomobject]@{
                Name = $currentName
                Refs = (New-Object 'System.Collections.Generic.List[int]')
            }
            $linesByObject.Add($currentLine) | Out-Null
        }

        $tokens = $line.Split(@(' '), [System.StringSplitOptions]::RemoveEmptyEntries) | Select-Object -Skip 1
        if ($tokens.Count -lt 2) {
            continue
        }

        $refs = New-Object 'System.Collections.Generic.List[int]'
        foreach ($token in $tokens) {
            $refs.Add((Convert-ObjVertexIndex -Token $token -VertexCount ($vertices.Count - 1))) | Out-Null
        }
        Append-PolylineRefs -OrderedRefs $currentLine.Refs -Refs $refs.ToArray()
        continue
    }
}

if ($linesByObject.Count -eq 0) {
    throw "Nenhuma trilha OBJ encontrada em $InputPath"
}

if (($vertices.Count - 1) -eq 0) {
    throw "Nenhum vertice OBJ encontrado em $InputPath"
}

$linePayloads = New-Object 'System.Collections.Generic.List[object]'
foreach ($lineEntry in $linesByObject) {
    if ($lineEntry.Refs.Count -lt 2) {
        throw "A trilha '$($lineEntry.Name)' nao possui pontos suficientes."
    }

    $points = New-Object 'System.Collections.Generic.List[object]'
    foreach ($refIndex in $lineEntry.Refs) {
        $vertex = $vertices[$refIndex]
        if ($null -eq $vertex) {
            throw "Vertice OBJ nao encontrado para indice $refIndex"
        }
        $enginePoint = Convert-VertexToEnginePoint -Vertex $vertex -Mapping $AxisMapping

        $points.Add([pscustomobject]@{
            XRaw = (To-Fixed1616Raw $enginePoint.X)
            YRaw = (To-Fixed1616Raw $enginePoint.Y)
            ZRaw = (To-Fixed1616Raw $enginePoint.Z)
        }) | Out-Null
    }

    $linePayloads.Add([pscustomobject]@{
        Name = $lineEntry.Name
        PointCount = $points.Count
        Points = $points
    }) | Out-Null
}

$outputBytes = New-Object 'System.Collections.Generic.List[byte]'
$version = [uint32]1
$lineCount = [uint32]$linePayloads.Count
Add-UInt32BE -Bytes $outputBytes -Value $version
Add-UInt32BE -Bytes $outputBytes -Value $lineCount

$headerBytes = 8 + ($linePayloads.Count * 8)
$dataOffset = $headerBytes
foreach ($linePayload in $linePayloads) {
    Add-UInt32BE -Bytes $outputBytes -Value ([uint32]$linePayload.PointCount)
    Add-UInt32BE -Bytes $outputBytes -Value ([uint32]$dataOffset)
    $dataOffset += ($linePayload.PointCount * 12)
}

foreach ($linePayload in $linePayloads) {
    foreach ($point in $linePayload.Points) {
        Add-Int32BE -Bytes $outputBytes -Value $point.XRaw
        Add-Int32BE -Bytes $outputBytes -Value $point.YRaw
        Add-Int32BE -Bytes $outputBytes -Value $point.ZRaw
    }
}

$outDir = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
[System.IO.File]::WriteAllBytes($OutputPath, $outputBytes.ToArray())

if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    $SummaryPath = [System.IO.Path]::ChangeExtension($OutputPath, ".path.json")
}

$summary = [pscustomobject]@{
    input = $InputPath
    output = $OutputPath
    version = $version
    axisMapping = $AxisMapping
    lineCount = $lineCount
    lines = @(
        foreach ($linePayload in $linePayloads) {
            [pscustomobject]@{
                name = $linePayload.Name
                pointCount = $linePayload.PointCount
            }
        }
    )
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding ASCII $SummaryPath

Write-Host ("PATH.NYA gerado: {0}" -f $OutputPath)
Write-Host ("Linhas exportadas: {0}" -f $linePayloads.Count)
foreach ($linePayload in $linePayloads) {
    Write-Host ("  {0}: {1} pontos" -f $linePayload.Name, $linePayload.PointCount)
}
Write-Host ("Resumo salvo em: {0}" -f $SummaryPath)
