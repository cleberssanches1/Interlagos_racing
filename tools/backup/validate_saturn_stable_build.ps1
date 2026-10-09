param(
    [switch]$SkipHostTests,
    [int64]$ExpectedIsoSize = 4134912
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$isoPath = Join-Path $repoRoot "BuildDrop\\Interlagos_racing.iso"
$hostTestScript = Join-Path $repoRoot "tools\\run_frame_reuse_decision_tests.ps1"

Push-Location $repoRoot
try
{
    $originalPath = $env:PATH
    $toolchainPrefix = "../../Compiler/Other Utilities;../../Compiler/msys2/usr/bin;../../Compiler/sh2eb-elf/bin;"
    $env:PATH = $toolchainPrefix + $env:PATH

    Write-Host "[1/3] Cleaning build artifacts"
    & make clean
    if ($LASTEXITCODE -ne 0)
    {
        throw "make clean failed with exit code $LASTEXITCODE"
    }

    Write-Host "[2/3] Building Saturn image"
    & make -B build DEBUG=1
    if ($LASTEXITCODE -ne 0)
    {
        throw "make build failed with exit code $LASTEXITCODE"
    }

    if (-not (Test-Path $isoPath))
    {
        throw "ISO not generated: $isoPath"
    }

    $isoSize = (Get-Item $isoPath).Length
    Write-Host ("ISO size: {0}" -f $isoSize)
    if ($isoSize -ne $ExpectedIsoSize)
    {
        throw ("Unexpected ISO size. Expected {0}, got {1}" -f $ExpectedIsoSize, $isoSize)
    }

    if (-not $SkipHostTests)
    {
        Write-Host "[3/3] Running host validation"
        $env:PATH = $originalPath
        & $hostTestScript
        if ($LASTEXITCODE -ne 0)
        {
            throw "Host validation failed with exit code $LASTEXITCODE"
        }
    }
    else
    {
        Write-Host "[3/3] Host validation skipped"
    }

    Write-Host "Stable build validation passed."
}
finally
{
    Pop-Location
}
