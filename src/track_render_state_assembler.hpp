#pragma once

#include "track_render_contracts.hpp"

// Assembler passivo do contexto/pacote de render da pista.
// Não participa do runtime atual; prepara a futura extração do scheduler.

namespace TrackRenderDomain
{

inline void SeedTrackFrameContext(uint32_t frameId,
                                  int32_t observedCarSegmentId,
                                  bool renderEnabled,
                                  const SRL::Math::Types::Vector3D& trackOffset,
                                  const SRL::Math::Types::Vector3D& lightDirection,
                                  const SRL::Math::Types::Vector3D& cameraLocation,
                                  const SRL::Math::Types::Vector3D& cameraLookTarget,
                                  const SRL::Math::Types::Vector3D& carWorldPosition,
                                  TrackFrameContext& outContext)
{
    outContext.renderEnabled = renderEnabled;
    outContext.frameId = frameId;
    outContext.observedCarSegmentId = observedCarSegmentId;
    outContext.trackOffset = trackOffset;
    outContext.lightDirection = lightDirection;
    outContext.cameraLocation = cameraLocation;
    outContext.cameraLookTarget = cameraLookTarget;
    outContext.carWorldPosition = carWorldPosition;
}

inline void SeedTrackRenderPacket(const TrackFrameContext& context,
                                  TrackRenderPacket& outPacket)
{
    outPacket.valid = context.renderEnabled;
    outPacket.observedCarSegmentId = context.observedCarSegmentId;
}

} // namespace TrackRenderDomain
