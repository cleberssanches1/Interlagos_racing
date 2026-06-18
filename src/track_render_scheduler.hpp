#pragma once

#include "track_render_transition_ops.hpp"

namespace Game
{

class TrackRenderScheduler final
{
public:
    using Ports = TrackRenderDomain::Ports;
    using FrameContext = TrackRenderDomain::TrackFrameContext;
    using RenderPacket = TrackRenderDomain::TrackRenderPacket;
    using Telemetry = TrackRenderDomain::TrackRenderTelemetry;

    static FrameContext BuildFrameContext(uint32_t frameId,
                                          int32_t observedCarSegmentId,
                                          bool renderEnabled,
                                          const SRL::Math::Types::Vector3D& trackOffset,
                                          const SRL::Math::Types::Vector3D& lightDirection,
                                          const SRL::Math::Types::Vector3D& cameraLocation,
                                          const SRL::Math::Types::Vector3D& cameraLookTarget,
                                          const SRL::Math::Types::Vector3D& carWorldPosition)
    {
        return TrackRenderDomain::BuildTrackFrameContext(frameId,
                                                         observedCarSegmentId,
                                                         renderEnabled,
                                                         trackOffset,
                                                         lightDirection,
                                                         cameraLocation,
                                                         cameraLookTarget,
                                                         carWorldPosition);
    }

    static RenderPacket BuildRenderPacket(const FrameContext& context,
                                          const TrackSystem* trackSystem = nullptr)
    {
        return TrackRenderDomain::BuildTrackRenderPacket(context, trackSystem);
    }

    static Telemetry BuildTelemetry(const TrackSystem& trackSystem)
    {
        return TrackRenderDomain::BuildTrackRenderTelemetry(trackSystem);
    }
};

} // namespace Game
