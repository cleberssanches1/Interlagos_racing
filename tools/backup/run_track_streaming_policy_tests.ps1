param(
    [string]$CompilerPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\host_tests"
$testSource = Join-Path $repoRoot "tests\track_streaming_policy_tests.cpp"
$testBinary = Join-Path $buildDir "track_streaming_policy_tests.exe"

if (-not (Test-Path $testSource))
{
    throw "Test source not found: $testSource"
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
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-I", (Join-Path $repoRoot "src"),
    $testSource,
    "-o", $testBinary
)

Write-Host "Compiling tests with $CompilerPath"
& $CompilerPath @compileArgs
if ($LASTEXITCODE -ne 0)
{
    throw "Compilation failed with exit code $LASTEXITCODE"
}

Write-Host "Running $testBinary"
& $testBinary
exit $LASTEXITCODE
