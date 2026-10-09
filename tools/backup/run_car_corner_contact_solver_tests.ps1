param(
    [string]$CompilerPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\host_tests"
$testSource = Join-Path $repoRoot "tests\car_corner_contact_solver_tests.cpp"
$testBinary = Join-Path $buildDir "car_corner_contact_solver_tests.exe"

if (-not $CompilerPath)
{
    $candidates = @($env:CXX, "C:\msys64\mingw64\bin\g++.exe", "g++.exe") |
        Where-Object { $_ }
    foreach ($candidate in $candidates)
    {
        if (Test-Path $candidate) { $CompilerPath = $candidate; break }
        $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($resolved) { $CompilerPath = $resolved.Source; break }
    }
}
if (-not $CompilerPath) { throw "No host C++ compiler found." }

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
& $CompilerPath -std=c++17 -Wall -Wextra -pedantic -I (Join-Path $repoRoot "src") `
    $testSource -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }
& $testBinary
exit $LASTEXITCODE
