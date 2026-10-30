param([string]$CompilerPath = "")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\host_tests"
$source = Join-Path $repoRoot "tests\minimap_marker_layout_tests.cpp"
$binary = Join-Path $buildDir "minimap_marker_layout_tests.exe"
if (-not $CompilerPath)
{
    foreach ($candidate in @($env:CXX, "C:\msys64\mingw64\bin\g++.exe", "C:\msys64\ucrt64\bin\g++.exe", "g++.exe"))
    {
        if (-not $candidate) { continue }
        if (Test-Path $candidate) { $CompilerPath = $candidate; break }
        $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($resolved) { $CompilerPath = $resolved.Source; break }
    }
}
if (-not $CompilerPath) { throw "No host C++ compiler found" }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
& $CompilerPath -std=c++17 -Wall -Wextra -pedantic -I (Join-Path $repoRoot "src") $source -o $binary
if ($LASTEXITCODE -ne 0) { throw "Host compile failed: $LASTEXITCODE" }
& $binary
if ($LASTEXITCODE -ne 0) { throw "Minimap marker layout test failed: $LASTEXITCODE" }
Write-Host "minimap_marker_layout_tests: PASS"
