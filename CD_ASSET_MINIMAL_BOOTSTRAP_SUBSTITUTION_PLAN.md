# Cd Asset Minimal Bootstrap Substitution Plan

## Objective

Define the smallest acceptable future bootstrap substitution order for CD asset
loading, using only the already prepared narrow packets for:

- `SBA` shadow model
- `car anchors`

This plan exists because `src/main.cxx` already proved sensitive to even very
small helper substitutions.

## Bootstrap boundaries covered

Only these two bootstrap use sites are in scope:

- SBA shadow model path/load site in `src/main.cxx`
- car-anchor fallback site in `src/main.cxx`

No other bootstrap or runtime CD integration should happen in the same patch
series.

## Narrow packets to use

### SBA boundary

Use only:

- `CdAssetSbaBootstrapDecisionPacket`

Current decision surface:

- `shouldResolveSbaPath`
- `hasResolvedSbaPath`
- `shouldAttemptSbaModelLoad`
- `resolvedSbaPath`
- `candidateCount`

### Car-anchor boundary

Use only:

- `CdAssetAnchorBootstrapDecisionPacket`

Current decision surface:

- `shouldResolveAnchorAsset`
- `shouldReadAnchorAsset`
- `shouldParseAnchorAsset`
- `hasValidAnchors`
- `shouldUseAnchorFallback`
- `anchors`
- `candidateCount`

## Required substitution order

### Step 1 - SBA boundary first

The first bootstrap retry, if any, must target only the SBA load site.

Patch shape:

1. assemble the narrow SBA packet locally
2. consume only `CdAssetSbaBootstrapDecisionPacket`
3. replace only equivalent local resolve/load gating
4. keep existing model construction and mesh validation ownership in place

Must remain unchanged:

- boot order
- render bootstrap order
- `ModelObject` construction ownership
- `MeshRenderer` construction ownership
- fallback candidate ordering

Status:

- applied with `src/cd_asset_bootstrap_runtime_bridge.hpp`
- current live cut only replaces the local load gate in `src/main.cxx`
- current implementation still resolves the path via the stable runtime helper
  `CdAssetDomain::ResolveSbaShadowModelPath()`
- validated with stable ISO `4134912`

### Step 2 - car-anchor boundary second

Only after repeated stable runs from Step 1:

1. assemble the narrow anchor packet locally
2. consume only `CdAssetAnchorBootstrapDecisionPacket`
3. replace only equivalent local anchor fallback gating
4. keep current yaw/fallback computations in place

Must remain unchanged:

- marker-based visual yaw path
- anchor-based fallback math ownership
- gameplay yaw derivation ownership
- bootstrap sequencing around car setup

Status:

- applied with `src/cd_asset_bootstrap_runtime_bridge.hpp`
- current live cut only replaces the local anchor fallback gate in `src/main.cxx`
- current implementation still loads anchors via the stable runtime helper
  `CdAssetDomain::LoadCarAnchorPointsAsset()`
- validated with stable ISO `4134912`

## What must not be pulled into `src/main.cxx`

Do not reintroduce these directly into the bootstrap call site first:

- `CdAssetFramePacket`
- `CdAssetBootstrapDecisionPacket`
- `CdAssetRequestPacket`
- `CdAssetReadPacket`
- `CdAssetParsePacket`
- `CdAssetTelemetry`

Those structures may remain upstream/off-path, but the live bootstrap boundary
should consume only the narrow packet prepared for it.

Preferred compile-only narrowing directly above those bootstrap boundaries:

- `CdAssetBootstrapDecisionBridgePacket`

## Remove-first rule

Each bootstrap patch must be substitutional.

That means:

- if a narrow packet field replaces a local branch, the original branch must be
  removed in the same patch
- if no branch is removed, the packet must stay compile-only

## Acceptance criteria

Every bootstrap patch in this sequence must keep:

- ISO exactly `4134912`
- emulator startup stable
- no invalid opcode
- no silent close on startup
- no boot-order drift
- no SBA/anchor fallback behavior drift

## Validation ritual

Required after every bootstrap attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Abort conditions

Rollback immediately if:

- ISO grows above `4134912`
- emulator no longer boots
- invalid opcode appears
- a boundary needs more than its narrow packet
- bootstrap sequencing starts to move together with decision substitution
