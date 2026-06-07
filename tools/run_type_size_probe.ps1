param(
    [string]$CompilerPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\host_tests"
$probeSource = Join-Path $repoRoot "tools\type_size_probe.cpp"
$probeBinary = Join-Path $buildDir "type_size_probe.exe"
$probeOutput = Join-Path $buildDir "type_size_probe.txt"

if (-not (Test-Path $probeSource))
{
    throw "Probe source not found: $probeSource"
}

if (-not $CompilerPath)
{
    $candidates = @(
        $env:CXX,
        "C:\msys64\mingw64\bin\g++.exe",
        "C:\msys64\ucrt64\bin\g++.exe",
        "g++.exe"
    ) | Where-Object { $_ }

    foreach ($candidate in $candidates)
    {
        if (Test-Path $candidate)
        {
            $CompilerPath = $candidate
            break
        }
        $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($resolved)
        {
            $CompilerPath = $resolved.Source
            break
        }
    }
}

if (-not $CompilerPath)
{
    throw "No host C++ compiler found. Pass -CompilerPath explicitly."
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$compileArgs = @(
    "-std=c++23",
    "-Wall",
    "-Wextra",
    "-Wno-unknown-pragmas",
    "-Wno-unused-parameter",
    "-fpermissive",
    "-idirafter", (Join-Path $repoRoot "..\..\modules\dummy"),
    "-I", (Join-Path $repoRoot "src"),
    "-I", (Join-Path $repoRoot "..\..\modules\SaturnMathPP"),
    "-I", (Join-Path $repoRoot "..\..\modules\sgl\INC"),
    "-I", (Join-Path $repoRoot "..\..\modules\danny\INC"),
    "-I", (Join-Path $repoRoot "..\..\modules\tlsf"),
    "-I", (Join-Path $repoRoot "..\..\saturnringlib"),
    $probeSource,
    "-o", $probeBinary
)

Write-Host "Compiling probe with $CompilerPath"
& $CompilerPath @compileArgs
if ($LASTEXITCODE -ne 0)
{
    throw "Compilation failed with exit code $LASTEXITCODE"
}

Write-Host "Running $probeBinary"
& $probeBinary | Tee-Object -FilePath $probeOutput
exit $LASTEXITCODE
