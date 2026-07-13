# Memory Budget Category Consumer Matrix

## Objective

Map the current `Memory Budget` categories to their real consumers, ownership
boundaries, and safest substitution shape.

This document is inventory-only.

It does not authorize runtime migration by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged

## Category matrix

| Category | Current consumer | Current file | Current shape | Current status | Best future boundary |
| --- | --- | --- | --- | --- | --- |
| `TrackRender` | former passive render packet enrichment | former `src/game_loop_track_render_packet_assembler.hpp` | `QueryCategoryPolicy(...)` into `TrackRenderFramePacket` | removed after smoke-only phase | `RenderBudgetObservabilityViewPacket` or one narrow render-budget consumer |
| `CarRender` | passive render packet enrichment | `src/game_loop_car_visual_packet_assembler.hpp` | `QueryCategoryPolicy(...)` into `CarVisualFramePacket` | passive only | `RenderBudgetObservabilityViewPacket` or one narrow render-budget consumer |
| `AudioPcm` | PCM allocation setup | `src/car_audio_system.hpp` | `ConfigurePcmStreamingBudgetFromPolicy()` | live neutral | keep bridge boundary only |
| `Hud` | optional periodic HUD stats gate | `src/game_loop_system.hpp` | `ShouldAvoidHudOptionalTelemetry()` | live neutral | keep bridge boundary only |
| `CdStaging` | bootstrap staging preference | `src/main.cxx` | `ShouldPreferCartForCdStaging()` | live neutral | keep bridge boundary only |
| `DebugTransient` | optional debug telemetry gate | `src/game_loop_system.hpp` | `ShouldAvoidDebugTransientOptionalTelemetry()` | live neutral | keep bridge boundary only |

## Consumer families

### Live-neutral bridge consumers

These categories are already consumed through narrow bridge accessors:

- `AudioPcm`
- `Hud`
- `CdStaging`
- `DebugTransient`

Common rule:

- keep the policy assembly behind `MemoryBudgetRuntimeBridge`
- do not widen those sites to `MemoryBudgetFramePacket`

### Passive render-budget consumers

These categories are currently packet-enriched only:

- `TrackRender`
- `CarRender`

Current passive chain:

1. `QueryCategoryPolicy(...)`
2. `TrackRenderFramePacket` / `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

Recommended next passive consumer shape:

- observability/debug-only consumer
- presentation-adjacent summary
- no render scheduling ownership move

## Best substitution order by category

1. `Hud`
2. `DebugTransient`
3. `CdStaging`
4. `AudioPcm`
5. `TrackRender`
6. `CarRender`

Rationale:

- the first four already use stable bridge boundaries
- the last two are close to render path ownership and should stay passive longer

## Explicit non-goals

Do not do these in the first runtime retry:

- move `TrackRender` policy into scheduling logic
- move `CarRender` policy into submission ownership
- replace multiple category consumers in one patch
- consume broad memory frame packets in render/bootstrap hosts

## Related documents

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
