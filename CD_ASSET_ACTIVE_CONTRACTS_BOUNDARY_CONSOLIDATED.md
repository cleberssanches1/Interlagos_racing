# CD Asset Active Contracts Boundary Consolidated

## Objective

Consolidate the currently active `bootstrap/CD` contracts that still
participate in the live runtime path, without changing boot order, CD timing,
or asset ownership.

This document is runtime-shape inventory only.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- bootstrap ownership remains in `src/main.cxx`
- CD read timing remains unchanged
- SBA/car-anchor fallback order remains unchanged

## Active contract families

The live `bootstrap/CD` path now relies on four active contract groups.

One narrow bridge packet now also exists above the already-accepted SBA and
anchor decisions, but only as a local composition of those existing live
decisions.

### 1. Narrow bootstrap runtime bridge

Files:

- `src/cd_asset_bootstrap_runtime_bridge.hpp`
- `src/cd_asset_transition_ops.hpp`
- `src/game_loop_cd_asset_decision_bridge_assembler.hpp`
- `src/game_loop_cd_asset_sba_decision_contracts.hpp`
- `src/game_loop_cd_asset_anchor_decision_contracts.hpp`
- `src/game_loop_cd_asset_decision_bridge_contracts.hpp`

Role:

- build one narrow SBA bootstrap decision
- build one narrow car-anchor bootstrap decision
- optionally compose one narrow bridge packet from those already-built live
  decisions
- keep request/resolve/load details outside the host bootstrap call site

Live posture:

- active
- bridge queries are consumed directly by `src/main.cxx`
- broader CD frame/request/read/parse packets remain off-path

### 2. SBA bootstrap local consumer boundary

Files:

- `src/main.cxx`

Local helper:

- `LoadSbaShadowBootstrapAssetsIfEnabled(...)`

Role:

- consume `CdAssetSbaBootstrapDecisionPacket`
- return one narrow SBA bootstrap runtime state to the host
- keep `ModelObject` construction local
- keep `MeshRenderer` construction local
- keep SBA enable/disable gating local
- keep bootstrap order and fallback timing unchanged

Live posture:

- active
- local helper only
- no ownership migration away from `src/main.cxx`
- no duplicated local anchor consumer remains after the accepted cut

### 3. Car-anchor bootstrap local consumer boundary

Files:

- `src/main.cxx`

Local helper:

- `BuildCarBootstrapVisualConfig(...)`
- `BuildBootstrapCarSystem(...)`
- `ApplyBootstrapCarVisualYaw(...)`
- `BuildBootstrapCarRuntimeState(...)`
- `BuildCarAnchorBootstrapFallback(...)`
- `ResolveCarAnchorBootstrapFallback(...)`
- `ResolveCarBootstrapVisualYawSelection(...)`
- `ResolveCarBootstrapVisualYawSetup(...)`
- `DescribeCarBootstrapVisualYawSource(...)`

Role:

- consume `CdAssetAnchorBootstrapDecisionPacket`
- return one narrow anchor bootstrap fallback result to the host
- compose one narrow bootstrap car runtime state for the host
- keep `CarSystem::Config` assembly local
- keep `CarSystem` creation gate local
- keep final visual-yaw application local
- keep spawn/camera bootstrap state application local
- keep anchor fallback math local
- keep final marker-vs-anchor visual-yaw selection local
- keep marker-mesh probe/debug aggregation local
- keep marker-first selection local
- keep gameplay/visual yaw derivation local

Live posture:

- active
- local helper only
- no ownership migration away from `src/main.cxx`

### 4. CD staging preference bridge

Files:

- `src/memory_budget_runtime_bridge.hpp`
- `src/main.cxx`

Role:

- answer `ShouldPreferCartForCdStaging()`
- keep staging preference query narrow and bootstrap-local

Live posture:

- active
- bridge-level only
- does not move broader memory-budget ownership into bootstrap

## Current live call shape

The active bootstrap/CD call graph is now intentionally narrow:

1. bootstrap asks whether cart should be preferred for CD staging
2. bootstrap resolves one SBA runtime state from a narrow decision surface
3. bootstrap derives local anchor fallback data from a narrow bridge helper
4. bootstrap keeps final construction, sequencing, and fallback ownership local

## Current active entry points

The active runtime boundary is entered through these host-owned sites:

- `src/main.cxx`
- `src/cd_asset_bootstrap_runtime_bridge.hpp`
- `src/memory_budget_runtime_bridge.hpp`

The highest live call-site families currently are:

- `CdAssetBootstrapRuntimeBridge::BuildSbaShadowModelDecision()`
- `CdAssetBootstrapRuntimeBridge::BuildCarAnchorDecision()`
- `CdAssetBootstrapRuntimeBridge::BuildSbaAnchorDecisionBridge(...)`
- `LoadSbaShadowBootstrapAssetsIfEnabled(...)`
- `BuildBootstrapCarRuntimeState(...)`
- `BuildCarAnchorBootstrapFallback(...)`
- `ResolveCarAnchorBootstrapFallback(...)`
- `MemoryBudgetRuntimeBridge::ShouldPreferCartForCdStaging()`

## What still stays outside

The following concerns remain intentionally outside this consolidated active
boundary:

- broad `CdAssetFramePacket` runtime integration
- broad request/read/parse packet consumption in `src/main.cxx`
- asynchronous CD job behavior
- generalized `CdAssetSystem` bootstrap facade ownership
- track/car runtime integration

## Why this boundary is now cleaner

The current shape makes explicit that:

- live bootstrap/CD use is narrow and local
- SBA and anchor boundaries are separate and substitutional
- the live bridge packet is composed through one narrow runtime-bridge helper
- the final bootstrap car setup is now composed through one narrow local helper
- staging preference remains a bridge query, not a broad policy move
- boot sequencing and asset construction still belong to `src/main.cxx`

## Recommended next move

Do next:

1. keep this active boundary stable
2. avoid widening live bootstrap use into broad CD frame/request/read/parse
   packets
3. treat the current local bootstrap seam as final enough for this branch
4. prefer another subsystem unless a new explicit bootstrap runtime goal is
   intentionally chosen

Do not do next:

- move bootstrap sequencing off the host in one patch
- mix bootstrap/CD runtime changes with track/audio/scheduler changes
- introduce background CD behavior through this boundary
