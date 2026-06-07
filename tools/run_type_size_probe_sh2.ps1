param()

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\type_size_probe"
$probeSource = Join-Path $repoRoot "tools\type_size_probe.cpp"
$probeObject = Join-Path $buildDir "type_size_probe_sh2.o"
$probeOutput = Join-Path $buildDir "type_size_probe_sh2.txt"
$compiler = Join-Path $repoRoot "..\..\Compiler\sh2eb-elf\bin\sh2eb-elf-g++.exe"
$nm = Join-Path $repoRoot "..\..\Compiler\sh2eb-elf\bin\sh2eb-elf-nm.exe"

if (-not (Test-Path $compiler))
{
    throw "SH2 compiler not found: $compiler"
}
if (-not (Test-Path $nm))
{
    throw "SH2 nm not found: $nm"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$compileArgs = @(
    "-DSRL_FRAMERATE=0",
    "-DDEBUG",
    "-DSRL_USE_SGL_SOUND_DRIVER=1",
    "-DSRL_ENABLE_FREQ_ANALYSIS=1",
    "-DSRL_MODE_NTSC",
    "-DSRL_MAX_TEXTURES=512",
    "-DSRL_MAX_CD_BACKGROUND_JOBS=1",
    "-DSRL_MAX_CD_FILES=4096",
    "-DSRL_MAX_CD_RETRIES=5",
    "-DSRL_DEBUG_MAX_PRINT_LENGTH=45",
    "-DSRL_DEBUG_MAX_LOG_LENGTH=80",
    "-DPHYSICS_POC_MODE=1",
    "-DPHYS_SATURN_LOW_COST=1",
    "-DPHYS_WALL_COLLISION_RUNTIME=1",
    "-DSGL_MAX_VERTICES=2800",
    "-DSGL_MAX_POLYGONS=2200",
    "-DSGL_MAX_EVENTS=64",
    "-DSGL_MAX_WORKS=64",
    "-DTYPE_SIZE_PROBE_EMIT_SYMBOLS=1",
    "-W",
    "-m2",
    "-c",
    "-O2",
    "-Wno-strict-aliasing",
    "-idirafter", (Join-Path $repoRoot "..\..\modules\dummy"),
    "-I", (Join-Path $repoRoot "..\..\modules\SaturnMathPP"),
    "-I", (Join-Path $repoRoot "..\..\modules\sgl\INC"),
    "-I", (Join-Path $repoRoot "..\..\modules\danny\INC"),
    "-I", (Join-Path $repoRoot "..\..\modules\tlsf"),
    "-I", (Join-Path $repoRoot "..\..\saturnringlib"),
    "-I", (Join-Path $repoRoot "src"),
    "-std=c++23",
    "-fpermissive",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-fno-threadsafe-statics",
    "-fno-use-cxa-atexit",
    "-o", $probeObject,
    $probeSource
)

Write-Host "Compiling SH2 size probe"
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0)
{
    throw "SH2 probe compilation failed with exit code $LASTEXITCODE"
}

$nmOutput = @(& $nm -S --size-sort $probeObject)
if ($LASTEXITCODE -ne 0)
{
    throw "SH2 nm failed with exit code $LASTEXITCODE"
}

$interesting = $nmOutput | Where-Object { $_ -match "size_" }
$rows = foreach ($line in $interesting)
{
    if ($line -match "^[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+\w\s+_?(size_[A-Za-z0-9_]+)$")
    {
        $hexSize = $Matches[1]
        $symbol = $Matches[2]
        $size = [Convert]::ToInt32($hexSize, 16)
        "{0,-56} {1,6}" -f $symbol, $size
    }
}

"TYPE SIZE AUDIT (SH2 OBJECT SYMBOLS)" | Set-Content $probeOutput
"------------------------------------------------------------" | Add-Content $probeOutput
$rows | Add-Content $probeOutput
Get-Content $probeOutput
