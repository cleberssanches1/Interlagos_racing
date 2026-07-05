#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilitySourcePacket
{
    bool hasSimulationDecision = false;
    bool hasTrackDecision = false;
    bool hasTelemetry = false;
    FrameReuseDomain::SimulationReuseDecisionPacket simulationDecision{};
    FrameReuseDomain::TrackReuseDecisionPacket trackDecision{};
    FrameReuseDomain::FrameReuseTelemetry telemetry{};
};

} // namespace GameLoopObservabilityDomain
