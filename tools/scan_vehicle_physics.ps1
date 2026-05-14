param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Root)) {
    throw "Root path not found: $Root"
}

$rg = Get-Command rg -ErrorAction SilentlyContinue
if (-not $rg) {
    throw "ripgrep (rg) not found in PATH."
}

$patterns = @(
    "physics",
    "dynamics",
    "vehicle",
    "car",
    "wheel",
    "suspension",
    "steer",
    "throttle",
    "brake",
    "yaw",
    "slip",
    "traction",
    "collision",
    "ground",
    "surface",
    "friction",
    "contact",
    "raycast"
)

$extGlob = @("*.c","*.cc","*.cpp","*.cxx","*.h","*.hpp","*.hh","*.inl")
$globArgs = @()
foreach ($g in $extGlob) {
    $globArgs += @("-g", $g)
}

$rootAbs = (Resolve-Path -LiteralPath $Root).Path
$quotedPatterns = ($patterns -join "|")

$args = @(
    "--no-ignore",
    "-n",
    "-S",
    "-i",
    "-e", $quotedPatterns
) + $globArgs + @($rootAbs)

$matches = & rg @args
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 1) {
    throw "rg failed with exit code $LASTEXITCODE"
}

$rows = @()
foreach ($line in $matches) {
    # rg output format: <file>:<line>:<text>
    # Use regex to preserve "C:\..." drive prefix safely.
    $m = [regex]::Match($line, "^(.*):([0-9]+):(.*)$")
    if (-not $m.Success) { continue }
    $file = $m.Groups[1].Value
    $lineNoRaw = $m.Groups[2].Value
    $text = $m.Groups[3].Value.Trim()
    $lineNo = 0
    [void][int]::TryParse($lineNoRaw, [ref]$lineNo)
    $rows += [PSCustomObject]@{
        file = $file
        line = $lineNo
        text = $text
    }
}

$grouped = $rows | Group-Object -Property file | Sort-Object Count -Descending
$topFiles = @()
foreach ($g in $grouped | Select-Object -First 40) {
    $topFiles += [PSCustomObject]@{
        file = $g.Name
        hits = $g.Count
    }
}

$result = [PSCustomObject]@{
    root = $rootAbs
    totalHits = $rows.Count
    topFiles = $topFiles
    sampleMatches = $rows | Select-Object -First 240
}

$json = $result | ConvertTo-Json -Depth 6
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    Write-Output $json
} else {
    $outAbs = [System.IO.Path]::GetFullPath($OutFile)
    $outDir = [System.IO.Path]::GetDirectoryName($outAbs)
    if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
        New-Item -ItemType Directory -Path $outDir | Out-Null
    }
    Set-Content -LiteralPath $outAbs -Value $json -Encoding UTF8
    Write-Output "written: $outAbs"
}
