param()

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "BuildDrop\observability_header_validation"
$sourcePath = Join-Path $buildDir "game_loop_observability_headers_smoke.cxx"
$objectPath = Join-Path $buildDir "game_loop_observability_headers_smoke.o"
$compiler = Join-Path $repoRoot "..\..\Compiler\sh2eb-elf\bin\sh2eb-elf-g++.exe"

if (-not (Test-Path $compiler))
{
    throw "SH2 compiler not found: $compiler"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

@"
#include "game_loop_overlay_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"
#include "game_loop_presentation_debug_assembler.hpp"
#include "game_loop_render_debug_contracts.hpp"
#include "game_loop_render_debug_assembler.hpp"
#include "game_loop_car_visual_debug_contracts.hpp"
#include "game_loop_car_visual_debug_assembler.hpp"
#include "game_loop_track_render_debug_contracts.hpp"
#include "game_loop_track_render_debug_assembler.hpp"
#include "game_loop_presenter_render_debug_contracts.hpp"
#include "game_loop_presenter_render_debug_assembler.hpp"
#include "game_loop_presenter_overlay_debug_contracts.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"
#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_observability_input_assembler.hpp"
#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presenter_input_summary_assembler.hpp"
#include "game_loop_presenter_input_contracts.hpp"
#include "game_loop_presenter_input_assembler.hpp"
#include "game_loop_overlay_state_assembler.hpp"
#include "game_loop_telemetry_contracts.hpp"
#include "game_loop_telemetry_state_assembler.hpp"
#include "game_loop_memory_presentation_contracts.hpp"
#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_memory_presentation_packet_assembler.hpp"
#include "game_loop_memory_trace_packet_assembler.hpp"
#include "game_loop_memory_trace_text_contracts.hpp"
#include "game_loop_memory_trace_text_assembler.hpp"
#include "game_loop_memory_trace_text_low_work_view_contracts.hpp"
#include "game_loop_memory_trace_text_low_work_view_assembler.hpp"
#include "game_loop_memory_trace_text_view_contracts.hpp"
#include "game_loop_memory_trace_text_view_assembler.hpp"
#include "game_loop_memory_overlay_text_contracts.hpp"
#include "game_loop_memory_overlay_text_assembler.hpp"
#include "game_loop_memory_overlay_text_view_contracts.hpp"
#include "game_loop_memory_overlay_text_view_assembler.hpp"
#include "game_loop_memory_debug_contracts.hpp"
#include "game_loop_memory_debug_packet_assembler.hpp"
#include "game_loop_observability_debug_contracts.hpp"
#include "game_loop_observability_debug_packet_assembler.hpp"
#include "game_loop_overlay_debug_text_contracts.hpp"
#include "game_loop_overlay_debug_text_assembler.hpp"
#include "game_loop_overlay_debug_contracts.hpp"
#include "game_loop_overlay_debug_packet_assembler.hpp"
#include "game_loop_track_render_packet.hpp"
#include "game_loop_track_render_packet_assembler.hpp"
#include "game_loop_track_render_producer_hint_contracts.hpp"
#include "game_loop_track_render_producer_hint_assembler.hpp"
#include "game_loop_track_reuse_decision_view_contracts.hpp"
#include "game_loop_track_reuse_decision_view_assembler.hpp"
#include "game_loop_track_reuse_telemetry_view_contracts.hpp"
#include "game_loop_track_reuse_telemetry_view_assembler.hpp"
#include "game_loop_simulation_reuse_decision_view_contracts.hpp"
#include "game_loop_simulation_reuse_decision_view_assembler.hpp"
#include "game_loop_simulation_reuse_telemetry_view_contracts.hpp"
#include "game_loop_simulation_reuse_telemetry_view_assembler.hpp"
#include "game_loop_simulation_drain_view_contracts.hpp"
#include "game_loop_simulation_drain_view_assembler.hpp"
#include "game_loop_simulation_completion_view_contracts.hpp"
#include "game_loop_simulation_completion_view_assembler.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_assembler.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp"
#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_reuse_observability_assembler.hpp"
#include "game_loop_reuse_runtime_observability_ops.hpp"
#include "frame_reuse_runtime_owner_contracts.hpp"
#include "frame_reuse_runtime_owner_assembler.hpp"
#include "frame_reuse_runtime_observability_owner_assembler.hpp"
#include "frame_reuse_observability_source_contracts.hpp"
#include "frame_reuse_observability_source_assembler.hpp"
#include "frame_reuse_observability_source_owner_contracts.hpp"
#include "frame_reuse_observability_source_owner_assembler.hpp"
#include "frame_reuse_observability_capture_ops.hpp"
#include "game_loop_reuse_source_state_contracts.hpp"
#include "game_loop_reuse_source_state_assembler.hpp"
#include "game_loop_reuse_source_owner_contracts.hpp"
#include "game_loop_reuse_source_owner_assembler.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"
#include "game_loop_track_render_telemetry_view_assembler.hpp"
#include "game_loop_car_visual_packet.hpp"
#include "game_loop_car_visual_packet_assembler.hpp"
#include "game_loop_cd_asset_packet.hpp"
#include "game_loop_cd_asset_packet_assembler.hpp"
#include "game_loop_cd_asset_bootstrap_decision_contracts.hpp"
#include "game_loop_cd_asset_bootstrap_decision_assembler.hpp"
#include "game_loop_cd_asset_sba_decision_contracts.hpp"
#include "game_loop_cd_asset_sba_decision_assembler.hpp"
#include "game_loop_cd_asset_anchor_decision_contracts.hpp"
#include "game_loop_cd_asset_anchor_decision_assembler.hpp"
#include "game_loop_cd_asset_decision_bridge_contracts.hpp"
#include "game_loop_cd_asset_decision_bridge_assembler.hpp"
#include "game_loop_observability_contracts.hpp"
#include "game_loop_observability_state_assembler.hpp"
#include "game_loop_observability_packet_assembler.hpp"
#include "game_loop_render_budget_observability_view_contracts.hpp"
#include "game_loop_render_budget_observability_view_assembler.hpp"
#include "game_loop_render_budget_presentation_view_contracts.hpp"
#include "game_loop_render_budget_presentation_view_assembler.hpp"
#include "game_loop_render_budget_overlay_text_view_contracts.hpp"
#include "game_loop_render_budget_overlay_text_view_assembler.hpp"
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

Write-Host "Compiling passive observability headers with SH2 toolchain"
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0)
{
    throw "Observability header validation failed with exit code $LASTEXITCODE"
}

Write-Host "Observability header validation passed."
