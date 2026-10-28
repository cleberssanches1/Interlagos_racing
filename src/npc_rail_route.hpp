#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "auto_lap_npc_rail_policy.hpp"
#include "path_nya_loader.hpp"
#include "track_system.hpp"
#include "track_zone_alloc.hpp"

// Side-rail PATH storage lives in Low Work RAM and is filled only after the
// player Auto Lap start is stable (ArmNpcRailsOnce gate). Keeping these in
// HWR required ~9 KiB that the pose1 .text/.bss window no longer has
// (heapFree ~5 KiB). Do not allocate NPC routes on the Ensure/start path.
struct NpcRailRoute
{
    TrackLowWorkVectorBase<SRL::Math::Types::Vector3D> centers{};
    TrackLowWorkVectorBase<int16_t> yawDeg{};
    int8_t selectedGuideLine = -1;
    uint8_t flags = 0u;

    static constexpr uint8_t kBuiltBit = 1u << 0;

    bool Built() const { return (flags & kBuiltBit) != 0u; }
    void SetBuilt(bool enabled)
    {
        if (enabled) flags |= kBuiltBit;
        else flags = static_cast<uint8_t>(flags & static_cast<uint8_t>(~kBuiltBit));
    }
};

inline void ReleaseNpcRailRoute(NpcRailRoute& route)
{
    {
        TrackLowWorkVectorBase<SRL::Math::Types::Vector3D> emptyCenters{};
        emptyCenters.swap(route.centers);
    }
    {
        TrackLowWorkVectorBase<int16_t> emptyYaw{};
        emptyYaw.swap(route.yawDeg);
    }
    route.selectedGuideLine = -1;
    route.SetBuilt(false);
}

inline bool HasNpcRailMinimumPoints(const NpcRailRoute& route, size_t minPoints = 2u)
{
    return route.centers.size() >= minPoints;
}

inline bool HasNpcRailYawAtIndex(const NpcRailRoute& route, size_t index)
{
    return index < route.yawDeg.size() && index < route.centers.size();
}

inline bool HasNpcRailNonZeroFirstXZ(const NpcRailRoute& route)
{
    if (route.centers.empty()) return false;
    const auto& first = route.centers.front();
    return AutoLapNpcRailPolicy::IsNonZeroRouteXZ(
        first.X.RawValue(), first.Z.RawValue());
}

inline bool FindNearestNpcRailPointIndex(
    const NpcRailRoute& route,
    const SRL::Math::Types::Vector3D& worldPosition,
    size_t& outIndex)
{
    if (route.centers.empty()) return false;

    size_t nearestIndex = 0u;
    bool foundNearest = false;
    SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    for (size_t i = 0u; i < route.centers.size(); ++i)
    {
        const auto& routePoint = route.centers[i];
        const auto dx = (routePoint.X - worldPosition.X).Abs();
        const auto dz = (routePoint.Z - worldPosition.Z).Abs();
        const auto score = dx + dz;
        if (!foundNearest || score < bestScore)
        {
            foundNearest = true;
            bestScore = score;
            nearestIndex = i;
        }
    }
    if (!foundNearest) return false;
    outIndex = nearestIndex;
    return true;
}

inline bool HasReachedOrPassedNpcWaypoint(const NpcRailRoute& route,
                                           size_t fromIndex,
                                           size_t toIndex,
                                           const SRL::Math::Types::Vector3D& worldPosition)
{
    const auto& from = route.centers[fromIndex];
    const auto& to = route.centers[toIndex];
    const int32_t remX = to.X.RawValue() - worldPosition.X.RawValue();
    const int32_t remZ = to.Z.RawValue() - worldPosition.Z.RawValue();
    const int32_t segX = to.X.RawValue() - from.X.RawValue();
    const int32_t segZ = to.Z.RawValue() - from.Z.RawValue();
    const int64_t dot = static_cast<int64_t>(remX) * static_cast<int64_t>(segX) +
                        static_cast<int64_t>(remZ) * static_cast<int64_t>(segZ);
    return dot <= 0;
}

template <typename YawFn>
inline void RebuildNpcRailYaw(NpcRailRoute& route, YawFn&& yawFromDeltaRaw)
{
    route.yawDeg.clear();
    const size_t pointCount = route.centers.size();
    if (pointCount < 2u) return;

    route.yawDeg.reserve(pointCount);
    for (size_t i = 0; i < pointCount; ++i)
    {
        const size_t j = (i + 1u) % pointCount;
        const int32_t dxRaw =
            route.centers[j].X.RawValue() - route.centers[i].X.RawValue();
        const int32_t dzRaw =
            route.centers[j].Z.RawValue() - route.centers[i].Z.RawValue();
        const int32_t fallbackYaw = route.yawDeg.empty()
            ? 0
            : static_cast<int32_t>(route.yawDeg.back());
        route.yawDeg.push_back(static_cast<int16_t>(
            yawFromDeltaRaw(dxRaw, dzRaw, fallbackYaw)));
    }
}

inline bool BuildNpcRailFromPathLine(
    NpcRailRoute& dst,
    const PathNya::PathLine& srcLine,
    int32_t lineIndex,
    const SRL::Math::Types::Vector3D& trackSegOffset,
    bool reverseOrder)
{
    ReleaseNpcRailRoute(dst);
    if (srcLine.size() < 2u || lineIndex < 0) return false;

    dst.centers.reserve(srcLine.size());
    if (reverseOrder)
    {
        for (size_t i = srcLine.size(); i > 0u; --i)
        {
            const auto& src = srcLine[i - 1u];
            dst.centers.push_back(
                SRL::Math::Types::Vector3D(
                    SRL::Math::Types::Fxp::BuildRaw(src.xRaw),
                    SRL::Math::Types::Fxp::BuildRaw(src.yRaw),
                    SRL::Math::Types::Fxp::BuildRaw(src.zRaw)) +
                trackSegOffset);
        }
    }
    else
    {
        for (size_t i = 0; i < srcLine.size(); ++i)
        {
            const auto& src = srcLine[i];
            dst.centers.push_back(
                SRL::Math::Types::Vector3D(
                    SRL::Math::Types::Fxp::BuildRaw(src.xRaw),
                    SRL::Math::Types::Fxp::BuildRaw(src.yRaw),
                    SRL::Math::Types::Fxp::BuildRaw(src.zRaw)) +
                trackSegOffset);
        }
    }

    dst.selectedGuideLine = static_cast<int8_t>(lineIndex);
    if (!HasNpcRailNonZeroFirstXZ(dst))
    {
        ReleaseNpcRailRoute(dst);
        return false;
    }
    dst.SetBuilt(true);
    return true;
}

// Heap-safe arm path: consume a resident LWR guide line in place (offset +
// optional reverse) then swap into the NPC route. A second copy allocate
// failed under LWR pressure in autolap20 (pose1 OK, no crosses / no pose F1).
template <typename GuideCenters>
inline bool PromoteGuideLineToNpcRail(
    NpcRailRoute& dst,
    GuideCenters& srcCenters,
    int32_t lineIndex,
    const SRL::Math::Types::Vector3D& trackSegOffset,
    bool reverseOrder)
{
    ReleaseNpcRailRoute(dst);
    if (srcCenters.size() < 2u || lineIndex < 0) return false;

    for (size_t i = 0; i < srcCenters.size(); ++i)
    {
        srcCenters[i] = srcCenters[i] + trackSegOffset;
    }
    if (reverseOrder)
    {
        std::reverse(srcCenters.begin(), srcCenters.end());
    }

    dst.centers.swap(srcCenters);
    dst.selectedGuideLine = static_cast<int8_t>(lineIndex);
    if (!HasNpcRailNonZeroFirstXZ(dst))
    {
        // Give the (now empty) source back whatever we hold so Clear paths stay safe.
        srcCenters.swap(dst.centers);
        ReleaseNpcRailRoute(dst);
        return false;
    }
    dst.SetBuilt(true);
    return true;
}

inline SRL::Math::Types::Fxp ResolveNpcRailGroundYAt(
    const NpcRailRoute& route,
    TrackSystem& trackSystem,
    const SRL::Math::Types::Vector3D& trackSegOffset,
    size_t index,
    const SRL::Math::Types::Fxp& fallbackY)
{
    if (index >= route.centers.size()) return fallbackY;
    const auto& point = route.centers[index];
    static constexpr uint8_t kAsphaltSurfaceType = 1u;
    SRL::Math::Types::Fxp groundY{};
    int32_t groundSegmentId = -1;
    if (trackSystem.FindSurfaceYBySurfaceTypeSet(
            point, trackSegOffset, &kAsphaltSurfaceType, 1u,
            groundY, &groundSegmentId, -1, false))
    {
        return groundY;
    }
    return point.Y;
}
