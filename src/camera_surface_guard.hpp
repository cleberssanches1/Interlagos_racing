#pragma once

#include <cstdint>

// Small, allocation-free state machine for the chase-camera road guard.
// Querying the mesh stays outside this class so the policy can be host-tested.
struct CameraSurfaceGuardState
{
    int32_t surfaceYRaw = 0;
    int32_t targetSurfaceYRaw = 0;
    int32_t surfaceVelocityRaw = 0;
    int16_t segmentId = -1;
    int16_t faceIndex = -1;
    uint8_t queryCooldown = 0u;
    uint8_t missCount = 0u;
    bool valid = false;
};

static_assert(sizeof(CameraSurfaceGuardState) <= 20u,
              "Camera surface guard must remain cheap in Work RAM");

class CameraSurfaceGuard
{
public:
    // One mesh query every other frame. A face hint makes the common path a
    // single face-plane evaluation through FSMAP/TrackSystem.
    static constexpr uint8_t kQueryCooldownFrames = 1u;
    static constexpr uint8_t kMissesBeforeRelease = 2u;
    static constexpr int32_t kClearanceRaw = 24 << 16;
    // Queries arrive every other frame. Interpolate the observed displacement
    // over those two rendered frames instead of imposing a fixed release speed
    // that permanently lags behind a steep descent. The cap is above the
    // chassis vertical limit but still rejects corrupt/remote face hits.
    static constexpr int32_t kMaxSurfaceReleasePerFrameRaw = 24 << 16;

    static void Reset(CameraSurfaceGuardState& state)
    {
        state = CameraSurfaceGuardState{};
    }

    static bool ShouldQuery(CameraSurfaceGuardState& state)
    {
        if (state.queryCooldown == 0u) return true;
        --state.queryCooldown;
        return false;
    }

    static void Observe(CameraSurfaceGuardState& state,
                        int32_t surfaceYRaw,
                        int32_t segmentId,
                        int32_t faceIndex)
    {
        const bool nextValid = segmentId > 0 && faceIndex >= 0;
        const bool hadValid = state.valid;
        state.targetSurfaceYRaw = surfaceYRaw;
        if (!hadValid || !nextValid)
        {
            state.surfaceYRaw = surfaceYRaw;
            state.surfaceVelocityRaw = 0;
        }
        else if (surfaceYRaw < state.surfaceYRaw)
        {
            // Smaller Y means higher terrain. Tighten immediately so the
            // camera can never be smoothed into the asphalt.
            state.surfaceYRaw = surfaceYRaw;
            state.surfaceVelocityRaw = 0;
        }
        else
        {
            const int32_t delta = surfaceYRaw - state.surfaceYRaw;
            // One observation covers the current and the following frame.
            // Round up so sub-unit fixed-point residue cannot accumulate.
            const int32_t perFrame = (delta >> 1) + (delta & 1);
            state.surfaceVelocityRaw =
                (perFrame > kMaxSurfaceReleasePerFrameRaw)
                    ? kMaxSurfaceReleasePerFrameRaw
                    : perFrame;
        }
        state.segmentId = ClampI16(segmentId);
        state.faceIndex = ClampI16(faceIndex);
        state.queryCooldown = kQueryCooldownFrames;
        state.missCount = 0u;
        state.valid = nextValid;
    }

    static void Miss(CameraSurfaceGuardState& state)
    {
        state.queryCooldown = kQueryCooldownFrames;
        if (state.missCount < 0xFFu) ++state.missCount;
        if (state.missCount >= kMissesBeforeRelease)
        {
            state.valid = false;
            state.surfaceVelocityRaw = 0;
            state.segmentId = -1;
            state.faceIndex = -1;
        }
    }

    // Y grows downward. Clamp immediately when the boom penetrates the road;
    // recovery remains smooth because CameraSystem starts the next frame from
    // the corrected position and applies its normal vertical blend.
    static int32_t ResolveYRaw(CameraSurfaceGuardState& state,
                               int32_t desiredYRaw)
    {
        if (!state.valid) return desiredYRaw;
        if (state.targetSurfaceYRaw > state.surfaceYRaw)
        {
            const int32_t delta = state.targetSurfaceYRaw - state.surfaceYRaw;
            const int32_t step =
                (delta > state.surfaceVelocityRaw)
                    ? state.surfaceVelocityRaw
                    : delta;
            state.surfaceYRaw += step;
            if (state.surfaceYRaw >= state.targetSurfaceYRaw)
            {
                state.surfaceYRaw = state.targetSurfaceYRaw;
                state.surfaceVelocityRaw = 0;
            }
        }
        const int32_t maximumCameraYRaw = state.surfaceYRaw - kClearanceRaw;
        return (desiredYRaw > maximumCameraYRaw)
            ? maximumCameraYRaw
            : desiredYRaw;
    }

private:
    static int16_t ClampI16(int32_t value)
    {
        if (value < -1) return -1;
        if (value > 32767) return 32767;
        return static_cast<int16_t>(value);
    }
};
