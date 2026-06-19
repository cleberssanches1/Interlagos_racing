#pragma once

#include <cstdint>

#include "game_loop_memory_presentation_contracts.hpp"
#include "game_loop_overlay_contracts.hpp"
#include "game_loop_telemetry_contracts.hpp"

namespace GameLoopObservabilityDomain
{

enum class Stage : uint8_t
{
    Overlay = 0,
    Telemetry,
    Presentation
};

struct OverlayPacketFlow
{
    bool valid = false;
    GameLoopOverlayDomain::SegmentOverlayPacket segment{};
    GameLoopOverlayDomain::WindowOverlayPacket window{};
    GameLoopOverlayDomain::NearestSegmentPacket nearest{};
    GameLoopOverlayDomain::OverlayDiagnosticsPacket diagnostics{};
    GameLoopOverlayDomain::OverlayEventPacket events{};
};

struct TelemetryPacketFlow
{
    bool valid = false;
    GameLoopTelemetryDomain::OverlayFaceShadowPacket faceShadow{};
    GameLoopTelemetryDomain::OverlayGroundProbePacket groundProbe{};
    GameLoopTelemetryDomain::OverlayPhysicsQueryPacket physicsQuery{};
    GameLoopTelemetryDomain::OverlaySegmentEventPacket segmentEvent{};
    GameLoopTelemetryDomain::Sh2TelemetryPacket sh2{};
    GameLoopTelemetryDomain::RealtimeFpsPacket realtimeFps{};
};

struct MemoryPresentationPacketFlow
{
    bool valid = false;
    GameLoopMemoryPresentationDomain::WorkRamUsagePacket workRamUsage{};
    GameLoopMemoryPresentationDomain::LowWorkOverlayPacket lowWorkOverlay{};
    GameLoopMemoryPresentationDomain::HighWorkTracePacket highWorkTrace{};
    GameLoopMemoryPresentationDomain::LowWorkTracePacket lowWorkTrace{};
};

struct FrameObservabilityPacket
{
    bool valid = false;
    uint32_t frameId = 0u;
    OverlayPacketFlow overlay{};
    TelemetryPacketFlow telemetry{};
    MemoryPresentationPacketFlow memory{};
};

} // namespace GameLoopObservabilityDomain
