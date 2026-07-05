#pragma once

#include "frame_reuse_contracts.hpp"

namespace FrameReuseDomain
{

struct FrameReuseRuntimeOwnerPacket
{
    bool hasSimulationHistory = false;
    bool hasTrackHistory = false;
    bool hasSimulationDecision = false;
    bool hasTrackDecision = false;
    bool hasTelemetry = false;
    SimulationFrameHistoryState simulationHistory{};
    TrackFrameHistoryState trackHistory{};
    SimulationReuseDecisionPacket simulationDecision{};
    TrackReuseDecisionPacket trackDecision{};
    FrameReuseTelemetry telemetry{};
};

} // namespace FrameReuseDomain
