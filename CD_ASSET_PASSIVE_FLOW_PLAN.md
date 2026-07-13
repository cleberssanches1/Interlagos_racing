# Cd Asset Passive Flow Plan

## Objective

Prepare the extraction of CD/bootstrap asset orchestration out of `src/main.cxx`
without changing the current boot sequence.

Consolidated chain document:

- `CD_BOOTSTRAP_CHAIN_FLOW_PLAN.md`

## Current passive building blocks

- `src/cd_asset_contracts.hpp`
- `src/cd_asset_parse_assembler.hpp`
- `src/cd_asset_transition_ops.hpp`
- `src/cd_asset_system.hpp`
- `src/game_loop_cd_asset_packet.hpp`
- `src/game_loop_cd_asset_packet_assembler.hpp`
- `src/game_loop_cd_asset_bootstrap_decision_contracts.hpp`
- `src/game_loop_cd_asset_bootstrap_decision_assembler.hpp`
- `src/game_loop_cd_asset_sba_decision_contracts.hpp`
- `src/game_loop_cd_asset_sba_decision_assembler.hpp`
- `src/game_loop_cd_asset_anchor_decision_contracts.hpp`
- `src/game_loop_cd_asset_anchor_decision_assembler.hpp`
- `src/game_loop_cd_asset_decision_bridge_contracts.hpp`
- `src/game_loop_cd_asset_decision_bridge_assembler.hpp`

These files already describe a passive CD asset path for:

- request assembly
- candidate resolution
- read assembly
- parse assembly
- telemetry assembly
- frame-local aggregation of bootstrap asset state

## Current bootstrap touch points

The CD/bootstrap domain is still consumed directly in `src/main.cxx`, mainly for:

1. SBA shadow model path resolution
2. car anchor JSON loading
3. direct candidate arrays for bootstrap assets

This means the domain already has passive helpers, but the bootstrap still owns:

- candidate lists
- sequencing decisions
- direct one-off parse calls

## Passive packet model

`src/game_loop_cd_asset_packet.hpp` aggregates:

- `CdAssetRequestPacket`
- `CdAssetReadPacket`
- `CdAssetParsePacket`
- `CdAssetTelemetry`
- `CarAnchorParseResult`

This stays:

- frame/bootstrap-local
- non-owning
- runtime-neutral until explicit integration is needed

## Narrow bootstrap decision layer

`CdAssetBootstrapDecisionPacket` now narrows `CdAssetFramePacket` down to the
smallest bootstrap-facing decision surface currently worth keeping off-path:

- request validity
- asset kind
- resolve/read/parse intent
- resolved-path presence
- read/parse success
- car-anchor parse validity
- candidate count
- chunk size

This exists to:

- prepare a future bootstrap-local substitution without carrying the broader
  request/read/parse packet structure into `src/main.cxx`
- keep the first live retry focused on explicit CD bootstrap decisions only
- avoid re-deriving the same resolve/read/parse booleans at bootstrap use sites

## Runtime-to-passive substitution map

### Request/candidate assembly

Passive coverage already available:

1. `CdAssetRequestPacket`
2. `CdAssetCandidateSet`

Current bootstrap-owned source data:

- SBA candidate path arrays
- car-anchor candidate path arrays
- logical asset names
- read/parse intent flags

### Read and resolve

Passive coverage already available:

1. `CdAssetDomain::ResolveExistingPath(...)`
2. `CdAssetDomain::ReadBinaryAsset(...)`
3. `CdAssetReadPacket`

Current bootstrap-relevant outputs:

- resolved path
- read chunk size
- read success/failure

### Parse and telemetry

Passive coverage already available:

1. `CdAssetParsePacket`
2. `CdAssetDomain::SeedCarAnchorParseResult(...)`
3. `CdAssetDomain::SeedCdAssetTelemetry(...)`

Current bootstrap-relevant outputs:

- parsed car-anchor validity
- candidate count
- bytes read
- resolved candidate index
- parse success/failure

## Safe integration order

### Step 1 - keep boot order unchanged

Do not change:

- boot-stage ordering
- current CD read timing
- current fallback ordering for asset candidates
- current SBA/car-anchor usage sites

### Step 2 - packetize bootstrap state only

When runtime integration becomes safe, assemble locally:

1. `CdAssetRequestPacket`
2. `CdAssetReadPacket`
3. `CdAssetParsePacket`
4. `CdAssetTelemetry`
5. `CarAnchorParseResult`
6. `CdAssetFramePacket`

Consume immediately in the same bootstrap scope.

No new persistent members.
No async job system.
No boot-policy changes.

### Step 3 - remove repeated candidate arrays first

The best first runtime/bootstrap candidate is:

- unify SBA candidate-path handling
- unify anchor candidate-path handling
- centralize request/telemetry metadata

This is lower risk than changing read timing or introducing retries/jobs.

### Step 4 - only later isolate `CdAssetSystem`

Only after repeated stable emulator runs:

- move candidate definitions behind explicit requests
- centralize telemetry for CD bootstrap loads
- then consider a true `CdAssetSystem` bootstrap facade

## Guard rails

- do not touch boot sequencing and track init ordering in the same patch
- do not introduce background CD job behavior yet
- do not mix this with track or car runtime cuts
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Immediate next safe step

The next safe step is:

1. keep `src/main.cxx` runtime behavior unchanged
2. use this packet only as passive groundwork
3. later try a bootstrap-local cut that centralizes SBA/anchor request metadata

A safe bootstrap-side substitution now also validated for this slice is:

- remove duplicated SBA candidate arrays from `src/main.cxx`
- consume `CdAssetDomain::ResolveSbaShadowModelPath()` directly at the existing use sites

This reduces bootstrap-local duplication without changing boot order, read timing or fallback behavior.

Another safe passive step now available for this slice is:

- explicit request builders for `SBA.NYA` and `CAR1_ANCHORS.JSON`
- explicit read/parse/telemetry builders in `src/cd_asset_transition_ops.hpp`
- explicit `CdAssetFramePacket` builder in `src/game_loop_cd_asset_packet_assembler.hpp`

This means the passive CD side now has a complete request-to-frame-packet path
ready for future substitutional bootstrap cuts.

An additional safe passive step now also exists for this slice:

- derive one `CdAssetBootstrapDecisionPacket` from `CdAssetFramePacket`
- keep that decision packet compile-only for now

This means a future bootstrap retry can target a narrow decision packet first,
instead of pulling the broader CD asset packet structure into `src/main.cxx`.

## Current branch interpretation

At the current branch state, `Bootstrap / CD` is no longer a mandatory next
runtime target.

Reason:

- the accepted narrow bootstrap runtime seam is already in place
- the remaining local bootstrap host work has already been reduced behind
  explicit helpers
- boot order and file timing remained unchanged through the accepted cuts
- any broader bootstrap move now counts as a new explicit runtime goal

Two even narrower bootstrap-side cuts now also exist:

- `CdAssetSbaBootstrapDecisionPacket`
- `CdAssetAnchorBootstrapDecisionPacket`

One bridge-level compile-only cut now also exists above them:

- `CdAssetBootstrapDecisionBridgePacket`

Current effect:

- SBA shadow-model path/load intent can now be represented without carrying the
  broader bootstrap packet into the future use site
- car-anchor fallback availability can now be represented together with parsed
  anchor data, without rebuilding fallback booleans at the future use site
- both slices remain compile-only and off-path for now
- the future SBA and anchor bootstrap boundaries can share one narrow bridge
  without carrying the broader CD asset frame packet into `src/main.cxx`

The exact future retry order for these narrow packets is now documented in:

- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`

## Applied safe runtime bridge

A first bootstrap-side runtime cut is now in place for `CdStaging` preference:

- `src/memory_budget_runtime_bridge.hpp`
- `src/main.cxx`

Current behavior remains intentionally equivalent:

- bootstrap still treats CD staging as cart-preferred when cart RAM exists
- no boot-stage order changed
- no track/car loader sequencing changed
- no file-read timing changed

Current runtime consumption is intentionally minimal:

- derive `CdStaging` preferred pool from passive memory-budget category policy
- consume it only in bootstrap-local cart-availability checks
- keep direct asset-loading flows untouched for now

## Applied safe bootstrap runtime bridges

Two narrow bootstrap substitutions are now in place for the current CD slice:

- `src/cd_asset_bootstrap_runtime_bridge.hpp`
- `src/main.cxx`

Current behavior remains intentionally equivalent:

- bootstrap still resolves the `SBA` path through
  `CdAssetDomain::ResolveSbaShadowModelPath()`
- bootstrap still loads `CAR1_ANCHORS.JSON` through
  `CdAssetDomain::LoadCarAnchorPointsAsset()`
- `ModelObject` construction ownership stays in `src/main.cxx`
- `MeshRenderer` construction ownership stays in `src/main.cxx`
- anchor fallback math ownership stays in `src/main.cxx`
- boot order and fallback timing stay unchanged

Current runtime consumption is intentionally narrow:

- build one `CdAssetSbaBootstrapDecisionPacket`
- consume only `shouldAttemptSbaModelLoad` and `resolvedSbaPath`
- build one `CdAssetAnchorBootstrapDecisionPacket`
- consume only `shouldUseAnchorFallback`, `hasValidAnchors`, and `anchors`

## Latest accepted bootstrap-side reduction

The next accepted `Bootstrap / CD` cut stayed local to `src/main.cxx` and
removed duplicated SBA shadow-model bootstrap consumption.

Applied shape:

- keep `CdAssetBootstrapRuntimeBridge::BuildSbaShadowModelDecision()` unchanged
- add one local helper that consumes only
  `CdAssetSbaBootstrapDecisionPacket`
- replace the two duplicated SBA model/bootstrap setup blocks in `src/main.cxx`
  with that one helper
- keep the SBA enable/disable gate local to `src/main.cxx`

Preserved behavior:

- boot order unchanged
- SBA path resolution unchanged
- model construction ownership unchanged
- mesh-renderer construction ownership unchanged
- fallback timing unchanged

Validation result:

- stable ISO preserved at `4134912`
- passive headers passed
- observability headers passed

One sibling accepted cut now also exists for the anchor boundary:

- keep `CdAssetBootstrapRuntimeBridge::BuildCarAnchorDecision()` unchanged
- add one local helper that consumes only
  `CdAssetAnchorBootstrapDecisionPacket`
- replace the local anchor fallback derivation block in `src/main.cxx` with
  that helper

Preserved behavior:

- boot order unchanged
- anchor load timing unchanged
- fallback yaw math ownership unchanged
- marker-first selection unchanged

Validation result:

- stable ISO preserved at `4134912`
- passive headers passed
- observability headers passed

Current interpretation:

- no duplicated local anchor consumer remains in `src/main.cxx`
- the SBA bootstrap helper surface is now reduced to one runtime-state return
  that carries both the narrow decision and the local SBA assets together
- the anchor fallback helper surface is now reduced to one structured return
- the final marker-vs-anchor visual-yaw application is now reduced to one
  narrow local helper instead of an open-coded host block
- the marker-mesh probe, yaw-selection result, and debug-source description are
  now consolidated behind one narrow visual bootstrap helper surface
- the remaining local visual bootstrap host code for config assembly, car
  creation gate, and yaw application is now narrowed behind explicit helpers
- the next bootstrap/CD move should not force a broader anchor-side cut unless
  a new explicit runtime goal justifies widening beyond the accepted local
  boundary
- one narrow live bridge composition is now acceptable only when composed from
  already-built SBA and anchor decisions, without pulling broader frame/request
  packets or anticipating anchor load timing
- that bridge composition may be consumed through one runtime-bridge helper
  instead of being assembled directly in `src/main.cxx`
- the current accepted reading for this branch is now documented in
  `CD_ASSET_BRANCH_FINAL_STATUS.md`

