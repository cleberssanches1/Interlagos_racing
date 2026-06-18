#pragma once

#include "frame_reuse_contracts.hpp"

namespace FrameReuseDomain
{

inline void ResetFrameReuseTelemetry(FrameReuseTelemetry& ioTelemetry)
{
    ioTelemetry = FrameReuseTelemetry{};
}

inline void SeedSimulationReuseTelemetry(const SimulationReuseDecisionPacket& packet,
                                         FrameReuseTelemetry& ioTelemetry)
{
    if (packet.hasCommittedPacket && packet.committedFrameId > 0u)
    {
        ioTelemetry.simulationPacketsCommitted =
            ioTelemetry.simulationPacketsCommitted + 1u;
    }
    if (packet.shouldConsumeCommitted)
    {
        if (packet.mode == ReuseMode::Lockstep)
        {
            ioTelemetry.simulationLockstepConsumes =
                ioTelemetry.simulationLockstepConsumes + 1u;
        }
        else
        {
            ioTelemetry.simulationPreviousFrameConsumes =
                ioTelemetry.simulationPreviousFrameConsumes + 1u;
        }
    }
    if (packet.requiresSynchronousFallback)
    {
        ioTelemetry.simulationFallbacks =
            ioTelemetry.simulationFallbacks + 1u;
    }
}

inline void SeedTrackReuseTelemetry(const TrackReuseDecisionPacket& packet,
                                    FrameReuseTelemetry& ioTelemetry)
{
    if (packet.hasCommittedPacket && packet.committedFrameId > 0u)
    {
        ioTelemetry.trackPacketsCommitted =
            ioTelemetry.trackPacketsCommitted + 1u;
    }
    if (packet.shouldConsumeCommitted)
    {
        if (packet.mode == ReuseMode::Lockstep)
        {
            ioTelemetry.trackLockstepConsumes =
                ioTelemetry.trackLockstepConsumes + 1u;
        }
        else
        {
            ioTelemetry.trackPreviousFrameConsumes =
                ioTelemetry.trackPreviousFrameConsumes + 1u;
        }
    }
    if (packet.requiresSynchronousFallback)
    {
        ioTelemetry.trackFallbacks =
            ioTelemetry.trackFallbacks + 1u;
    }
}

} // namespace FrameReuseDomain
