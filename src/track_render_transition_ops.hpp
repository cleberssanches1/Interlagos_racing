#pragma once

#include "track_render_state_assembler.hpp"
#include "track_render_telemetry_assembler.hpp"

// Operacoes passivas externas para futura reintroducao de `TrackRenderScheduler`.
// Nao devem ser integradas ao runtime critico nesta fase.

namespace TrackRenderDomain
{

inline TrackFrameContext BuildTrackFrameContext(uint32_t frameId,
                                                int32_t observedCarSegmentId,
                                                bool renderEnabled,
                                                const SRL::Math::Types::Vector3D& trackOffset,
                                                const SRL::Math::Types::Vector3D& lightDirection,
                                                const SRL::Math::Types::Vector3D& cameraLocation,
                                                const SRL::Math::Types::Vector3D& cameraLookTarget,
                                                const SRL::Math::Types::Vector3D& carWorldPosition)
{
    TrackFrameContext context{};
    SeedTrackFrameContext(frameId,
                          observedCarSegmentId,
                          renderEnabled,
                          trackOffset,
                          lightDirection,
                          cameraLocation,
                          cameraLookTarget,
                          carWorldPosition,
                          context);
    return context;
}

inline TrackRenderPacket BuildTrackRenderPacket(const TrackFrameContext& context,
                                                const TrackSystem* trackSystem = nullptr)
{
    TrackRenderPacket packet{};
    SeedTrackRenderPacket(context, packet);
    if (trackSystem)
    {
        SeedTrackRenderPacketProducerFlags(*trackSystem, packet);
    }
    return packet;
}

inline TrackRenderTelemetry BuildTrackRenderTelemetry(const TrackSystem& trackSystem)
{
    TrackRenderTelemetry telemetry{};
    SeedTrackRenderTelemetry(trackSystem, telemetry);
    return telemetry;
}

} // namespace TrackRenderDomain
