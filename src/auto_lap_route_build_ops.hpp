#pragma once

#include <cstddef>

#include "auto_lap_route_runtime_state.hpp"

namespace AutoLapRouteDomain
{

struct HeadingVector
{
    int32_t deltaXRaw = 0;
    int32_t deltaZRaw = 0;
};

inline bool HasMinimumRoutePoints(const AutoLapRouteState& state, size_t minPoints = 2u)
{
    return state.centers.size() >= minPoints;
}

inline bool NeedsYawRebuild(const AutoLapRouteState& state)
{
    return state.yawDeg.size() != state.centers.size() ||
           state.offDeg.size() != state.centers.size();
}

inline bool HasConsistentMappedRoute(const AutoLapRouteState& state)
{
    return state.centers.size() == state.ids.size();
}

inline bool HasConsistentYawRoute(const AutoLapRouteState& state)
{
    return state.centers.size() == state.yawDeg.size() &&
           state.centers.size() == state.offDeg.size();
}

inline bool HasAnyRoutePoints(const AutoLapRouteState& state)
{
    return !state.centers.empty();
}

inline bool HasValidGuideBuildOutput(const AutoLapRouteState& state)
{
    return HasAnyRoutePoints(state) &&
           HasConsistentMappedRoute(state) &&
           HasConsistentYawRoute(state);
}

inline bool IsRouteInitialized(const AutoLapRouteState& state)
{
    return state.Initialized();
}

inline bool CanApplyYawAtCurrentIndex(const AutoLapRouteState& state)
{
    return HasConsistentYawRoute(state) &&
           static_cast<size_t>(state.index) < state.yawDeg.size();
}

inline bool HasYawAtIndex(const AutoLapRouteState& state, size_t index)
{
    return HasConsistentYawRoute(state) &&
           index < state.yawDeg.size();
}

inline bool HasObservedSegmentAtCurrentIndex(const AutoLapRouteState& state)
{
    return !state.ids.empty() &&
           static_cast<size_t>(state.index) < state.ids.size();
}

inline bool CanUpdateCurrentYawOffset(const AutoLapRouteState& state)
{
    return HasConsistentYawRoute(state) &&
           !state.yawDeg.empty();
}

inline bool HasCenterAtIndex(const AutoLapRouteState& state, size_t index)
{
    return index < state.centers.size();
}

inline bool HasMappedSegmentAtIndex(const AutoLapRouteState& state, size_t index)
{
    return HasCenterAtIndex(state, index) &&
           !state.ids.empty() &&
           index < state.ids.size();
}

inline bool HasGuideLineMinimumPoints(const AutoLapRouteState& state,
                                      size_t lineIndex,
                                      size_t minPoints = 2u)
{
    return lineIndex < state.guideLines.size() &&
           state.guideLines[lineIndex].size() >= minPoints;
}

inline bool HasValidGuideLineIndex(const AutoLapRouteState& state, int32_t lineIndex)
{
    return lineIndex >= 0 &&
           static_cast<size_t>(lineIndex) < state.guideLines.size();
}

inline bool HasPositiveSegmentCount(int32_t segmentCount)
{
    return segmentCount > 0;
}

inline bool HasMinimumRouteIds(const AutoLapRouteState& state, size_t minIds = 2u)
{
    return state.ids.size() >= minIds;
}

inline bool HasMinimumPointCount(size_t pointCount, size_t minPoints = 2u)
{
    return pointCount >= minPoints;
}

inline bool ShouldReverseRouteDirection(int32_t directionScore)
{
    return directionScore < 0;
}

inline bool FindNearestRoutePointIndex(const AutoLapRouteState& state,
                                       const SRL::Math::Types::Vector3D& worldPosition,
                                       size_t& outIndex)
{
    if (!HasAnyRoutePoints(state))
    {
        return false;
    }

    size_t nearestIndex = 0u;
    bool foundNearest = false;
    SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    for (size_t i = 0u; i < state.centers.size(); ++i)
    {
        const auto& routePoint = state.centers[i];
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

    if (!foundNearest)
    {
        return false;
    }

    outIndex = nearestIndex;
    return true;
}

inline int32_t SelectBestGuideLineIndex(const AutoLapRouteState& state,
                                        const SRL::Math::Types::Vector3D& trackSegOffset,
                                        const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
{
    int32_t selectedLineIndex = -1;
    SRL::Math::Types::Fxp selectedScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    for (int32_t lineIndex = 0; lineIndex < static_cast<int32_t>(state.guideLines.size()); ++lineIndex)
    {
        const auto& line = state.guideLines[lineIndex];
        if (!HasGuideLineMinimumPoints(state, static_cast<size_t>(lineIndex)))
        {
            continue;
        }

        SRL::Math::Types::Fxp lineScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (size_t i = 0; i < line.size(); ++i)
        {
            const auto worldPoint = line[i] + trackSegOffset;
            const auto dx = (worldPoint.X - referenceCarWorldPosition.X).Abs();
            const auto dz = (worldPoint.Z - referenceCarWorldPosition.Z).Abs();
            const auto score = dx + dz;
            if (score < lineScore) lineScore = score;
        }

        if (selectedLineIndex < 0 || lineScore < selectedScore)
        {
            selectedLineIndex = lineIndex;
            selectedScore = lineScore;
        }
    }

    return selectedLineIndex;
}

inline int32_t WrapSegmentId(int32_t segmentCount, int32_t segmentId)
{
    if (segmentCount <= 0) return -1;
    int32_t normalized = (segmentId - 1) % segmentCount;
    if (normalized < 0) normalized += segmentCount;
    return normalized + 1;
}

inline int32_t ScoreRouteDirection(const AutoLapRouteState& state, int32_t segmentCount)
{
    int32_t directionScore = 0;
    if (!HasMinimumRouteIds(state))
    {
        return directionScore;
    }

    for (size_t i = 0; i < state.ids.size(); ++i)
    {
        const int32_t fromId = state.ids[i];
        const int32_t toId = state.ids[(i + 1u) % state.ids.size()];
        if (fromId <= 0 || toId <= 0) continue;
        int32_t forwardDelta = (toId - fromId) % segmentCount;
        if (forwardDelta < 0) forwardDelta += segmentCount;
        if (forwardDelta == 0) continue;
        if (forwardDelta <= (segmentCount / 2))
        {
            ++directionScore;
        }
        else
        {
            --directionScore;
        }
    }
    return directionScore;
}

inline bool HasReachedOrPassedWaypoint(const AutoLapRouteState& state,
                                       size_t fromIndex,
                                       size_t toIndex,
                                       const SRL::Math::Types::Vector3D& worldPosition)
{
    const auto& from = state.centers[fromIndex];
    const auto& to = state.centers[toIndex];
    const int32_t remX = to.X.RawValue() - worldPosition.X.RawValue();
    const int32_t remZ = to.Z.RawValue() - worldPosition.Z.RawValue();
    const int32_t absRemX = (remX < 0) ? -remX : remX;
    const int32_t absRemZ = (remZ < 0) ? -remZ : remZ;
    if (absRemX <= (8 << 16) && absRemZ <= (8 << 16)) return true;

    const int64_t segX = static_cast<int64_t>(to.X.RawValue()) -
                         static_cast<int64_t>(from.X.RawValue());
    const int64_t segZ = static_cast<int64_t>(to.Z.RawValue()) -
                         static_cast<int64_t>(from.Z.RawValue());
    const int64_t toCarX = static_cast<int64_t>(worldPosition.X.RawValue()) -
                           static_cast<int64_t>(to.X.RawValue());
    const int64_t toCarZ = static_cast<int64_t>(worldPosition.Z.RawValue()) -
                           static_cast<int64_t>(to.Z.RawValue());
    return ((segX * toCarX) + (segZ * toCarZ)) >= 0;
}

inline HeadingVector BuildHeadingVector(const AutoLapRouteState& state,
                                        size_t currentIndex,
                                        size_t nextIndex,
                                        size_t routePointCount)
{
    HeadingVector vector{};
    const size_t headingA = currentIndex;
    constexpr size_t kYawLookAheadPoints = 2u;
    const size_t headingB = (headingA + kYawLookAheadPoints) % routePointCount;

    vector.deltaXRaw = state.centers[headingB].X.RawValue() -
                       state.centers[headingA].X.RawValue();
    vector.deltaZRaw = state.centers[headingB].Z.RawValue() -
                       state.centers[headingA].Z.RawValue();
    if (vector.deltaXRaw == 0 && vector.deltaZRaw == 0)
    {
        vector.deltaXRaw = state.centers[nextIndex].X.RawValue() -
                           state.centers[headingA].X.RawValue();
        vector.deltaZRaw = state.centers[nextIndex].Z.RawValue() -
                           state.centers[headingA].Z.RawValue();
    }

    return vector;
}

} // namespace AutoLapRouteDomain
