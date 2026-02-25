#pragma once

#include <cstdint>
#include <srl.hpp>

#include "frame_budget.hpp"
#include "track_draw_producer.hpp"

struct FrameTelemetry
{
    uint32_t frameId = 0;
    uint32_t submittedTrackSegments = 0;
    uint32_t submittedTrackMeshes = 0;
    uint32_t submittedTrackFaces = 0;
    uint32_t trackSegmentsSkippedByBudget = 0;
    uint32_t trackSegmentsPrepared = 0;
    uint32_t drawListCount = 0;
    uint32_t drawListResolveMisses = 0;
    uint32_t executeResolveMisses = 0;
    uint32_t invalidChunks = 0;
    TrackDrawProducerStats producer{};

    // Reset telemetry counters for the next frame.
    void BeginFrame(uint32_t id)
    {
        frameId = id;
        submittedTrackSegments = 0;
        submittedTrackMeshes = 0;
        submittedTrackFaces = 0;
        trackSegmentsSkippedByBudget = 0;
        trackSegmentsPrepared = 0;
        drawListCount = 0;
        drawListResolveMisses = 0;
        executeResolveMisses = 0;
        invalidChunks = 0;
        producer = {};
    }

    // Print frame level telemetry to the debug overlay.
    void Present(const FrameBudget& budget, const FrameBudgetUsage& usage) const
    {
        constexpr bool kShowParallelTelemetry = false;
        constexpr bool kShowTrackFrameTelemetry = false;
        if constexpr (kShowTrackFrameTelemetry)
        {
            SRL::Debug::Print(2, 20, "FR:%lu DRL:%lu PREP:%lu SKIP:%lu",
                              (unsigned long)frameId,
                              (unsigned long)drawListCount,
                              (unsigned long)trackSegmentsPrepared,
                              (unsigned long)trackSegmentsSkippedByBudget);
            SRL::Debug::Print(2, 21, "MIS prep:%lu exe:%lu inv:%lu",
                              (unsigned long)drawListResolveMisses,
                              (unsigned long)executeResolveMisses,
                              (unsigned long)invalidChunks);
            SRL::Debug::Print(2, 22, "TRK seg:%lu/%lu mesh:%lu/%lu",
                              (unsigned long)usage.drawnTrackSegments,
                              (unsigned long)budget.maxTrackSegments,
                              (unsigned long)usage.drawnTrackMeshes,
                              (unsigned long)budget.maxTrackMeshes);
            SRL::Debug::Print(2, 23, "TRK face:%lu/%lu",
                              (unsigned long)usage.drawnTrackFaces,
                              (unsigned long)budget.maxTrackFaces);
            if (usage.drawnTrackFaces + 1000 >= budget.maxTrackFaces)
            {
                SRL::Debug::Print(2, 29, "TRK face near cap, reserving for car");
            }
        }
        if constexpr (kShowParallelTelemetry)
        {
            SRL::Debug::Print(2, 24, "PRD sub:%lu done:%lu reu:%lu fly:%d",
                              (unsigned long)producer.jobsSubmitted,
                              (unsigned long)producer.jobsCompleted,
                              (unsigned long)producer.reusedPreviousList,
                              producer.jobInFlight ? 1 : 0);
            SRL::Debug::Print(2, 25, "PRD lat:%lu max:%lu tmo:%lu",
                              (unsigned long)producer.lastLatencyFrames,
                              (unsigned long)producer.maxLatencyFrames,
                              (unsigned long)producer.timeoutFallbacks);
            SRL::Debug::Print(2, 26, "PRD sync:%lu cto:%lu dis:%d",
                              (unsigned long)producer.synchronousBuilds,
                              (unsigned long)producer.consecutiveTimeouts,
                              producer.slaveDisabledByTimeout ? 1 : 0);
            SRL::Debug::Print(2, 27, "PRD ren:%lu safe:%d trig:%lu",
                              (unsigned long)producer.slaveReenabledCount,
                              producer.safeModeActive ? 1 : 0,
                              (unsigned long)producer.safeModeTriggers);
            SRL::Debug::Print(2, 28, "PRD safe frames:%lu",
                              (unsigned long)producer.safeModeFrames);
        }
    }
};
