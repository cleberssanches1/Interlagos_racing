#pragma once

#include <algorithm>
#include <cstdint>

// Presentation-only camera response. The car's collision impulse remains in physics.
// All coordinates and normals use signed 16.16 fixed point; no track query is made.
namespace CameraWallImpact
{
struct State
{
    int32_t normalXRaw = 0;
    int32_t normalZRaw = 0;
    int32_t planeXRaw = 0;
    int32_t planeZRaw = 0;
    uint8_t responseFrames = 0;
    uint8_t guardFrames = 0;
    bool previousHit = false;
};

inline void Reset(State& state) { state = State{}; }

inline void Observe(State& state, bool hit, int16_t pushX, int16_t pushZ,
                    int32_t carXRaw, int32_t carZRaw)
{
    constexpr uint8_t kResponseFrames = 12;
    constexpr uint8_t kGuardFrames = 18;
    constexpr int32_t kCarSideInsetRaw = 8 << 16;
    if (hit)
    {
        if (!state.previousHit) state.responseFrames = kResponseFrames;
        const int32_t maxAxis = std::max<int32_t>(
            (pushX < 0) ? -static_cast<int32_t>(pushX) : pushX,
            (pushZ < 0) ? -static_cast<int32_t>(pushZ) : pushZ);
        if (maxAxis > 0)
        {
            // Chebyshev normalization avoids a square root on the Master SH-2.
            state.normalXRaw = static_cast<int32_t>(
                (static_cast<int64_t>(pushX) * 65536) / maxAxis);
            state.normalZRaw = static_cast<int32_t>(
                (static_cast<int64_t>(pushZ) * 65536) / maxAxis);
            // Conservative contact half-space: keep the camera at least as far
            // inside the track as a point eight units behind the car center.
            state.planeXRaw = carXRaw - static_cast<int32_t>(
                (static_cast<int64_t>(state.normalXRaw) * kCarSideInsetRaw) >> 16);
            state.planeZRaw = carZRaw - static_cast<int32_t>(
                (static_cast<int64_t>(state.normalZRaw) * kCarSideInsetRaw) >> 16);
            state.guardFrames = kGuardFrames;
        }
    }
    state.previousHit = hit;
}

inline int32_t PositionBlendRaw(const State& state, int32_t normalBlendRaw)
{
    if (state.responseFrames > 6) return 11796; // 0.18: impact inertia
    if (state.responseFrames > 0) return 26214; // 0.40: catch-up
    return normalBlendRaw;
}

inline int32_t HeadingBlendRaw(const State& state, int32_t normalBlendRaw)
{
    if (state.responseFrames > 6) return 5243;  // 0.08
    if (state.responseFrames > 0) return 11796; // 0.18
    return normalBlendRaw;
}

inline int32_t LookBlendRaw(const State& state)
{
    return (state.responseFrames > 6) ? 22938 : 39322; // 0.35 / 0.60
}

inline void Advance(State& state)
{
    if (state.responseFrames > 0) --state.responseFrames;
}

inline void KeepCameraOnTrackSide(State& state,
                                  int32_t carXRaw, int32_t carZRaw,
                                  int32_t& cameraXRaw, int32_t& cameraZRaw)
{
    if (state.guardFrames == 0) return;
    const int64_t nx = state.normalXRaw;
    const int64_t nz = state.normalZRaw;
    const int64_t normalSqRaw = ((nx * nx) + (nz * nz)) >> 16;
    if (normalSqRaw <= 0) return;

    const int64_t distanceRaw =
        (((static_cast<int64_t>(cameraXRaw) - state.planeXRaw) * nx) +
         ((static_cast<int64_t>(cameraZRaw) - state.planeZRaw) * nz)) >> 16;
    const int64_t carDistanceRaw =
        (((static_cast<int64_t>(carXRaw) - state.planeXRaw) * nx) +
         ((static_cast<int64_t>(carZRaw) - state.planeZRaw) * nz)) >> 16;
    // If the car has passed the old contact plane, it belongs to another
    // stretch of track; an infinite extension of that wall must not pin us.
    if (carDistanceRaw < 0)
    {
        state.guardFrames = 0;
        return;
    }
    constexpr int32_t kInsideMarginRaw = 2 << 16;
    if (distanceRaw >= kInsideMarginRaw)
    {
        // Release only after consecutive frames where the natural chase
        // position is already safe. Never expire while the boom crosses wall.
        if (!state.previousHit) --state.guardFrames;
        return;
    }

    // Slide along the wall tangent, preserving chase distance and height.
    const int64_t correctionX =
        ((static_cast<int64_t>(kInsideMarginRaw) - distanceRaw) * nx) / normalSqRaw;
    const int64_t correctionZ =
        ((static_cast<int64_t>(kInsideMarginRaw) - distanceRaw) * nz) / normalSqRaw;
    cameraXRaw = static_cast<int32_t>(static_cast<int64_t>(cameraXRaw) + correctionX);
    cameraZRaw = static_cast<int32_t>(static_cast<int64_t>(cameraZRaw) + correctionZ);
}
} // namespace CameraWallImpact
