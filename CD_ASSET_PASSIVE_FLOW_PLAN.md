# Cd Asset Passive Flow Plan

## Objective

Prepare the extraction of CD/bootstrap asset orchestration out of `src/main.cxx`
without changing the current boot sequence.

## Current passive building blocks

- `src/cd_asset_contracts.hpp`
- `src/cd_asset_parse_assembler.hpp`
- `src/cd_asset_transition_ops.hpp`
- `src/cd_asset_system.hpp`
- `src/game_loop_cd_asset_packet.hpp`
- `src/game_loop_cd_asset_packet_assembler.hpp`

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

