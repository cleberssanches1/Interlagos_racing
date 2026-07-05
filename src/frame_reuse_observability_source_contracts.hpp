#pragma once

#include "frame_reuse_contracts.hpp"

namespace FrameReuseDomain
{

struct ReuseObservabilitySourceSnapshot
{
    bool hasSimulationDecision = false;
    bool hasTrackDecision = false;
    bool hasTelemetry = false;
    SimulationReuseDecisionPacket simulationDecision{};
    TrackReuseDecisionPacket trackDecision{};
    FrameReuseTelemetry telemetry{};
};

} // namespace FrameReuseDomain
