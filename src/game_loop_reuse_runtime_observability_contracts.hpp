#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilityAssemblyInputs
{
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision = nullptr;
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision = nullptr;
    const FrameReuseDomain::FrameReuseTelemetry* telemetry = nullptr;
};

} // namespace GameLoopObservabilityDomain
