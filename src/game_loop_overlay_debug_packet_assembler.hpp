#pragma once

#include "game_loop_observability_state_assembler.hpp"
#include "game_loop_overlay_debug_contracts.hpp"
#include "game_loop_overlay_debug_text_assembler.hpp"

namespace GameLoopObservabilityDomain
{

struct OverlayDebugFrameInputs
{
    GameLoopOverlayDomain::SegmentOverlayPacket segment{};
    GameLoopOverlayDomain::WindowOverlayPacket window{};
    GameLoopOverlayDomain::NearestSegmentPacket nearest{};
    GameLoopOverlayDomain::OverlayDiagnosticsPacket diagnostics{};
    GameLoopOverlayDomain::OverlayEventPacket events{};
    GameLoopTelemetryDomain::OverlayFaceShadowPacket faceShadow{};
    GameLoopTelemetryDomain::OverlayGroundProbePacket groundProbe{};
    GameLoopTelemetryDomain::OverlayPhysicsQueryPacket physicsQuery{};
    GameLoopTelemetryDomain::OverlaySegmentEventPacket segmentEvent{};
    GameLoopTelemetryDomain::Sh2TelemetryPacket sh2{};
    GameLoopTelemetryDomain::RealtimeFpsPacket realtimeFps{};
    int32_t carYawDeg = 0;
    int32_t shadowWorldX = 0;
    int32_t shadowWorldY = 0;
    int32_t shadowWorldZ = 0;
    int32_t shadowYawDeg = 0;
};

inline void SeedOverlayDebugBundle(const OverlayPacketFlow& overlayFlow,
                                   const TelemetryPacketFlow& telemetryFlow,
                                   const OverlayDebugTextBundle& text,
                                   OverlayDebugBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.overlayFlow = overlayFlow;
    outBundle.telemetryFlow = telemetryFlow;
    outBundle.text = text;
}

inline OverlayDebugBundle BuildOverlayDebugBundle(const OverlayPacketFlow& overlayFlow,
                                                  const TelemetryPacketFlow& telemetryFlow,
                                                  const OverlayDebugTextBundle& text)
{
    OverlayDebugBundle bundle{};
    SeedOverlayDebugBundle(overlayFlow, telemetryFlow, text, bundle);
    return bundle;
}

inline OverlayDebugBundle BuildOverlayDebugBundle(const OverlayDebugFrameInputs& inputs)
{
    const auto overlayFlow = BuildOverlayPacketFlow(
        inputs.segment, inputs.window, inputs.nearest, inputs.diagnostics, inputs.events);
    const auto telemetryFlow = BuildTelemetryPacketFlow(inputs.faceShadow,
                                                        inputs.groundProbe,
                                                        inputs.physicsQuery,
                                                        inputs.segmentEvent,
                                                        inputs.sh2,
                                                        inputs.realtimeFps);
    const auto text = BuildOverlayDebugTextBundle(inputs.diagnostics.snapshot,
                                                  inputs.carYawDeg,
                                                  inputs.shadowWorldX,
                                                  inputs.shadowWorldY,
                                                  inputs.shadowWorldZ,
                                                  inputs.shadowYawDeg,
                                                  inputs.faceShadow,
                                                  inputs.groundProbe,
                                                  inputs.physicsQuery,
                                                  inputs.segmentEvent);
    return BuildOverlayDebugBundle(overlayFlow, telemetryFlow, text);
}

} // namespace GameLoopObservabilityDomain
