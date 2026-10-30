#pragma once

#include <cstdint>

#include "npc_rail_route.hpp"

// Pose-only PATH follower for side rails. No physics, audio, or mesh.
namespace NpcRailFollower
{
struct State
{
    uint16_t pointIndex = 0;
    int16_t stepUnits = 10;
    int16_t yawDeg = 0;
    uint8_t groundSampleCountdown = 0;
    uint8_t flags = 0;
    int32_t contactKickXRaw = 0;
    int32_t contactKickZRaw = 0;
    SRL::Math::Types::Vector3D pos{};

    static constexpr uint8_t kActiveBit = 1u << 0;

    bool Active() const { return (flags & kActiveBit) != 0u; }
    void SetActive(bool enabled)
    {
        if (enabled) flags |= kActiveBit;
        else flags = static_cast<uint8_t>(flags & static_cast<uint8_t>(~kActiveBit));
    }
};

inline void Reset(State& state)
{
    state = {};
}

inline bool Bind(State& state,
                 const NpcRailRoute& route,
                 uint16_t startIndex,
                 int16_t stepUnits)
{
    Reset(state);
    if (!HasNpcRailMinimumPoints(route)) return false;

    const size_t count = route.centers.size();
    state.pointIndex = static_cast<uint16_t>(startIndex % static_cast<uint16_t>(count));
    state.stepUnits = stepUnits > 0 ? stepUnits : 1;
    state.pos = route.centers[state.pointIndex];
    if (HasNpcRailYawAtIndex(route, state.pointIndex))
    {
        state.yawDeg = route.yawDeg[state.pointIndex];
    }
    state.groundSampleCountdown = 0;
    state.SetActive(true);
    return true;
}

inline void AdvanceHorizontal(State& state,
                              const SRL::Math::Types::Vector3D& current,
                              const SRL::Math::Types::Vector3D& next)
{
    const int32_t ndx = next.X.RawValue() - current.X.RawValue();
    const int32_t ndz = next.Z.RawValue() - current.Z.RawValue();
    const int32_t nAdx = (ndx < 0) ? -ndx : ndx;
    const int32_t nAdz = (ndz < 0) ? -ndz : ndz;
    const int32_t maxAxis = (nAdx > nAdz) ? nAdx : nAdz;
    if (maxAxis <= 0) return;

    const int32_t requestedStepRaw = (static_cast<int32_t>(state.stepUnits) << 16);
    const int32_t stepRaw = (requestedStepRaw < maxAxis) ? requestedStepRaw : maxAxis;
    const int64_t moveX64 =
        (static_cast<int64_t>(ndx) * static_cast<int64_t>(stepRaw)) / maxAxis;
    const int64_t moveZ64 =
        (static_cast<int64_t>(ndz) * static_cast<int64_t>(stepRaw)) / maxAxis;
    state.pos.X += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveX64));
    state.pos.Z += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveZ64));
}

inline void AddContactKick(State& state, int32_t xRaw, int32_t zRaw)
{
    constexpr int32_t kMaxKickRaw = 6 << 16;
    const int64_t x = static_cast<int64_t>(state.contactKickXRaw) + xRaw;
    const int64_t z = static_cast<int64_t>(state.contactKickZRaw) + zRaw;
    state.contactKickXRaw = static_cast<int32_t>(
        x < -kMaxKickRaw ? -kMaxKickRaw : (x > kMaxKickRaw ? kMaxKickRaw : x));
    state.contactKickZRaw = static_cast<int32_t>(
        z < -kMaxKickRaw ? -kMaxKickRaw : (z > kMaxKickRaw ? kMaxKickRaw : z));
}

inline void RecoverTowardRail(State& state,
                              const SRL::Math::Types::Vector3D& current,
                              const SRL::Math::Types::Vector3D& next)
{
    const int32_t segX = next.X.RawValue() - current.X.RawValue();
    const int32_t segZ = next.Z.RawValue() - current.Z.RawValue();
    const int64_t lenSq = static_cast<int64_t>(segX) * segX +
                          static_cast<int64_t>(segZ) * segZ;
    const int64_t divisor = lenSq >> 16;
    if (divisor <= 0) return;
    const int32_t fromX = state.pos.X.RawValue() - current.X.RawValue();
    const int32_t fromZ = state.pos.Z.RawValue() - current.Z.RawValue();
    const int64_t dot = static_cast<int64_t>(fromX) * segX +
                        static_cast<int64_t>(fromZ) * segZ;
    int64_t tRaw = dot / divisor;
    if (tRaw < 0) tRaw = 0;
    if (tRaw > (1 << 16)) tRaw = 1 << 16;
    const int32_t targetX = current.X.RawValue() + static_cast<int32_t>(
        (static_cast<int64_t>(segX) * tRaw) >> 16);
    const int32_t targetZ = current.Z.RawValue() + static_cast<int32_t>(
        (static_cast<int64_t>(segZ) * tRaw) >> 16);
    int32_t errorX = targetX - state.pos.X.RawValue();
    int32_t errorZ = targetZ - state.pos.Z.RawValue();
    const int32_t absX = errorX < 0 ? -errorX : errorX;
    const int32_t absZ = errorZ < 0 ? -errorZ : errorZ;
    const int32_t maxAxis = absX > absZ ? absX : absZ;
    constexpr int32_t kMaxRecoveryRaw = 2 << 16;
    if (maxAxis > kMaxRecoveryRaw)
    {
        errorX = static_cast<int32_t>(
            (static_cast<int64_t>(errorX) * kMaxRecoveryRaw) / maxAxis);
        errorZ = static_cast<int32_t>(
            (static_cast<int64_t>(errorZ) * kMaxRecoveryRaw) / maxAxis);
    }
    state.pos.X += SRL::Math::Types::Fxp::BuildRaw(errorX);
    state.pos.Z += SRL::Math::Types::Fxp::BuildRaw(errorZ);
}

inline void Step(State& state,
                 NpcRailRoute& route,
                 TrackSystem* trackSystem,
                 const SRL::Math::Types::Vector3D& trackSegOffset,
                 uint8_t groundSamplePeriod = 8u)
{
    if (!state.Active()) return;
    if (!HasNpcRailMinimumPoints(route))
    {
        state.SetActive(false);
        return;
    }

    const size_t count = route.centers.size();
    size_t current = static_cast<size_t>(state.pointIndex) % count;
    size_t next = (current + 1u) % count;

    for (size_t guard = 0u; guard < 8u; ++guard)
    {
        if (!HasReachedOrPassedNpcWaypoint(route, current, next, state.pos))
        {
            break;
        }
        current = next;
        next = (current + 1u) % count;
    }
    state.pointIndex = static_cast<uint16_t>(current);

    const auto& currentCenter = route.centers[current];
    const auto& nextCenter = route.centers[next];
    AdvanceHorizontal(state, currentCenter, nextCenter);
    const auto guidedPos = state.pos;
    const bool hasContactKick = state.contactKickXRaw != 0 ||
                                state.contactKickZRaw != 0;
    state.pos.X += SRL::Math::Types::Fxp::BuildRaw(state.contactKickXRaw);
    state.pos.Z += SRL::Math::Types::Fxp::BuildRaw(state.contactKickZRaw);
    state.contactKickXRaw = static_cast<int32_t>(
        (static_cast<int64_t>(state.contactKickXRaw) * 3) / 4);
    state.contactKickZRaw = static_cast<int32_t>(
        (static_cast<int64_t>(state.contactKickZRaw) * 3) / 4);
    if (state.contactKickXRaw > -(1 << 12) && state.contactKickXRaw < (1 << 12))
        state.contactKickXRaw = 0;
    if (state.contactKickZRaw > -(1 << 12) && state.contactKickZRaw < (1 << 12))
        state.contactKickZRaw = 0;
    RecoverTowardRail(state, currentCenter, nextCenter);
    if (hasContactKick && trackSystem != nullptr)
    {
        static constexpr uint8_t kAsphalt = 1u;
        SRL::Math::Types::Fxp surfaceY{};
        if (!trackSystem->FindSurfaceYBySurfaceTypeSet(
                state.pos, trackSegOffset, &kAsphalt, 1u,
                surfaceY, nullptr, -1, false))
        {
            state.pos.X = guidedPos.X;
            state.pos.Z = guidedPos.Z;
            state.contactKickXRaw = 0;
            state.contactKickZRaw = 0;
        }
        else
        {
            state.pos.Y = surfaceY;
        }
    }

    if (HasNpcRailYawAtIndex(route, current))
    {
        state.yawDeg = route.yawDeg[current];
    }

    if (groundSamplePeriod == 0u) groundSamplePeriod = 1u;
    if (state.groundSampleCountdown == 0u)
    {
        state.groundSampleCountdown = groundSamplePeriod;
        if (trackSystem != nullptr)
        {
            state.pos.Y = ResolveNpcRailGroundYAt(
                route, *trackSystem, trackSegOffset, current, state.pos.Y);
        }
        else
        {
            state.pos.Y = currentCenter.Y;
        }
    }
    else
    {
        --state.groundSampleCountdown;
    }
}
} // namespace NpcRailFollower
