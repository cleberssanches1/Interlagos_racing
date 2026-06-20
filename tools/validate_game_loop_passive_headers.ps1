param()

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\passive_header_validation"
$sourcePath = Join-Path $buildDir "game_loop_passive_headers_smoke.cxx"
$objectPath = Join-Path $buildDir "game_loop_passive_headers_smoke.o"
$compiler = Join-Path $repoRoot "..\..\Compiler\sh2eb-elf\bin\sh2eb-elf-g++.exe"

if (-not (Test-Path $compiler))
{
    throw "SH2 compiler not found: $compiler"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

@"
#include "game_loop_presentation_ops.hpp"
#include "game_loop_overlay_contracts.hpp"
#include "game_loop_overlay_state_assembler.hpp"
#include "game_loop_telemetry_contracts.hpp"
#include "game_loop_telemetry_state_assembler.hpp"
#include "game_loop_observability_contracts.hpp"
#include "game_loop_observability_state_assembler.hpp"
#include "game_loop_memory_presentation_contracts.hpp"
#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_track_render_packet.hpp"
#include "game_loop_track_render_packet_assembler.hpp"
#include "game_loop_cd_asset_packet.hpp"
#include "game_loop_cd_asset_packet_assembler.hpp"
#include "game_loop_memory_budget_packet.hpp"
#include "game_loop_memory_budget_packet_assembler.hpp"
#include "game_loop_auto_lap_packet.hpp"
#include "game_loop_auto_lap_packet_assembler.hpp"
int main() { return 0; }
"@ | Set-Content $sourcePath

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
    "-DAUDIO_PROFILE=1",
    "-DPHYS_SATURN_LOW_COST=1",
    "-DPHYS_WALL_COLLISION_RUNTIME=1",
    "-DSGL_MAX_VERTICES=2800",
    "-DSGL_MAX_POLYGONS=2200",
    "-DSGL_MAX_EVENTS=64",
    "-DSGL_MAX_WORKS=64",
    "-W",
    "-m2",
    "-c",
    "-O2",
    "-Wno-strict-aliasing",
    "-idirafter", (Join-Path $repoRoot "..\..\modules\dummy"),
    "-I" + (Join-Path $repoRoot "..\..\modules\SaturnMathPP"),
    "-I" + (Join-Path $repoRoot "..\..\modules\sgl\INC"),
    "-I" + (Join-Path $repoRoot "..\..\modules\danny\INC"),
    "-I" + (Join-Path $repoRoot "..\..\modules\tlsf"),
    "-I" + (Join-Path $repoRoot "..\..\saturnringlib"),
    "-I" + (Join-Path $repoRoot "src"),
    "-std=c++23",
    "-fpermissive",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-fno-threadsafe-statics",
    "-fno-use-cxa-atexit",
    $sourcePath,
    "-o", $objectPath
)

Write-Host "Compiling passive game-loop headers with SH2 toolchain"
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0)
{
    throw "Passive header validation failed with exit code $LASTEXITCODE"
}

Write-Host "Passive header validation passed."
