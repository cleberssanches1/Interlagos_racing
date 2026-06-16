#pragma once

#include "track_render_contracts.hpp"

// Assembler passivo da telemetria de render da pista.
// Mantém o recorte de telemetria fora do runtime até a futura integração.

namespace TrackRenderDomain
{

inline void SeedTrackRenderTelemetry(const TrackSystem& trackSystem,
                                     TrackRenderTelemetry& outTelemetry)
{
    outTelemetry.masterFrameTicks = trackSystem.FrameTicksThisFrame();
    outTelemetry.slaveSortTicks = trackSystem.SlaveSortTicksThisFrame();
    outTelemetry.slavePlanTicks = trackSystem.SlavePlanTicksThisFrame();

    const auto& telemetry = trackSystem.Telemetry();
    outTelemetry.slaveProducerTicks = telemetry.producer.slaveLastJobTicks;
    outTelemetry.producerTimeoutFallbacks = telemetry.producer.timeoutFallbacks;
    outTelemetry.producerSafeModeFrames = telemetry.producer.safeModeFrames;
    outTelemetry.producerSafeModeActive = telemetry.producer.safeModeActive;
}

inline void SeedTrackRenderPacketProducerFlags(const TrackSystem& trackSystem,
                                               TrackRenderPacket& ioPacket)
{
    const auto& telemetry = trackSystem.Telemetry();
    ioPacket.submittedTrackFaces = static_cast<uint16_t>(telemetry.submittedTrackFaces);
    ioPacket.usedSlaveProducer = !telemetry.producer.safeModeActive;
    ioPacket.usedSlaveSort = (trackSystem.SlaveSortTicksThisFrame() > 0u);
}

} // namespace TrackRenderDomain
