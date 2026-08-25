#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cd_asset_transition_ops.hpp"
#include "path_nya_loader.hpp"
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

inline SRL::Math::Types::Fxp ScorePointToSegment(TrackSystem& trackSystem,
                                                 const SRL::Math::Types::Vector3D& trackSegOffset,
                                                 const SRL::Math::Types::Vector3D& point,
                                                 int32_t segmentId)
{
    SRL::Math::Types::Vector3D center{};
    if (!trackSystem.FindSegmentCenterById(segmentId, trackSegOffset, center))
    {
        return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    }
    return (center.X - point.X).Abs() + (center.Z - point.Z).Abs();
}

inline int32_t FindBestSegmentForPoint(TrackSystem& trackSystem,
                                       const SRL::Math::Types::Vector3D& trackSegOffset,
                                       const SRL::Math::Types::Vector3D& routePoint,
                                       int32_t mappedSegmentId,
                                       int32_t segmentCount)
{
    int32_t bestSegmentId = -1;
    SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    if (mappedSegmentId <= 0)
    {
        for (int32_t segmentId = 1; segmentId <= segmentCount; ++segmentId)
        {
            const auto score = ScorePointToSegment(trackSystem, trackSegOffset, routePoint, segmentId);
            if (bestSegmentId > 0 && !(score < bestScore)) continue;
            bestSegmentId = segmentId;
            bestScore = score;
        }
        return bestSegmentId;
    }

    static constexpr int32_t kBackSearch = 0;
    static constexpr int32_t kForwardSearch = 12;
    for (int32_t delta = -kBackSearch; delta <= kForwardSearch; ++delta)
    {
        const int32_t segmentId = WrapSegmentId(segmentCount, mappedSegmentId + delta);
        const auto score = ScorePointToSegment(trackSystem, trackSegOffset, routePoint, segmentId);
        if (bestSegmentId > 0 && !(score < bestScore)) continue;
        bestSegmentId = segmentId;
        bestScore = score;
    }
    return bestSegmentId;
}

inline bool PopulateRouteFromGuideLine(AutoLapRouteState& state,
                                       TrackSystem& trackSystem,
                                       const SRL::Math::Types::Vector3D& trackSegOffset,
                                       const TrackLowWorkVector<SRL::Math::Types::Vector3D>& routeLine)
{
    const int32_t segmentCount = static_cast<int32_t>(trackSystem.SegmentCount());
    if (!HasPositiveSegmentCount(segmentCount)) return false;

    state.centers.reserve(routeLine.size());
    state.ids.reserve(routeLine.size());

    int32_t mappedSegmentId = -1;
    for (size_t i = 0; i < routeLine.size(); ++i)
    {
        const SRL::Math::Types::Vector3D routePoint = routeLine[i] + trackSegOffset;
        state.centers.push_back(routePoint);

        const int32_t bestSegmentId =
            FindBestSegmentForPoint(trackSystem, trackSegOffset, routePoint, mappedSegmentId, segmentCount);
        if (bestSegmentId <= 0) return false;
        mappedSegmentId = bestSegmentId;
        state.ids.push_back(static_cast<int16_t>(mappedSegmentId));
    }
    return true;
}

inline bool NormalizeRouteDirection(AutoLapRouteState& state, int32_t segmentCount)
{
    const int32_t directionScore = ScoreRouteDirection(state, segmentCount);
    if (!ShouldReverseRouteDirection(directionScore))
    {
        return false;
    }

    std::reverse(state.centers.begin(), state.centers.end());
    std::reverse(state.ids.begin(), state.ids.end());
    return true;
}

inline void AdvanceObservedSegmentToward(int32_t segmentCount,
                                        int32_t desiredSegmentId,
                                        int16_t& ioLatestActiveSegmentId)
{
    if (desiredSegmentId <= 0 || segmentCount <= 0)
    {
        ioLatestActiveSegmentId = static_cast<int16_t>(desiredSegmentId);
        return;
    }

    desiredSegmentId = WrapSegmentId(segmentCount, desiredSegmentId);
    if (ioLatestActiveSegmentId <= 0)
    {
        ioLatestActiveSegmentId = static_cast<int16_t>(desiredSegmentId);
        return;
    }

    const int32_t currentSegmentId = WrapSegmentId(segmentCount, ioLatestActiveSegmentId);
    if (currentSegmentId <= 0)
    {
        ioLatestActiveSegmentId = static_cast<int16_t>(desiredSegmentId);
        return;
    }

    int32_t forwardDistance = (desiredSegmentId - currentSegmentId) % segmentCount;
    if (forwardDistance < 0) forwardDistance += segmentCount;
    if (forwardDistance == 0)
    {
        ioLatestActiveSegmentId = static_cast<int16_t>(currentSegmentId);
        return;
    }

    if (forwardDistance < (segmentCount / 2))
    {
        ioLatestActiveSegmentId = static_cast<int16_t>(WrapSegmentId(segmentCount, currentSegmentId + 1));
        return;
    }

    ioLatestActiveSegmentId = static_cast<int16_t>(currentSegmentId);
}

inline SRL::Math::Types::Fxp ResolveRouteGroundYAt(const AutoLapRouteState& state,
                                                   TrackSystem& trackSystem,
                                                   const SRL::Math::Types::Vector3D& trackSegOffset,
                                                   size_t routeIndex,
                                                   const SRL::Math::Types::Fxp& fallbackY)
{
    static constexpr uint8_t kAsphaltSurfaceType = 1u;

    if (HasCenterAtIndex(state, routeIndex))
    {
        SRL::Math::Types::Fxp asphaltY{};
        if (trackSystem.FindSurfaceYBySurfaceTypeSet(
                state.centers[routeIndex],
                trackSegOffset,
                &kAsphaltSurfaceType,
                1u,
                asphaltY))
        {
            return asphaltY;
        }
    }

    if (HasMappedSegmentAtIndex(state, routeIndex))
    {
        const int32_t routeSegmentId = static_cast<int32_t>(state.ids[routeIndex]);
        SRL::Math::Types::Vector3D segmentCenter{};
        if (routeSegmentId > 0 &&
            trackSystem.FindSegmentCenterById(routeSegmentId, trackSegOffset, segmentCenter))
        {
            return segmentCenter.Y;
        }
    }

    return HasCenterAtIndex(state, routeIndex)
        ? state.centers[routeIndex].Y
        : fallbackY;
}

inline double PointSegmentDistanceSqXZ(const SRL::Math::Types::Vector3D& point,
                                       const SRL::Math::Types::Vector3D& a,
                                       const SRL::Math::Types::Vector3D& b)
{
    const double px = static_cast<double>(point.X.RawValue());
    const double pz = static_cast<double>(point.Z.RawValue());
    const double ax = static_cast<double>(a.X.RawValue());
    const double az = static_cast<double>(a.Z.RawValue());
    const double bx = static_cast<double>(b.X.RawValue());
    const double bz = static_cast<double>(b.Z.RawValue());

    const double vx = bx - ax;
    const double vz = bz - az;
    const double wx = px - ax;
    const double wz = pz - az;
    const double vv = (vx * vx) + (vz * vz);
    if (vv <= 0.0)
    {
        return (wx * wx) + (wz * wz);
    }

    double t = ((wx * vx) + (wz * vz)) / vv;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    const double dx = px - (ax + (vx * t));
    const double dz = pz - (az + (vz * t));
    return (dx * dx) + (dz * dz);
}

inline TrackLowWorkVector<SRL::Math::Types::Vector3D> SimplifyGuideLine(
    const TrackLowWorkVector<SRL::Math::Types::Vector3D>& input,
    size_t segmentCount)
{
    TrackLowWorkVector<SRL::Math::Types::Vector3D> output{};
    if (input.size() <= 2u)
    {
        output = input;
        return output;
    }

    const size_t targetMaxPoints = std::min<size_t>(
        std::max<size_t>(segmentCount * 6u, 1536u),
        2048u);
    if (input.size() <= targetMaxPoints)
    {
        output = input;
        return output;
    }

    constexpr double kEpsilonRaw = static_cast<double>(2 << 16);
    const double epsilonSq = kEpsilonRaw * kEpsilonRaw;

    std::vector<uint8_t> keep(input.size(), 0u);
    keep.front() = 1u;
    keep.back() = 1u;

    std::vector<std::pair<size_t, size_t>> stack{};
    stack.emplace_back(0u, input.size() - 1u);
    while (!stack.empty())
    {
        const auto range = stack.back();
        stack.pop_back();
        if (range.second <= range.first + 1u) continue;

        size_t farthestIndex = 0u;
        double farthestDistSq = -1.0;
        for (size_t i = range.first + 1u; i < range.second; ++i)
        {
            const double distSq = PointSegmentDistanceSqXZ(
                input[i], input[range.first], input[range.second]);
            if (distSq <= farthestDistSq) continue;
            farthestDistSq = distSq;
            farthestIndex = i;
        }

        if (farthestDistSq > epsilonSq)
        {
            keep[farthestIndex] = 1u;
            stack.emplace_back(range.first, farthestIndex);
            stack.emplace_back(farthestIndex, range.second);
        }
    }

    output.reserve(std::min(targetMaxPoints, input.size()));
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (keep[i] == 0u) continue;
        output.push_back(input[i]);
    }

    if (output.size() <= targetMaxPoints)
    {
        return output;
    }

    TrackLowWorkVector<SRL::Math::Types::Vector3D> capped{};
    capped.reserve(targetMaxPoints);
    const size_t lastIndex = output.size() - 1u;
    for (size_t i = 0; i < targetMaxPoints; ++i)
    {
        const size_t srcIndex =
            (i * lastIndex) / std::max<size_t>(1u, targetMaxPoints - 1u);
        if (!capped.empty() &&
            capped.back().X.RawValue() == output[srcIndex].X.RawValue() &&
            capped.back().Y.RawValue() == output[srcIndex].Y.RawValue() &&
            capped.back().Z.RawValue() == output[srcIndex].Z.RawValue())
        {
            continue;
        }
        capped.push_back(output[srcIndex]);
    }

    if (capped.size() >= 2u)
    {
        return capped;
    }
    return output;
}

inline const TrackLowWorkVector<SRL::Math::Types::Vector3D>* ResolveSelectedRouteLine(
    AutoLapRouteState& state,
    int32_t selectedLineIndex,
    int32_t segmentCount)
{
    if (!HasValidGuideLineIndex(state, selectedLineIndex))
    {
        return nullptr;
    }

    state.selectedGuideLine = static_cast<int8_t>(selectedLineIndex);
    const auto& selectedLine = state.guideLines[static_cast<size_t>(selectedLineIndex)];
    if (!HasPositiveSegmentCount(segmentCount))
    {
        return nullptr;
    }

    const auto simplifiedMiddleLine =
        SimplifyGuideLine(selectedLine, static_cast<size_t>(segmentCount));
    if (HasMinimumPointCount(simplifiedMiddleLine.size()))
    {
        state.guideLines[static_cast<size_t>(selectedLineIndex)] = simplifiedMiddleLine;
    }

    return &state.guideLines[static_cast<size_t>(selectedLineIndex)];
}

inline void PopulateFallbackCenters(AutoLapRouteState& state,
                                    TrackSystem& trackSystem,
                                    const SRL::Math::Types::Vector3D& trackSegOffset,
                                    int32_t segmentCount)
{
    SRL::Math::Types::Vector3D center{};
    for (int32_t id = 1; id <= segmentCount; ++id)
    {
        if (!trackSystem.FindSegmentCenterById(id, trackSegOffset, center)) continue;
        state.ids.push_back(id);
        state.centers.push_back(center);
    }
}

inline void CopyParsedGuideLines(AutoLapRouteState& state, const PathNya::ParseResult& parsed)
{
    for (size_t lineIndex = 0; lineIndex < state.guideLines.size(); ++lineIndex)
    {
        const auto& srcLine = parsed.lines[lineIndex];
        auto& dstLine = state.guideLines[lineIndex];
        dstLine.reserve(srcLine.size());
        for (size_t pointIndex = 0; pointIndex < srcLine.size(); ++pointIndex)
        {
            const auto& srcPoint = srcLine[pointIndex];
            dstLine.push_back(SRL::Math::Types::Vector3D(
                SRL::Math::Types::Fxp::BuildRaw(srcPoint.xRaw),
                SRL::Math::Types::Fxp::BuildRaw(srcPoint.yRaw),
                SRL::Math::Types::Fxp::BuildRaw(srcPoint.zRaw)));
        }
    }
}

inline const char* const* GuidePathCandidates(size_t& outCount)
{
    static const char* const kCandidates[] = {
        "/CD/DATA/PATH.NYA",
        "/CD/DATA/PATH.NYA;1",
        "/DATA/PATH.NYA",
        "/DATA/PATH.NYA;1",
        "CD/DATA/PATH.NYA",
        "CD/DATA/PATH.NYA;1",
        "DATA/PATH.NYA",
        "DATA/PATH.NYA;1",
        "cd/data/PATH.NYA",
        "cd/data/PATH.NYA;1",
        "data/PATH.NYA",
        "data/PATH.NYA;1",
        "/PATH.NYA",
        "/PATH.NYA;1",
        "PATH.NYA",
        "PATH.NYA;1",
    };
    outCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
    return kCandidates;
}

inline bool TryLoadGuideBytes(std::vector<uint8_t>& outBytes, const char*& outLoadedCandidate)
{
    size_t candidateCount = 0u;
    const char* const* candidates = GuidePathCandidates(candidateCount);
    outLoadedCandidate = nullptr;
    for (size_t i = 0; i < candidateCount; ++i)
    {
        if (!CdAssetDomain::ReadBinaryAsset(candidates[i], outBytes)) continue;
        outLoadedCandidate = candidates[i];
        return true;
    }
    return false;
}

inline bool TryParseGuideBytes(const std::vector<uint8_t>& bytes,
                               PathNya::ParseResult& outParsed)
{
    return PathNya::Parse(bytes.data(), bytes.size(), outParsed);
}

} // namespace AutoLapRouteDomain
