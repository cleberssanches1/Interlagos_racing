param([string]$CompilerPath = "")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\host_tests"
$source = Join-Path $repoRoot "tests\auto_lap_car_contact_policy_tests.cpp"
$binary = Join-Path $buildDir "auto_lap_car_contact_policy_tests.exe"

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
if ($LASTEXITCODE -ne 0) { throw "Contact policy test failed: $LASTEXITCODE" }
Write-Host "auto_lap_car_contact_policy_tests: PASS"
