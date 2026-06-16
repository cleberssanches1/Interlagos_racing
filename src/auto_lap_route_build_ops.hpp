#pragma once

#include <cstddef>

#include "auto_lap_route_runtime_state.hpp"

namespace AutoLapRouteDomain
{

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

} // namespace AutoLapRouteDomain
