# Memory Budget Active Contracts Boundary Consolidated

## Objective

Consolidate the currently active `memory budget` contracts that still
participate in the live runtime path, without widening allocator ownership or
changing behavior.

This document is runtime-shape inventory only.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- `src/game_loop_system.hpp` remains the host/runtime owner for frame-end and
  overlay presentation
- `src/main.cxx` remains the host/runtime owner for bootstrap staging hints
- `src/car_audio_system.hpp` remains the host/runtime owner for PCM setup timing

## Active contract families

The live `memory budget` path now relies on five active contract groups.

### 1. Runtime bridge policy boundary

Files:

- `src/memory_budget_runtime_bridge.hpp`
- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`
- `src/memory_budget_category_assembler.hpp`

Role:

- expose narrow live memory-policy decisions
- derive category policy and threshold-based runtime guidance
- keep allocator and policy ownership outside presentation call sites

Live posture:

- active through narrow bridge accessors only
- no broad memory-policy packet is consumed directly in critical runtime code

### 2. Frame-end memory debug bundle boundary

Files:

- `src/game_loop_memory_debug_contracts.hpp`
- `src/game_loop_memory_debug_packet_assembler.hpp`
- `src/game_loop_memory_debug_presenter_ops.hpp`
- `src/game_loop_memory_presentation_contracts.hpp`
- `src/game_loop_memory_presentation_state_assembler.hpp`

Role:

- derive `MemoryDebugPresentationBundle`
- group work-RAM usage, high-work trace text, and low-work trace text
- present the assembled bundle in the local frame-end path

Live posture:

- active in the host frame-end path
- bundle is assembled and consumed immediately
- no persistent ownership is introduced

### 3. Low-work overlay boundary

Files:

- `src/game_loop_low_work_overlay_assembly_ops.hpp`
- `src/game_loop_low_work_overlay_capture_ops.hpp`
- `src/game_loop_low_work_overlay_presenter_ops.hpp`
- `src/game_loop_memory_overlay_text_contracts.hpp`
- `src/game_loop_memory_overlay_text_assembler.hpp`
- `src/game_loop_memory_overlay_text_view_contracts.hpp`
- `src/game_loop_memory_overlay_text_view_assembler.hpp`

Role:

- derive low-work overlay packets and text bundles
- capture allocator/tag-group facts
- present compact/full low-work overlay output

Live posture:

- active in the host low-work overlay path
- cadence, early returns, and state ownership remain local

### 4. Trace runtime debug boundary

Files:

- `src/game_loop_memory_trace_ops.hpp`
- `src/game_loop_memory_trace_packet_assembler.hpp`
- `src/game_loop_memory_trace_text_contracts.hpp`
- `src/game_loop_memory_trace_text_assembler.hpp`
- `src/game_loop_memory_trace_text_view_contracts.hpp`
- `src/game_loop_memory_trace_text_view_assembler.hpp`
- `src/game_loop_memory_trace_text_low_work_view_contracts.hpp`
- `src/game_loop_memory_trace_text_low_work_view_assembler.hpp`
- `src/game_loop_memory_trace_runtime_debug_ops.hpp`

Role:

- capture high/low work snapshots
- derive delta packets and text packets
- present trace text through narrow runtime debug helpers

Live posture:

- active where frame-end memory debug and low-work overlay paths need it
- still host-local in timing and ordering

### 5. Observability/debug aggregation boundary

Files:

- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`
- `src/game_loop_observability_debug_contracts.hpp`
- `src/game_loop_observability_debug_packet_assembler.hpp`

Role:

- aggregate memory debug slices alongside the broader observability state
- keep debug aggregation explicit without moving allocator ownership

Live posture:

- active only where current host/runtime paths already consume the narrowed
  observability/debug boundary
- not a broad allocator/runtime migration

## Current live call shape

The active call graph is now intentionally split by use case:

1. runtime bridge accessors answer narrow policy questions for:
   - PCM setup
   - CD/bootstrap staging preference
   - optional HUD/debug telemetry gating
2. frame-end path assembles `MemoryDebugPresentationBundle`
3. frame-end path presents the bundle locally
4. low-work overlay path captures overlay-local allocator/tag facts
5. low-work overlay path assembles compact/full text bundles locally
6. trace helpers derive delta/text packets only where the local host path needs
   them

## Current active entry points

The active runtime boundary is entered through these host-owned sites:

- `src/car_audio_system.hpp`
- `src/main.cxx`
- `src/game_loop_system.hpp`

The highest live call-site families currently are:

- `MemoryBudgetRuntimeBridge::*`
- `BuildFrameEndMemoryDebugPresentationBundle(...)`
- `PresentMemoryDebugPresentationBundle(...)`
- `UpdateLowWorkFreeOverlayEnabled()`

## What still stays outside

The following concerns remain intentionally outside this consolidated active
boundary:

- allocator ownership
- `MemoryBudgetSystem` ownership
- render scheduling ownership
- broad render-budget presentation retries
- bootstrap sequencing ownership
- audio initialization ownership

## Why this boundary is now cleaner

The current shape makes explicit that:

- live memory-budget use is narrow and local
- presentation/debug consumers sit above explicit bundle/text boundaries
- bridge policy queries are separate from presentation ownership
- runtime call sites stay reversible and substitutional

## Recommended next move

Do next:

1. keep this active boundary stable
2. avoid widening live usage into broad memory snapshot/policy packets
3. prefer future work on passive/render-budget consolidation or another
   subsystem boundary

Do not do next:

- consume broad memory-policy packets directly in a first live retry
- move allocator sequencing behind this boundary
- merge bootstrap/audio/presenter/memory changes in one patch
