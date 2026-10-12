param([string]$CompilerPath = "C:\msys64\mingw64\bin\g++.exe")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "BuildDrop\host_tests"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$binary = Join-Path $outputDirectory "speedometer_policy_tests.exe"
& $CompilerPath -std=c++17 -Wall -Wextra -pedantic -I (Join-Path $repoRoot "src") `
    (Join-Path $repoRoot "tests\speedometer_policy_tests.cpp") -o $binary
if ($LASTEXITCODE -ne 0) { throw "Compilacao do teste de velocimetro falhou." }
& $binary
if ($LASTEXITCODE -ne 0) { throw "Teste de velocimetro falhou." }
Write-Host "speedometer policy OK"
