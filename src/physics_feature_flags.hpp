#pragma once

// Central feature flags for gradual rollout of physics/ground-query changes.
// Keep defaults conservative and enable incrementally.

#ifndef PHYS_SURFACE_TYPE_QUERY
#define PHYS_SURFACE_TYPE_QUERY 1
#endif

#ifndef PHYS_FACE_CACHE
#define PHYS_FACE_CACHE 1
#endif

#ifndef PHYS_SCMAP_RUNTIME
#define PHYS_SCMAP_RUNTIME 1
#endif

#ifndef PHYS_LOCAL_FACE_NEIGHBOR
#define PHYS_LOCAL_FACE_NEIGHBOR 1
#endif

#ifndef PHYS_SAFE_TELEMETRY
#define PHYS_SAFE_TELEMETRY 1
#endif

#ifndef PHYS_WALL_COLLISION_RUNTIME
#define PHYS_WALL_COLLISION_RUNTIME 0
#endif

namespace Game::PhysicsFeatureFlags
{
static constexpr bool kEnableSurfaceTypeQuery = (PHYS_SURFACE_TYPE_QUERY != 0);
static constexpr bool kEnableFaceCache = (PHYS_FACE_CACHE != 0);
static constexpr bool kEnableScmapRuntime = (PHYS_SCMAP_RUNTIME != 0);
static constexpr bool kEnableLocalFaceNeighbor = (PHYS_LOCAL_FACE_NEIGHBOR != 0);
static constexpr bool kEnableSafeTelemetry = (PHYS_SAFE_TELEMETRY != 0);
static constexpr bool kEnableWallCollisionRuntime = (PHYS_WALL_COLLISION_RUNTIME != 0);
} // namespace Game::PhysicsFeatureFlags
