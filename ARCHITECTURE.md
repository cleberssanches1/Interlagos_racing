# Interlagos Racing Architecture

## Frame Pipeline

Runtime frame order is fixed and orchestrated by `GameLoopSystem`:

1. Input and camera update
2. Car command feed (`ICarCommand`)
3. Gameplay tick (`IGameplayTick`)
4. Car physics step (`ICarPhysics`)
5. Background update
6. Track render pipeline (`TrackSystem`)
7. Car render pipeline (`CarSystem` + `RenderPipeline`)
8. Audio events (`IAudioEvents`)
9. HUD and telemetry present
10. `SRL::Core::Synchronize()`

## Core Systems

- `TrackSystem`
  - Loads and stages `.NYA` segments
  - Produces draw list on Master/Slave SH2
  - Applies budget and telemetry
  - Exposes nearest-segment query for collision/gameplay

- `CarSystem`
  - Owns render instance and command state
  - Exposes `ICarCommand` adapter
  - Maintains command smoothing and wheel spin state

- `CameraSystem`
  - Owns camera state and controller mapping
  - Produces camera position, direction and look target

- `HudSystem`
  - Owns HUD update and periodic memory/VDP1 telemetry

## Runtime Contracts

Contracts are defined in `src/interfaces.hpp`:

- `ITrackCollisionQuery`
- `ICarPhysics`
- `IGameplayTick`
- `IAudioEvents`

Current initial implementations:

- `SimpleCarPhysics`
- `SimpleGameplayTick`
- `SimpleAudioEvents`
- `TrackCollisionQueryFromSystem`

Fallback no-op implementations remain in `runtime_null_systems.hpp`.

## Segment Rendering Target

Current startup budget is configured in `main.cxx`:

- `initialSegments = 20`
- `initialMeshes = 128`
- `initialFaces = 32000`

Adaptive budget logic can still reduce submitted work under pressure to keep frame stability.
