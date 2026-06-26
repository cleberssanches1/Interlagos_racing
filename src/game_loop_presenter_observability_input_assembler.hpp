#pragma once

#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket& schedulerReuse,
    PresenterObservabilityInputPacket& outPacket)
{
    outPacket.valid = overlay.valid || observability.valid || schedulerReuse.valid;
    outPacket.overlay = overlay;
    outPacket.overlayDebug = BuildPresenterOverlayDebugPacket(overlay, observability);
    outPacket.observability = observability;
    outPacket.schedulerReuse = schedulerReuse;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket& schedulerReuse)
{
    PresenterObservabilityInputPacket packet{};
    SeedPresenterObservabilityInputPacket(overlay, observability, schedulerReuse, packet);
    return packet;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability)
{
    return BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket{});
}

} // namespace GameLoopRuntime
