#pragma once

#include <algorithm>
#include <cstdint>

#include "frame_budget.hpp"
#include "frame_telemetry.hpp"

class AdaptiveTrackBudgetController
{
public:
    struct Limits
    {
        uint32_t minSegments = 1;
        uint32_t maxSegments = 20;
        uint32_t minMeshes = 16;
        uint32_t maxMeshes = 128;
        uint32_t minFaces = 4000;
        uint32_t maxFaces = 30000;
    };

    // Build controller with internal default limits.
    AdaptiveTrackBudgetController()
        : limits_(DefaultLimits())
    {}

    // Build controller with custom limits.
    explicit AdaptiveTrackBudgetController(const Limits& limits)
        : limits_(limits)
    {}

    // Update budget for the next frame based on producer and draw telemetry.
    FrameBudget Update(const FrameBudget& currentBudget, const FrameTelemetry& telemetry)
    {
        FrameBudget next = currentBudget;

        const bool timeoutRaised = telemetry.producer.timeoutFallbacks > lastTimeoutFallbacks_;
        const bool underPressure = timeoutRaised ||
                                   telemetry.producer.jobInFlight ||
                                   telemetry.producer.consecutiveTimeouts > 0 ||
                                   telemetry.producer.lastLatencyFrames > 2;

        const bool canGrow = telemetry.trackSegmentsSkippedByBudget == 0 &&
                             telemetry.producer.reusedPreviousList == 0 &&
                             telemetry.producer.lastLatencyFrames <= 1 &&
                             !telemetry.producer.jobInFlight;

        if (underPressure)
        {
            ++pressureStreak_;
            stableStreak_ = 0;
        }
        else if (canGrow)
        {
            ++stableStreak_;
            pressureStreak_ = 0;
        }
        else
        {
            pressureStreak_ = 0;
            stableStreak_ = 0;
        }

        // Apply change only after repeated signal to avoid budget oscillation.
        if (pressureStreak_ >= pressureHysteresisFrames_)
        {
            next.maxTrackSegments = Clamp(next.maxTrackSegments > 0 ? next.maxTrackSegments - 1 : 0,
                                          limits_.minSegments, limits_.maxSegments);
            next.maxTrackMeshes = Clamp(next.maxTrackMeshes > 8 ? next.maxTrackMeshes - 8 : 0,
                                        limits_.minMeshes, limits_.maxMeshes);
            next.maxTrackFaces = Clamp(next.maxTrackFaces > 1500 ? next.maxTrackFaces - 1500 : 0,
                                       limits_.minFaces, limits_.maxFaces);
            pressureStreak_ = 0;
        }
        else if (stableStreak_ >= growthHysteresisFrames_)
        {
            next.maxTrackSegments = Clamp(next.maxTrackSegments + 1, limits_.minSegments, limits_.maxSegments);
            next.maxTrackMeshes = Clamp(next.maxTrackMeshes + 4, limits_.minMeshes, limits_.maxMeshes);
            next.maxTrackFaces = Clamp(next.maxTrackFaces + 1000, limits_.minFaces, limits_.maxFaces);
            stableStreak_ = 0;
        }

        lastTimeoutFallbacks_ = telemetry.producer.timeoutFallbacks;
        return next;
    }

private:
    // Provide default limits without relying on complex default arguments.
    static Limits DefaultLimits()
    {
        Limits value{};
        value.minSegments = 1;
        value.maxSegments = 20;
        value.minMeshes = 16;
        value.maxMeshes = 128;
        value.minFaces = 4000;
        value.maxFaces = 30000;
        return value;
    }

    static uint32_t Clamp(uint32_t value, uint32_t minValue, uint32_t maxValue)
    {
        return std::max(minValue, std::min(value, maxValue));
    }

    Limits limits_{};
    uint32_t lastTimeoutFallbacks_ = 0;
    uint32_t pressureStreak_ = 0;
    uint32_t stableStreak_ = 0;
    uint32_t pressureHysteresisFrames_ = 2;
    uint32_t growthHysteresisFrames_ = 6;
};
