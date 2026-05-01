#pragma once

#include <cstdint>
#include <limits>

#include "drive_surface_map_loader.hpp"

namespace DriveSurfaceMap
{
namespace Query
{
struct SampleResult
{
    bool found = false;
    bool inside = false;
    int64_t yRaw = 0;
    int32_t segmentId = -1;
    int32_t normalXRaw = 0;
    int32_t normalYRaw = -(1 << 16);
    int32_t normalZRaw = 0;
};

inline int64_t Abs64(int64_t v)
{
    return (v < 0) ? -v : v;
}

inline bool SolvePlaneYRaw(const TriangleEntryV1& tri,
                           int64_t pxRaw,
                           int64_t pzRaw,
                           int64_t& outYRaw)
{
    if (tri.ny == 0) return false;
    const int64_t rhs =
        (tri.nx * (pxRaw - static_cast<int64_t>(tri.ax))) +
        (tri.nz * (pzRaw - static_cast<int64_t>(tri.az)));
    outYRaw = static_cast<int64_t>(tri.ay) - (rhs / tri.ny);
    return true;
}

inline void AccumulateCandidate(const TriangleEntryV1& tri,
                                int64_t pxRaw,
                                int64_t pyRaw,
                                int64_t pzRaw,
                                bool& foundInside,
                                int64_t& bestInsideDeltaY,
                                SampleResult& bestInside,
                                bool& foundFallback,
                                int64_t& bestFallbackPlanar,
                                int64_t& bestFallbackDeltaY,
                                SampleResult& bestFallback)
{
    int64_t yRaw = 0;
    if (!SolvePlaneYRaw(tri, pxRaw, pzRaw, yRaw)) return;

    const bool inside =
        (pxRaw >= static_cast<int64_t>(tri.minX) && pxRaw <= static_cast<int64_t>(tri.maxX) &&
         pzRaw >= static_cast<int64_t>(tri.minZ) && pzRaw <= static_cast<int64_t>(tri.maxZ));
    const int64_t deltaY = Abs64(yRaw - pyRaw);

    if (inside)
    {
        if (!foundInside || deltaY < bestInsideDeltaY)
        {
            foundInside = true;
            bestInsideDeltaY = deltaY;
            bestInside.found = true;
            bestInside.inside = true;
            bestInside.yRaw = yRaw;
            bestInside.segmentId = static_cast<int32_t>(tri.segmentId);
            bestInside.normalXRaw = tri.normalX;
            bestInside.normalYRaw = tri.normalY;
            bestInside.normalZRaw = tri.normalZ;
        }
        return;
    }

    const int64_t centerX = (static_cast<int64_t>(tri.minX) + static_cast<int64_t>(tri.maxX)) / 2;
    const int64_t centerZ = (static_cast<int64_t>(tri.minZ) + static_cast<int64_t>(tri.maxZ)) / 2;
    const int64_t planarScore = Abs64(centerX - pxRaw) + Abs64(centerZ - pzRaw);
    if (!foundFallback ||
        planarScore < bestFallbackPlanar ||
        (planarScore == bestFallbackPlanar && deltaY < bestFallbackDeltaY))
    {
        foundFallback = true;
        bestFallbackPlanar = planarScore;
        bestFallbackDeltaY = deltaY;
        bestFallback.found = true;
        bestFallback.inside = false;
        bestFallback.yRaw = yRaw;
        bestFallback.segmentId = static_cast<int32_t>(tri.segmentId);
        bestFallback.normalXRaw = tri.normalX;
        bestFallback.normalYRaw = tri.normalY;
        bestFallback.normalZRaw = tri.normalZ;
    }
}

inline bool SampleSegments(const Loader::View& view,
                           const int32_t* segmentIds,
                           size_t segmentCount,
                           int64_t pxRaw,
                           int64_t pyRaw,
                           int64_t pzRaw,
                           SampleResult& outResult)
{
    outResult = {};
    if (!view.valid || !segmentIds || segmentCount == 0) return false;

    bool foundInside = false;
    bool foundFallback = false;
    int64_t bestInsideDeltaY = std::numeric_limits<int64_t>::max();
    int64_t bestFallbackPlanar = std::numeric_limits<int64_t>::max();
    int64_t bestFallbackDeltaY = std::numeric_limits<int64_t>::max();
    SampleResult bestInside{};
    SampleResult bestFallback{};

    for (size_t si = 0; si < segmentCount; ++si)
    {
        const int32_t segmentId = segmentIds[si];
        if (segmentId <= 0) continue;

        SegmentEntryV1 seg{};
        if (!Loader::ReadSegmentEntryById(view, segmentId, seg)) continue;
        if (seg.triangleCount == 0) continue;

        for (uint32_t ti = 0; ti < seg.triangleCount; ++ti)
        {
            const uint32_t triIndex = seg.firstTriangleIndex + ti;
            TriangleEntryV1 tri{};
            if (!Loader::ReadTriangleByIndex(view, triIndex, tri)) continue;
            if (tri.ny == 0) continue;
            AccumulateCandidate(
                tri,
                pxRaw,
                pyRaw,
                pzRaw,
                foundInside,
                bestInsideDeltaY,
                bestInside,
                foundFallback,
                bestFallbackPlanar,
                bestFallbackDeltaY,
                bestFallback);
        }
    }

    if (foundInside)
    {
        outResult = bestInside;
        return true;
    }
    if (foundFallback)
    {
        outResult = bestFallback;
        return true;
    }
    return false;
}
} // namespace Query
} // namespace DriveSurfaceMap

