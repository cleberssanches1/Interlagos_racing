# CD / Bootstrap Chain Flow Plan

## Objective

Consolidate the current passive CD/bootstrap chain into one document that shows
the narrowing order, intended future bootstrap boundaries, and current
compile-only status.

This document is inventory-and-flow only.

It does not authorize runtime/bootstrap ownership changes.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- bootstrap ownership remains in `src/main.cxx`

## Chain overview

The current CD/bootstrap chain is intentionally layered from broad packet
assembly to narrow bootstrap decisions:

1. request / candidate layer
2. read / resolve layer
3. parse / telemetry layer
4. frame packet layer
5. bootstrap decision layer
6. SBA narrow decision layer
7. anchor narrow decision layer
8. bootstrap decision bridge layer

## Layer-by-layer narrowing

### 1. Request / candidate layer

Files:

- `src/cd_asset_contracts.hpp`
- `src/cd_asset_request_assembler.hpp`

Primary packets:

- `CdAssetRequestPacket`
- `CdAssetCandidateSet`

Purpose:

- preserve explicit request metadata before any bootstrap-local decisions

### 2. Read / resolve layer

Files:

- `src/cd_asset_transition_ops.hpp`

Primary packets:

- `CdAssetReadPacket`

Purpose:

- preserve resolved path, read intent, and chunk metadata

### 3. Parse / telemetry layer

Files:

- `src/cd_asset_parse_assembler.hpp`

Primary packets:

- `CdAssetParsePacket`
- `CdAssetTelemetry`
- `CarAnchorParseResult`

Purpose:

- preserve parse success/failure and anchor parse state

### 4. Frame packet layer

Files:

- `src/game_loop_cd_asset_packet.hpp`
- `src/game_loop_cd_asset_packet_assembler.hpp`

Primary packet:

- `CdAssetFramePacket`

Purpose:

- preserve one broad frame/bootstrap-local aggregation above all narrow
  bootstrap decisions

### 5. Bootstrap decision layer

Files:

- `src/game_loop_cd_asset_bootstrap_decision_contracts.hpp`
- `src/game_loop_cd_asset_bootstrap_decision_assembler.hpp`

Primary packet:

- `CdAssetBootstrapDecisionPacket`

Purpose:

- narrow the broad frame packet to explicit bootstrap-facing resolve/read/parse
  booleans and counters

### 6. SBA narrow decision layer

Files:

- `src/game_loop_cd_asset_sba_decision_contracts.hpp`
- `src/game_loop_cd_asset_sba_decision_assembler.hpp`

Primary packet:

- `CdAssetSbaBootstrapDecisionPacket`

Purpose:

- narrow the SBA shadow-model boundary to explicit path/load intent only

### 7. Anchor narrow decision layer

Files:

- `src/game_loop_cd_asset_anchor_decision_contracts.hpp`
- `src/game_loop_cd_asset_anchor_decision_assembler.hpp`

Primary packet:

- `CdAssetAnchorBootstrapDecisionPacket`

Purpose:

- narrow the car-anchor boundary to explicit resolve/read/parse/fallback intent

### 8. Bootstrap decision bridge layer

Files:

- `src/game_loop_cd_asset_decision_bridge_contracts.hpp`
- `src/game_loop_cd_asset_decision_bridge_assembler.hpp`

Primary packet:

- `CdAssetBootstrapDecisionBridgePacket`

Purpose:

- preserve one narrow bridge above the two known bootstrap boundaries
- avoid carrying `CdAssetFramePacket` into a future narrow bootstrap retry

## Boundary-focused summary

### Future SBA bootstrap boundary

Preferred passive chain:

1. `CdAssetBootstrapDecisionPacket`
2. `CdAssetSbaBootstrapDecisionPacket`
3. `CdAssetBootstrapDecisionBridgePacket`

### Future car-anchor bootstrap boundary

Preferred passive chain:

1. `CdAssetBootstrapDecisionPacket`
2. `CdAssetAnchorBootstrapDecisionPacket`
3. `CdAssetBootstrapDecisionBridgePacket`

## Current runtime/bootstrap status

- all CD/bootstrap decision layers remain compile-only
- no new passive CD/bootstrap decision packet is consumed live in `src/main.cxx`
- bootstrap sequencing, model construction, anchor fallback math, and read
  timing remain unchanged

## Remove-first rule for future bootstrap retries

Any future bootstrap retry must:

1. target one bootstrap boundary only
2. consume only the narrow packet prepared for that boundary
3. remove equivalent host-local gating in the same patch
4. keep boot order unchanged
5. keep load/parse ownership unchanged

## Validation

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `CD_ASSET_PASSIVE_FLOW_PLAN.md`
- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
