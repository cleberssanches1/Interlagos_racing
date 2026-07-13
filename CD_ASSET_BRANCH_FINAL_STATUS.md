# CD Asset / Bootstrap Branch Final Status

## Objective

Record the final accepted branch-level state for `bootstrap/CD`, separating the
accepted narrow live seam from the broader bootstrap/runtime ownership moves
that remain intentionally outside this branch.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/main.cxx` remains the live bootstrap owner
- boot order must remain unchanged
- CD file timing must remain unchanged

## Accepted active seam for this branch

The accepted live `bootstrap/CD` result for this branch is the current narrow
bootstrap-local seam only.

Active live path:

- `CdAssetBootstrapRuntimeBridge::BuildSbaShadowModelDecision()`
- `CdAssetBootstrapRuntimeBridge::BuildCarAnchorDecision()`
- `CdAssetBootstrapRuntimeBridge::BuildSbaAnchorDecisionBridge(...)`
- `LoadSbaShadowBootstrapAssetsIfEnabled(...)`
- `BuildBootstrapCarRuntimeState(...)`
- `BuildCarBootstrapVisualConfig(...)`
- `BuildBootstrapCarSystem(...)`
- `ApplyBootstrapCarVisualYaw(...)`
- `BuildCarAnchorBootstrapFallback(...)`
- `ResolveCarAnchorBootstrapFallback(...)`
- `ResolveCarBootstrapVisualYawSelection(...)`
- `ResolveCarBootstrapVisualYawSetup(...)`
- `DescribeCarBootstrapVisualYawSource(...)`
- `MemoryBudgetRuntimeBridge::ShouldPreferCartForCdStaging()`

Accepted host posture:

- bootstrap sequencing remains local to `src/main.cxx`
- `ModelObject` construction remains local
- `MeshRenderer` construction remains local
- `CarSystem` creation gate remains local
- anchor fallback math remains local
- marker-first visual-yaw behavior remains local
- staging preference remains a narrow bridge query only

## Why this is considered final enough

This branch already achieved the stable remove-first reductions that mattered:

- duplicated SBA bootstrap consumption was removed
- duplicated anchor fallback consumption was removed
- marker-vs-anchor visual-yaw selection was localized
- bootstrap visual config, gated `CarSystem` creation, and yaw application were
  reduced behind explicit local helpers
- final bootstrap car setup is now also composed through one narrow local
  runtime-state helper reused by both bootstrap flows
- emulator stability and fixed ISO envelope were preserved throughout

That is the accepted closure point for this branch.

## What remains intentionally outside the active seam

The following are explicitly deferred and are not required for branch closure:

- broad `CdAssetFramePacket` live integration in `src/main.cxx`
- broad request/read/parse packet consumption in the bootstrap host
- asynchronous/background CD bootstrap behavior
- generalized `CdAssetSystem` bootstrap facade ownership
- moving bootstrap sequencing off the host
- mixed bootstrap/CD plus track/audio/scheduler runtime patches

## Deferred rationale

Bootstrap runtime ownership remains sensitive, and the accepted branch work has
already reached the safe narrow seam without needing a broader move.

Relevant references:

- `CD_ASSET_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`
- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `CD_ASSET_PASSIVE_FLOW_PLAN.md`

## Branch-final interpretation

For the purpose of refactor closure in this branch:

- `bootstrap/CD` is considered actively consolidated at a narrow local seam
- broader bootstrap/runtime ownership migration is formally deferred
- no broader CD/bootstrap live retry is required before branch closure

## What can still happen later

Future work may still reopen `bootstrap/CD`, but only as a new explicit runtime
goal, not as part of the remaining mandatory refactor closure for this branch.
