#pragma once

#include <cstddef>
#include <cstdint>

struct FrameBudget
{
    uint32_t maxTrackSegments = 3;
    uint32_t maxTrackMeshes = 24;
    uint32_t maxTrackFaces = 3500;
};

struct FrameBudgetUsage
{
    uint32_t drawnTrackSegments = 0;
    uint32_t drawnTrackMeshes = 0;
    uint32_t drawnTrackFaces = 0;

    // Clear counters at frame start.
    void Reset()
    {
        drawnTrackSegments = 0;
        drawnTrackMeshes = 0;
        drawnTrackFaces = 0;
    }

    // Check segment count budget.
    bool CanDrawSegment(const FrameBudget& budget) const
    {
        return drawnTrackSegments < budget.maxTrackSegments;
    }

    // Check mesh budget for one candidate segment.
    bool CanDrawMeshCount(const FrameBudget& budget, uint32_t meshCount) const
    {
        return (drawnTrackMeshes + meshCount) <= budget.maxTrackMeshes;
    }

    // Check face budget for one candidate segment.
    bool CanDrawFaces(const FrameBudget& budget, uint32_t faceCount) const
    {
        return (drawnTrackFaces + faceCount) <= budget.maxTrackFaces;
    }

    // Consume budget with effective draw cost.
    void Consume(uint32_t meshCount, uint32_t faceCount)
    {
        ++drawnTrackSegments;
        drawnTrackMeshes += meshCount;
        drawnTrackFaces += faceCount;
    }
};
