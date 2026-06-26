#pragma once

#include "frame_reuse_contracts.hpp"
#include "game_loop_simulation_reuse_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationReuseTelemetryViewPacket(
    const FrameReuseDomain::FrameReuseTelemetry& telemetry,
    SimulationReuseTelemetryViewPacket& outPacket)
{
    outPacket.simulationPacketsCommitted = telemetry.simulationPacketsCommitted;
    outPacket.simulationPreviousFrameConsumes = telemetry.simulationPreviousFrameConsumes;
    outPacket.simulationLockstepConsumes = telemetry.simulationLockstepConsumes;
    outPacket.simulationFallbacks = telemetry.simulationFallbacks;
}

inline SimulationReuseTelemetryViewPacket BuildSimulationReuseTelemetryViewPacket(
    const FrameReuseDomain::FrameReuseTelemetry& telemetry)
{
    SimulationReuseTelemetryViewPacket packet{};
    SeedSimulationReuseTelemetryViewPacket(telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
